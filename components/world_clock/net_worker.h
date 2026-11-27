#pragma once

#include "esphome/core/defines.h"

#if defined(USE_WORLD_CLOCK_NET_WORKER)

#include "clock_math.h"
#include "ics_parser.h"
#include <atomic>
#include <new>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_tls.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include "esphome/core/hal.h"

namespace world_clock {

// Cross-core shared telemetry mailbox (Core 0 NetWorker <-> Core 1 LVGL/ESPHome main loop).
// Uses FreeRTOS mutex for multi-field structs and C++17 std::atomic for single-word flags (zero UB / zero spinlocks!).
struct StandaloneTelemetry {
  SemaphoreHandle_t mtx{nullptr};

  std::atomic<bool> wifi_connected{false};
  std::atomic<bool> time_valid{false};
  std::atomic<bool> sync_busy{false};
  std::atomic<bool> force_sync{false};
  std::atomic<bool> cal_force_sync{false};
  std::atomic<bool> ota_active{false};
  std::atomic<bool> iss_valid{false};
  std::atomic<uint8_t> sleep_phase{1};
  std::atomic<uint8_t> active_occasion{1};
  std::atomic<int> local_utc_offset_min{0};

  // Protected by `mtx`:
  float work_lat{51.5074f};
  float work_lon{-0.1278f};
  int loc_year{0};
  int loc_month{0};
  int loc_day{0};

  // Weather payload (Core 0 writes under mtx; Core 1 reads non-blocking)
  bool wx_valid{false};
  bool wx_dirty{false};
  uint32_t wx_ms{0};
  float wx_temp{NAN};
  float wx_feels{NAN};
  float wx_high{NAN};
  float wx_low{NAN};
  char cond_buf[32]{"cloudy"};
  char wx_today_cond_buf[32]{""};
  int wx_today_precip_pct{-1};
  char forecast_3day_buf[128]{""};

  // ISS payload (protected by mtx; iss_valid is atomic above)
  uint32_t iss_ms{0};
  float iss_lat{0.0f};
  float iss_lon{0.0f};
  bool iss_eclipsed{false};

  // Optional iCloud / Google / School Calendar (.ics / webcal://) feeds (protected by mtx)
  char family_cal_url[256]{""};
  char school_cal_url[256]{""};
  IcsFeedSummary family_cal{};
  IcsFeedSummary school_cal{};
  char cal_diag[96]{"Waiting for sync"};
  bool cal_dirty{false};
  bool cal_diag_dirty{false};

  void ensure_mutex() {
    if (mtx == nullptr) mtx = xSemaphoreCreateMutex();
  }
  bool lock(TickType_t wait_ticks = pdMS_TO_TICKS(50)) {
    return (mtx != nullptr) && (xSemaphoreTake(mtx, wait_ticks) == pdTRUE);
  }
  bool try_lock() { return (mtx != nullptr) && (xSemaphoreTake(mtx, 0) == pdTRUE); }
  void unlock() {
    if (mtx != nullptr) xSemaphoreGive(mtx);
  }
};

inline StandaloneTelemetry &standalone_telemetry() {
  static StandaloneTelemetry t;
  return t;
}

class NetWorker {
 public:
  static void start(float init_lat, float init_lon) {
    static std::atomic<bool> s_started{false};
    if (s_started.exchange(true, std::memory_order_acq_rel)) return;
    auto &t = standalone_telemetry();
    t.ensure_mutex();
    if (t.lock(pdMS_TO_TICKS(50))) {
      t.work_lat = init_lat;
      t.work_lon = init_lon;
      t.unlock();
    }
    // Allocate worker stack cleanly: try 10KB internal SRAM first, then 8KB internal SRAM,
    // and finally fall back to 14KB in 8MB Octal PSRAM (NetWorker never touches SPI flash/NVS!)
    // so fragmented internal SRAM (LVGL + Wi-Fi + Noise API) can NEVER silently prevent NetWorker from spawning!
    const char *stack_src = "OOM";
    BaseType_t ok = xTaskCreatePinnedToCore(&NetWorker::task_loop, "cloud_sync", 10240, nullptr, 1, nullptr, 0);
    if (ok == pdPASS) {
      stack_src = "SRAM-10K";
    } else {
      ok = xTaskCreatePinnedToCore(&NetWorker::task_loop, "cloud_sync", 8192, nullptr, 1, nullptr, 0);
      if (ok == pdPASS) {
        stack_src = "SRAM-8K";
      }
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
      else {
        ok = xTaskCreatePinnedToCoreWithCaps(&NetWorker::task_loop, "cloud_sync", 14336, nullptr, 1, nullptr, 0,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ok == pdPASS) stack_src = "PSRAM-14K";
      }
#endif
    }
    if (t.lock(pdMS_TO_TICKS(50))) {
      if (ok == pdPASS) {
        snprintf(t.cal_diag, sizeof(t.cal_diag), "Worker started (%s)", stack_src);
      } else {
        size_t max_blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        snprintf(t.cal_diag, sizeof(t.cal_diag), "Worker OOM (maxInt=%uB)", (unsigned)max_blk);
      }
      t.cal_diag_dirty = true;
      t.unlock();
    }
  }

