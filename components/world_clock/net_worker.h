#pragma once

#include "esphome/core/defines.h"

#if defined(USE_WORLD_CLOCK_NET_WORKER)

#include "clock_math.h"
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esphome/core/hal.h"

namespace world_clock {

// Cross-core shared telemetry mailbox for Standalone mode (Core 0 -> Core 1).
struct StandaloneTelemetry {
  bool lock{false};
  bool sync_busy{false};
  bool force_sync{false};
  bool ota_active{false};
  uint8_t sleep_phase{1};
  uint8_t active_occasion{1};
  float work_lat{51.5074f};
  float work_lon{-0.1278f};

  // Weather payload (Core 0 writes under lock; Core 1 reads non-blocking)
  bool wx_valid{false};
  bool wx_dirty{false};
  uint32_t wx_ms{0};
  float wx_temp{NAN};
  float wx_feels{NAN};
  float wx_high{NAN};
  float wx_low{NAN};
  char cond_buf[32]{"cloudy"};
  char forecast_3day_buf[128]{""};

  // ISS payload
  bool iss_valid{false};
  uint32_t iss_ms{0};
  float iss_lat{0.0f};
  float iss_lon{0.0f};
  bool iss_eclipsed{false};

  // Callbacks injected at boot so net_worker.h doesn't hardcode component variable names
  bool (*is_wifi_connected)(){nullptr};
  bool (*is_time_valid)(){nullptr};
};

inline StandaloneTelemetry &standalone_telemetry() {
  static StandaloneTelemetry t;
  return t;
}

class NetWorker {
 public:
  static void start(float init_lat, float init_lon, bool (*wifi_fn)(), bool (*time_fn)()) {
    auto &t = standalone_telemetry();
    t.work_lat = init_lat;
    t.work_lon = init_lon;
    t.is_wifi_connected = wifi_fn;
    t.is_time_valid = time_fn;
    // 12KB stack on Core 0 gives generous headroom for mbedTLS + crt_bundle verification
    xTaskCreatePinnedToCore(&NetWorker::task_loop, "cloud_sync", 12288, nullptr, 1, nullptr, 0);
  }

 private:
  struct HttpBuf {
    char *data;
    size_t len;
    size_t cap;
  };

  static esp_err_t on_http_data(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data != nullptr && evt->data_len > 0) {
      HttpBuf *hb = (HttpBuf *)evt->user_data;
      size_t avail = (hb->len + 1 < hb->cap) ? (hb->cap - 1 - hb->len) : 0;
      size_t copy_n = std::min((size_t)evt->data_len, avail);
      if (copy_n > 0) {
        memcpy(hb->data + hb->len, evt->data, copy_n);
        hb->len += copy_n;
        hb->data[hb->len] = '\0';
      }
    }
    return ESP_OK;
  }