 private:
  static constexpr size_t kMaxIcsBytes = 2UL * 1024UL * 1024UL;  // 2 MB hard streaming cap
  static constexpr uint32_t kMaxIcsWallMs = 20000UL;             // 20s hard wall-clock cap (anti-tarpit)

  struct HttpBuf {
    char *data;
    size_t len;
    size_t cap;
    bool truncated{false};
  };

  struct IcsStreamCtx {
    IcsStreamParser *parser{nullptr};
    size_t *total_bytes{nullptr};
    uint32_t chain_start_ms{0};
    bool aborted{false};
    int tls_err{0};
    int mbedtls_err{0};
    int cert_flags{0};
    char redirect_loc[288]{""};
  };

  static bool is_private_sockaddr(const struct addrinfo *ai) {
    if (ai == nullptr || ai->ai_addr == nullptr) return true;
    if (ai->ai_family == AF_INET) {
      const struct sockaddr_in *sin = (const struct sockaddr_in *)ai->ai_addr;
      uint32_t ip = ntohl(sin->sin_addr.s_addr);
      return is_private_ipv4_octets((uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip);
    }
#if LWIP_IPV6
    if (ai->ai_family == AF_INET6) {
      const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)ai->ai_addr;
      const uint8_t *b = (const uint8_t *)&sin6->sin6_addr;
      bool all_zero = true;
      for (int i = 0; i < 15; i++) {
        if (b[i] != 0) {
          all_zero = false;
          break;
        }
      }
      if (all_zero && (b[15] == 0 || b[15] == 1)) return true;  // :: or ::1
      if ((b[0] & 0xFE) == 0xFC) return true;                   // fc00::/7 ULA
      if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;   // fe80::/10 link-local
      if (b[0] == 0xFF) return true;                            // ff00::/8 multicast
      // IPv4-mapped IPv6 ::ffff:a.b.c.d
      bool v4_mapped = true;
      for (int i = 0; i < 10; i++) {
        if (b[i] != 0) v4_mapped = false;
      }
      if (v4_mapped && b[10] == 0xFF && b[11] == 0xFF) {
        return is_private_ipv4_octets(b[12], b[13], b[14], b[15]);
      }
      return false;
    }
#endif
    return true;
  }

  // Resolves host via DNS and rejects any URL whose host resolves to a loopback/RFC1918/link-local/CGNAT/ULA IP!
  static bool verify_public_dns(const char *url, char *err_out = nullptr, size_t err_cap = 0) {
    if (!is_public_https_url(url)) {
      if (err_out && err_cap) snprintf(err_out, err_cap, "bad_host");
      return false;
    }
    char host[128];
    if (!extract_https_host(url, host, sizeof(host))) {
      if (err_out && err_cap) snprintf(err_out, err_cap, "bad_host");
      return false;
    }
    struct addrinfo hints = {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *res = nullptr;
    int gai_rc = getaddrinfo(host, nullptr, &hints, &res);
    if (gai_rc != 0 || res == nullptr) {
      if (err_out && err_cap) snprintf(err_out, err_cap, "dns=%d", gai_rc);
      return false;
    }
    bool all_public = true;
    int count = 0;
    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
      count++;
      if (is_private_sockaddr(p)) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "priv_ip(af=%d)", p->ai_family);
        all_public = false;
        break;
      }
    }
    freeaddrinfo(res);
    if (count == 0 && err_out && err_cap) snprintf(err_out, err_cap, "dns_empty");
    return (count > 0 && all_public);
  }

  static esp_err_t on_http_data(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data != nullptr && evt->data_len > 0) {
      HttpBuf *hb = (HttpBuf *)evt->user_data;
      size_t avail = (hb->len + 1 < hb->cap) ? (hb->cap - 1 - hb->len) : 0;
      if ((size_t)evt->data_len > avail) {
        hb->truncated = true;
        return ESP_FAIL;
      }
      memcpy(hb->data + hb->len, evt->data, (size_t)evt->data_len);
      hb->len += (size_t)evt->data_len;
      hb->data[hb->len] = '\0';
    }
    return ESP_OK;
  }

  static esp_err_t on_ics_stream_data(esp_http_client_event_t *evt) {
    if (evt->user_data == nullptr) return ESP_OK;
    IcsStreamCtx *ctx = (IcsStreamCtx *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ERROR && evt->data != nullptr &&
        evt->data_len >= (int)sizeof(esp_tls_last_error_t)) {
      const esp_tls_last_error_t *te = (const esp_tls_last_error_t *)evt->data;
      ctx->tls_err = (int)te->last_error;
      ctx->mbedtls_err = (int)te->esp_tls_error_code;
      ctx->cert_flags = (int)te->esp_tls_flags;
    } else if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != nullptr && evt->header_value != nullptr) {
      if (strcasecmp(evt->header_key, "Location") == 0 && ctx->redirect_loc[0] == '\0') {
        snprintf(ctx->redirect_loc, sizeof(ctx->redirect_loc), "%s", evt->header_value);
      }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
      if (ctx->total_bytes != nullptr) *ctx->total_bytes += (size_t)evt->data_len;
      uint32_t elapsed = (uint32_t)(esphome::millis() - ctx->chain_start_ms);
      if ((ctx->total_bytes != nullptr && *ctx->total_bytes > kMaxIcsBytes) || elapsed > kMaxIcsWallMs) {
        ctx->aborted = true;
        return ESP_FAIL;
      }
      // Only feed HTTP 2xx bodies into the ICS parser (never pollute parser with 301/302 HTML redirect pages!)
      int st = esp_http_client_get_status_code(evt->client);
      if (st >= 200 && st < 300 && ctx->parser != nullptr) {
        ctx->parser->feed((const char *)evt->data, (size_t)evt->data_len);
      }
    }
    return ESP_OK;
  }

  // Always HTTPS with mbedTLS Mozilla CA bundle & strict truncation detection.
  static bool fetch_https(const char *url, char *buf, size_t cap, int timeout_ms) {
    if (buf == nullptr || cap < 16 || url == nullptr || strncasecmp(url, "https://", 8) != 0) return false;
    buf[0] = '\0';
    HttpBuf hb{buf, 0, cap, false};
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = timeout_ms;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 512;
    cfg.event_handler = &NetWorker::on_http_data;
    cfg.user_data = &hb;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) return false;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (err == ESP_OK && !hb.truncated && status == 200 && hb.len > 10);
  }

  // Resolves a redirect Location header against current `cur_url` while enforcing HTTPS-only!
  static bool resolve_https_redirect(const char *cur_url, const char *loc, char *out, size_t cap) {
    if (loc == nullptr || loc[0] == '\0' || out == nullptr || cap < 16) return false;
    if (strncasecmp(loc, "https://", 8) == 0) {
      return snprintf(out, cap, "%s", loc) < (int)cap;
    }
    if (strncmp(loc, "//", 2) == 0) {
      return snprintf(out, cap, "https:%s", loc) < (int)cap;
    }
    if (loc[0] == '/') {
      // Root-relative path on same origin (keep https://<authority>)
      const char *slash = strchr(cur_url + 8, '/');
      size_t origin_len = (slash != nullptr) ? (size_t)(slash - cur_url) : strlen(cur_url);
      if (origin_len + strlen(loc) + 1 >= cap) return false;
      snprintf(out, cap, "%.*s%s", (int)origin_len, cur_url, loc);
      return true;
    }
    // Reject any scheme prefix like http:, javascript:, data: (colon before first slash)
    for (const char *p = loc; *p && *p != '/' && *p != '?' && *p != '#'; p++) {
      if (*p == ':') return false;
    }
    // Directory-relative path (e.g. Location: calendar.ics) resolved against last '/' after https://host/
    const char *path_start = strchr(cur_url + 8, '/');
    if (path_start == nullptr) {
      return snprintf(out, cap, "%s/%s", cur_url, loc) < (int)cap;
    }
    const char *last_slash = strrchr(path_start, '/');
    size_t dir_len = (size_t)(last_slash - cur_url) + 1;
    if (dir_len + strlen(loc) + 1 >= cap) return false;
    snprintf(out, cap, "%.*s%s", (int)dir_len, cur_url, loc);
    return true;
  }

  // Streams iCloud / Google / School .ics feeds straight into IcsStreamParser (< 2 KB RAM)
  // with strict HTTPS, CA bundle, manual redirect validation (anti-downgrade & anti-SSRF), full-chain byte & time caps!
  static bool fetch_ics_stream(const char *raw_url, IcsStreamParser &parser, char *norm_url, size_t norm_cap,
                               char *diag_out = nullptr, size_t diag_cap = 0) {
    auto set_diag = [&](const char *msg) {
      if (diag_out && diag_cap) snprintf(diag_out, diag_cap, "%s", msg);
    };
    if (!normalize_calendar_url(raw_url, norm_url, norm_cap)) {
      set_diag("bad_url");
      return false;
    }

    uint32_t chain_start_ms = esphome::millis();
    size_t chain_total_bytes = 0;

    for (int hop = 0; hop <= 3; hop++) {
      if ((uint32_t)(esphome::millis() - chain_start_ms) > kMaxIcsWallMs) {
        set_diag("timeout");
        return false;
      }
      char dns_err[24] = "";
      if (!verify_public_dns(norm_url, dns_err, sizeof(dns_err))) {
        set_diag(dns_err[0] ? dns_err : "dns_fail");
        return false;
      }
      parser.reset();

      IcsStreamCtx ctx;
      ctx.parser = &parser;
      ctx.total_bytes = &chain_total_bytes;
      ctx.chain_start_ms = chain_start_ms;

      esp_http_client_config_t cfg = {};
      cfg.url = norm_url;
      cfg.method = HTTP_METHOD_GET;
      cfg.timeout_ms = 8000;
      cfg.buffer_size = 2048;
      cfg.buffer_size_tx = 512;
      cfg.event_handler = &NetWorker::on_ics_stream_data;
      cfg.user_data = &ctx;
      cfg.crt_bundle_attach = esp_crt_bundle_attach;
      cfg.disable_auto_redirect = true;

      esp_http_client_handle_t client = esp_http_client_init(&cfg);
      if (client == nullptr) {
        set_diag("init_err");
        return false;
      }
      esp_http_client_set_header(client, "Accept-Encoding", "identity");
      esp_http_client_set_header(client, "Connection", "close");
      esp_http_client_set_header(client, "User-Agent", "WorldClock/1.3");

      esp_err_t err = esp_http_client_perform(client);
      int status = esp_http_client_get_status_code(client);
      esp_http_client_cleanup(client);

      if (err != ESP_OK || ctx.aborted) {
        if (diag_out && diag_cap) {
          if (ctx.aborted) snprintf(diag_out, diag_cap, "cap(%uB)", (unsigned)chain_total_bytes);
          else if (ctx.mbedtls_err != 0 || ctx.cert_flags != 0)
            snprintf(diag_out, diag_cap, "tls=-0x%x/f=0x%x", (unsigned)(-ctx.mbedtls_err), (unsigned)ctx.cert_flags);
          else
            snprintf(diag_out, diag_cap, "err=0x%x/st=%d", (unsigned)err, status);
        }
        return false;
      }
      if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        if (hop == 3 || ctx.redirect_loc[0] == '\0') {
          set_diag("redir_fail");
          return false;
        }
        char next_url[288];
        if (!resolve_https_redirect(norm_url, ctx.redirect_loc, next_url, sizeof(next_url))) {
          set_diag("redir_http");
          return false;
        }
        snprintf(norm_url, norm_cap, "%s", next_url);
        continue;
      }
      if (status < 200 || status >= 300) {
        if (diag_out && diag_cap) snprintf(diag_out, diag_cap, "http=%d", status);
        return false;
      }
      parser.finish();
      if (!parser.summary().valid) {
        if (diag_out && diag_cap) snprintf(diag_out, diag_cap, "no_ics(%uB)", (unsigned)chain_total_bytes);
        return false;
      }
      if (diag_out && diag_cap) {
        snprintf(diag_out, diag_cap, "OK(%dev%s)", parser.summary().events_seen,
                 parser.summary().today_is_school_holiday ? ",HOL" : "");
      }
      return true;
    }
    set_diag("redir_loop");
    return false;
  }

  // PSRAM scratch arena so large ICS parser & summary structs never sit on the FreeRTOS stack
  struct WorkerScratch {
    char rx[4096];
    char url_buf[288];
    char fc_local[128];
    char fam_u[256];
    char sch_u[256];
    IcsStreamParser parser;
    IcsFeedSummary fam_res;
    IcsFeedSummary sch_res;
  };

  static void task_loop(void *) {
    auto &t = standalone_telemetry();
    constexpr size_t kRxCap = sizeof(((WorkerScratch *)0)->rx);
    constexpr size_t kUrlCap = sizeof(((WorkerScratch *)0)->url_buf);
    constexpr size_t kFcCap = sizeof(((WorkerScratch *)0)->fc_local);
    void *raw_ws = heap_caps_malloc(sizeof(WorkerScratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw_ws == nullptr) {
      raw_ws = heap_caps_malloc(sizeof(WorkerScratch), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    WorkerScratch *ws = (raw_ws != nullptr) ? new (raw_ws) WorkerScratch() : nullptr;
    char *arena = (char *)ws;
    char *rx = (ws != nullptr) ? ws->rx : nullptr;
    char *url_buf = (ws != nullptr) ? ws->url_buf : nullptr;
    char *fc_local = (ws != nullptr) ? ws->fc_local : nullptr;

    uint32_t next_wx_ms = 0;
    uint32_t next_iss_ms = 0;
    uint32_t next_cal_ms = 0;
    uint32_t wx_backoff_ms = 15000UL;
    uint32_t iss_backoff_ms = 15000UL;
    bool sntp_ready_once = false;

    const char *kIssPrimary = "https://api.wheretheiss.at/v1/satellites/25544";

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(500));
      bool is_ota = t.ota_active.load(std::memory_order_acquire);
      bool wifi_ok = t.wifi_connected.load(std::memory_order_acquire);
      if (arena == nullptr || is_ota || !wifi_ok) {
        continue;
      }

      uint32_t now_ms = esphome::millis();

      // Strictly wait for valid SNTP/HA wall-clock time before performing TLS cert verification!
      if (!sntp_ready_once) {
        if (t.time_valid.load(std::memory_order_acquire)) {
          sntp_ready_once = true;
          next_wx_ms = now_ms;
          next_iss_ms = now_ms;
          next_cal_ms = now_ms + 2000UL;
        } else {
          continue;
        }
      }

      if (t.force_sync.exchange(false, std::memory_order_acq_rel)) {
        next_wx_ms = now_ms;
        next_iss_ms = now_ms;
        next_cal_ms = now_ms;
      }
      if (t.cal_force_sync.exchange(false, std::memory_order_acq_rel)) {
        next_cal_ms = now_ms;
      }

      // 1. Open-Meteo Weather + 3-Day Forecast (Standalone mode only)
#if defined(USE_WORLD_CLOCK_STANDALONE_POLL)
      if ((int32_t)(now_ms - next_wx_ms) >= 0) {
        t.sync_busy.store(true, std::memory_order_release);
        float q_lat = 51.5074f, q_lon = -0.1278f;
        if (t.lock()) {
          q_lat = std::clamp(t.work_lat, -85.0f, 85.0f);
          q_lon = std::clamp(t.work_lon, -180.0f, 180.0f);
          t.unlock();
        }

        snprintf(url_buf, kUrlCap,
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,apparent_temperature,weather_code,is_day"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                 "&timezone=auto&forecast_days=4",
                 q_lat, q_lon);

        bool ok_wx = fetch_https(url_buf, rx, kRxCap, 6000);
        if (ok_wx) {
          const char *cur_sec = strstr(rx, "\"current\"");
          const char *cur_end = (cur_sec != nullptr) ? find_json_object_end(cur_sec) : nullptr;
          const char *day_sec = strstr(rx, "\"daily\"");
          float t_cur = NAN, t_app = NAN, w_code_f = 3.0f, is_day_f = 1.0f;
          if (cur_sec != nullptr && cur_end != nullptr &&
              extract_json_num_bounded(cur_sec, cur_end, "\"temperature_2m\"", t_cur) &&
              extract_json_num_bounded(cur_sec, cur_end, "\"apparent_temperature\"", t_app) &&
              extract_json_num_bounded(cur_sec, cur_end, "\"weather_code\"", w_code_f)) {
            extract_json_num_bounded(cur_sec, cur_end, "\"is_day\"", is_day_f);
            const char *cond_str = wmo_to_cond((int)roundf(w_code_f), is_day_f >= 0.5f);

            float d_max[] = {NAN, NAN, NAN, NAN};
            float d_min[] = {NAN, NAN, NAN, NAN};
            float d_wmo[] = {3, 3, 3, 3};
            float d_pp[] = {-1, -1, -1, -1};
            bool have_pp = false;
            fc_local[0] = '\0';
            bool have_daily = false;

            if (day_sec != nullptr) {
              const char *p_max = strstr(day_sec, "\"temperature_2m_max\"");
              const char *p_min = strstr(day_sec, "\"temperature_2m_min\"");
              const char *p_wmo = strstr(day_sec, "\"weather_code\"");
              const char *p_pp = strstr(day_sec, "\"precipitation_probability_max\"");
              const char *p_tm = strstr(day_sec, "\"time\"");
              if (p_pp != nullptr && parse_csv_floats(p_pp + 31, d_pp, 4)) have_pp = true;
              if (p_max && p_min && p_wmo && p_tm && parse_csv_floats(p_max + 20, d_max, 4) &&
                  parse_csv_floats(p_min + 20, d_min, 4) && parse_csv_floats(p_wmo + 14, d_wmo, 4)) {
                have_daily = true;
                const char *q = strchr(p_tm + 6, '[');
                int pos = 0;
                for (int d_i = 0; d_i < 4 && q != nullptr; d_i++) {
                  q = strchr(q, '"');
                  if (q == nullptr) break;
                  int yy = 0, mm = 0, dd = 0;
                  if (sscanf(q + 1, "%d-%d-%d", &yy, &mm, &dd) == 3 && d_i >= 1) {
                    int n = snprintf(fc_local + pos, kFcCap - (size_t)pos, "%s%s,%s,%.0f,%.0f", (d_i > 1) ? "|" : "",
                                     ymd_to_dow(yy, mm, dd), wmo_to_cond((int)roundf(d_wmo[d_i]), true), d_max[d_i],
                                     d_min[d_i]);
                    if (n > 0 && pos + n < (int)kFcCap) pos += n;
                  }
                  q = strchr(q + 1, '"');
                  if (q != nullptr) q++;
                }
              }
            }

            if (t.lock(pdMS_TO_TICKS(100))) {
              t.wx_temp = t_cur;
              t.wx_feels = t_app;
              if (have_daily) {
                t.wx_high = d_max[0];
                t.wx_low = d_min[0];
                snprintf(t.wx_today_cond_buf, sizeof(t.wx_today_cond_buf), "%s",
                         wmo_to_cond((int)roundf(d_wmo[0]), true));
                t.wx_today_precip_pct = have_pp ? (int)roundf(std::clamp(d_pp[0], 0.0f, 100.0f)) : -1;
                if (fc_local[0] != '\0') snprintf(t.forecast_3day_buf, sizeof(t.forecast_3day_buf), "%s", fc_local);
              }
              snprintf(t.cond_buf, sizeof(t.cond_buf), "%s", cond_str);
              t.wx_ms = esphome::millis();
              t.wx_valid = true;
              t.wx_dirty = true;
              t.unlock();
            }

            wx_backoff_ms = 15000UL;
            next_wx_ms = esphome::millis() + 900000UL;
          } else {
            ok_wx = false;
          }
        }
        if (!ok_wx) {
          next_wx_ms = esphome::millis() + wx_backoff_ms;
          wx_backoff_ms = std::min(wx_backoff_ms * 2UL, 300000UL);
        }
        t.sync_busy.store(false, std::memory_order_release);
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      // 2. HTTPS-Only Live ISS Poll (WhereTheISS.at TLS)
      uint8_t cur_slp = t.sleep_phase.load(std::memory_order_relaxed);
      uint8_t cur_occ = t.active_occasion.load(std::memory_order_relaxed);
      bool need_iss = (cur_slp == 0 && cur_occ == 1) || !t.iss_valid.load(std::memory_order_relaxed);
      now_ms = esphome::millis();
      if (!need_iss) {
        next_iss_ms = now_ms;  // Prevent 24.8-day rollover across Advent / holidays
      } else if ((int32_t)(now_ms - next_iss_ms) >= 0) {
        t.sync_busy.store(true, std::memory_order_release);
        float ilat = 0.0f, ilon = 0.0f;
        bool eclipsed = false, ok_iss = false;

        if (fetch_https(kIssPrimary, rx, kRxCap, 5000) && extract_json_num(rx, "\"latitude\"", ilat) &&
            extract_json_num(rx, "\"longitude\"", ilon) && ilat >= -90.0f && ilat <= 90.0f && ilon >= -180.0f &&
            ilon <= 180.0f) {
          eclipsed = json_key_equals_str(rx, "\"visibility\"", "eclipsed");
          ok_iss = true;
        }

        if (ok_iss) {
          if (t.lock(pdMS_TO_TICKS(100))) {
            t.iss_lat = ilat;
            t.iss_lon = ilon;
            t.iss_eclipsed = eclipsed;
            t.iss_ms = esphome::millis();
            t.unlock();
            t.iss_valid.store(true, std::memory_order_release);
          }
          iss_backoff_ms = 15000UL;
          next_iss_ms = esphome::millis() + 45000UL;
        } else {
          next_iss_ms = esphome::millis() + iss_backoff_ms;
          iss_backoff_ms = std::min(iss_backoff_ms * 2UL, 180000UL);
        }
        t.sync_busy.store(false, std::memory_order_release);
      }
#else
      (void)next_wx_ms;
      (void)next_iss_ms;
      (void)wx_backoff_ms;
      (void)iss_backoff_ms;
      (void)kIssPrimary;
#endif

      // 3. Optional Family & School Calendar (.ics / webcal://) Streaming Poll (every 30 min — Standalone & HA Kids!)
      now_ms = esphome::millis();
      if ((int32_t)(now_ms - next_cal_ms) >= 0) {
        ws->fam_u[0] = '\0';
        ws->sch_u[0] = '\0';
        int cy = 0, cm = 0, cd = 0;
        int utc_off_min = t.local_utc_offset_min.load(std::memory_order_relaxed);
        if (t.lock()) {
          snprintf(ws->fam_u, sizeof(ws->fam_u), "%s", t.family_cal_url);
          snprintf(ws->sch_u, sizeof(ws->sch_u), "%s", t.school_cal_url);
          cy = t.loc_year;
          cm = t.loc_month;
          cd = t.loc_day;
          t.unlock();
        }

        if (cy >= 2024 && (ws->fam_u[0] != '\0' || ws->sch_u[0] != '\0')) {
          t.sync_busy.store(true, std::memory_order_release);
          ws->fam_res = IcsFeedSummary{};
          ws->sch_res = IcsFeedSummary{};
          bool ok_fam = false, ok_sch = false;
          char fam_diag[36] = "none";
          char sch_diag[36] = "none";

          if (ws->fam_u[0] != '\0') {
            ws->parser = IcsStreamParser(cy, cm, cd, false, utc_off_min);
            ok_fam = fetch_ics_stream(ws->fam_u, ws->parser, url_buf, kUrlCap, fam_diag, sizeof(fam_diag));
            if (ok_fam) ws->fam_res = ws->parser.summary();
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          if (ws->sch_u[0] != '\0') {
            ws->parser = IcsStreamParser(cy, cm, cd, true, utc_off_min);
            ok_sch = fetch_ics_stream(ws->sch_u, ws->parser, url_buf, kUrlCap, sch_diag, sizeof(sch_diag));
            if (ok_sch) ws->sch_res = ws->parser.summary();
          }

          if (t.lock(pdMS_TO_TICKS(100))) {
            if (ws->fam_u[0] == '\0') t.family_cal = IcsFeedSummary{};
            else if (ok_fam) t.family_cal = ws->fam_res;
            if (ws->sch_u[0] == '\0') t.school_cal = IcsFeedSummary{};
            else if (ok_sch) t.school_cal = ws->sch_res;
            snprintf(t.cal_diag, sizeof(t.cal_diag), "Fam:%s | Sch:%s", fam_diag, sch_diag);
            t.cal_dirty = true;
            t.cal_diag_dirty = true;
            t.unlock();
          }

          t.sync_busy.store(false, std::memory_order_release);
          next_cal_ms = esphome::millis() + ((ok_fam || ok_sch) ? 1800000UL : 120000UL);
        } else {
          if (t.lock(pdMS_TO_TICKS(50))) {
            snprintf(t.cal_diag, sizeof(t.cal_diag), (cy < 2024) ? "Waiting for date" : "No calendar URLs");
            t.cal_diag_dirty = true;
            t.unlock();
          }
          next_cal_ms = esphome::millis() + 15000UL;
        }
      }
    }
  }
};

}  // namespace world_clock

#endif  // USE_WORLD_CLOCK_NET_WORKER