  static bool fetch_url(const char *url, char *buf, size_t cap, int timeout_ms, bool use_tls) {
    if (buf == nullptr || cap < 16) return false;
    buf[0] = '\0';
    HttpBuf hb{buf, 0, cap};
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = timeout_ms;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 512;
    cfg.event_handler = &NetWorker::on_http_data;
    cfg.user_data = &hb;
    if (use_tls) cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) return false;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_header(client, "Connection", "close");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return (err == ESP_OK && status == 200 && hb.len > 10);
  }

  static void task_loop(void *) {
    auto &t = standalone_telemetry();
    constexpr size_t kRxCap = 4096;
    constexpr size_t kUrlCap = 288;
    constexpr size_t kFcCap = 128;
    char *arena = (char *)heap_caps_malloc(kRxCap + kUrlCap + kFcCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *rx = arena;
    char *url_buf = (arena != nullptr) ? (arena + kRxCap) : nullptr;
    char *fc_local = (arena != nullptr) ? (arena + kRxCap + kUrlCap) : nullptr;

    uint32_t next_wx_ms = 0;
    uint32_t next_iss_ms = 0;
    uint32_t wx_backoff_ms = 15000UL;
    uint32_t iss_backoff_ms = 15000UL;
    uint32_t wifi_up_ms = 0;
    bool sntp_ready_once = false;

    const char *kIssPrimary = "https://api.wheretheiss.at/v1/satellites/25544";
    const char *kIssFallback = "http://api.open-notify.org/iss-now.json";

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(500));
      bool is_ota = __atomic_load_n(&t.ota_active, __ATOMIC_ACQUIRE);
      bool wifi_ok = (t.is_wifi_connected != nullptr) && t.is_wifi_connected();
      if (arena == nullptr || is_ota || !wifi_ok) {
        wifi_up_ms = 0;
        continue;
      }

      uint32_t now_ms = esphome::millis();
      if (wifi_up_ms == 0) wifi_up_ms = now_ms;

      // Wait up to 15s for SNTP time sync before the very first HTTPS TLS handshake
      if (!sntp_ready_once) {
        bool t_ok = (t.is_time_valid != nullptr) && t.is_time_valid();
        if (t_ok || (uint32_t)(now_ms - wifi_up_ms) >= 15000UL) {
          sntp_ready_once = true;
          next_wx_ms = now_ms;
          next_iss_ms = now_ms;
        } else {
          continue;
        }
      }

      if (__atomic_exchange_n(&t.force_sync, false, __ATOMIC_ACQ_REL)) {
        next_wx_ms = now_ms;
        next_iss_ms = now_ms;
      }

      // 1. Open-Meteo Weather + 3-Day Forecast
      if ((int32_t)(now_ms - next_wx_ms) >= 0) {
        __atomic_store_n(&t.sync_busy, true, __ATOMIC_RELEASE);
        while (__atomic_test_and_set(&t.lock, __ATOMIC_ACQUIRE)) vTaskDelay(1);
        float q_lat = std::clamp(t.work_lat, -85.0f, 85.0f);
        float q_lon = std::clamp(t.work_lon, -180.0f, 180.0f);
        __atomic_clear(&t.lock, __ATOMIC_RELEASE);

        snprintf(url_buf, kUrlCap,
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,apparent_temperature,weather_code,is_day"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                 "&timezone=auto&forecast_days=4",
                 q_lat, q_lon);

        bool ok_wx = fetch_url(url_buf, rx, kRxCap, 6000, true);
        if (ok_wx) {
          const char *cur_sec = strstr(rx, "\"current\"");
          const char *day_sec = strstr(rx, "\"daily\"");
          float t_cur = NAN, t_app = NAN, w_code_f = 3.0f, is_day_f = 1.0f;
          if (cur_sec != nullptr && extract_json_num(cur_sec, "\"temperature_2m\"", t_cur) &&
              extract_json_num(cur_sec, "\"apparent_temperature\"", t_app) &&
              extract_json_num(cur_sec, "\"weather_code\"", w_code_f)) {
            extract_json_num(cur_sec, "\"is_day\"", is_day_f);
            const char *cond_str = wmo_to_cond((int)roundf(w_code_f), is_day_f >= 0.5f);

            float d_max[] = {NAN, NAN, NAN, NAN};
            float d_min[] = {NAN, NAN, NAN, NAN};
            float d_wmo[] = {3, 3, 3, 3};
            fc_local[0] = '\0';
            bool have_daily = false;

            if (day_sec != nullptr) {
              const char *p_max = strstr(day_sec, "\"temperature_2m_max\"");
              const char *p_min = strstr(day_sec, "\"temperature_2m_min\"");
              const char *p_wmo = strstr(day_sec, "\"weather_code\"");
              const char *p_tm = strstr(day_sec, "\"time\"");
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

            while (__atomic_test_and_set(&t.lock, __ATOMIC_ACQUIRE)) vTaskDelay(1);
            t.wx_temp = t_cur;
            t.wx_feels = t_app;
            if (have_daily) {
              t.wx_high = d_max[0];
              t.wx_low = d_min[0];
              if (fc_local[0] != '\0') snprintf(t.forecast_3day_buf, sizeof(t.forecast_3day_buf), "%s", fc_local);
            }
            snprintf(t.cond_buf, sizeof(t.cond_buf), "%s", cond_str);
            t.wx_ms = esphome::millis();
            t.wx_valid = true;
            t.wx_dirty = true;
            __atomic_clear(&t.lock, __ATOMIC_RELEASE);

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
        __atomic_store_n(&t.sync_busy, false, __ATOMIC_RELEASE);
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      // 2. Dual-Provider Live ISS Poll (Primary: WhereTheISS.at HTTPS -> Failover: Open-Notify HTTP)
      uint8_t cur_slp = __atomic_load_n(&t.sleep_phase, __ATOMIC_RELAXED);
      uint8_t cur_occ = __atomic_load_n(&t.active_occasion, __ATOMIC_RELAXED);
      bool need_iss = (cur_slp == 0 && cur_occ == 1) || !__atomic_load_n(&t.iss_valid, __ATOMIC_RELAXED);
      now_ms = esphome::millis();
      if (!need_iss) {
        next_iss_ms = now_ms;  // Prevent 24.8-day rollover across Advent / holidays
      } else if ((int32_t)(now_ms - next_iss_ms) >= 0) {
        __atomic_store_n(&t.sync_busy, true, __ATOMIC_RELEASE);
        float ilat = 0.0f, ilon = 0.0f;
        bool eclipsed = false, ok_iss = false;

        if (fetch_url(kIssPrimary, rx, kRxCap, 5000, true) && extract_json_num(rx, "\"latitude\"", ilat) &&
            extract_json_num(rx, "\"longitude\"", ilon) && ilat >= -90.0f && ilat <= 90.0f && ilon >= -180.0f &&
            ilon <= 180.0f) {
          eclipsed = (strstr(rx, "\"eclipsed\"") != nullptr);
          ok_iss = true;
        } else {
          vTaskDelay(pdMS_TO_TICKS(100));
          if (fetch_url(kIssFallback, rx, kRxCap, 5000, false) && extract_json_num(rx, "\"latitude\"", ilat) &&
              extract_json_num(rx, "\"longitude\"", ilon) && ilat >= -90.0f && ilat <= 90.0f && ilon >= -180.0f &&
              ilon <= 180.0f) {
            eclipsed = false;
            ok_iss = true;
          }
        }

        if (ok_iss) {
          while (__atomic_test_and_set(&t.lock, __ATOMIC_ACQUIRE)) vTaskDelay(1);
          t.iss_lat = ilat;
          t.iss_lon = ilon;
          t.iss_eclipsed = eclipsed;
          t.iss_ms = esphome::millis();
          t.iss_valid = true;
          __atomic_clear(&t.lock, __ATOMIC_RELEASE);

          iss_backoff_ms = 15000UL;
          next_iss_ms = esphome::millis() + 45000UL;
        } else {
          next_iss_ms = esphome::millis() + iss_backoff_ms;
          iss_backoff_ms = std::min(iss_backoff_ms * 2UL, 180000UL);
        }
        __atomic_store_n(&t.sync_busy, false, __ATOMIC_RELEASE);
      }
    }
  }
};

}  // namespace world_clock

#endif  // USE_ESP32 && USE_HTTP_REQUEST
