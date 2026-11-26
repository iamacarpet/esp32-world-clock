#pragma once

#include "clock_math.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace world_clock {

// Checks whether a parsed IPv4 address (host byte order or 4 octets) is private, loopback,
// link-local, CGNAT, multicast, or unspecified (SSRF protection).
inline bool is_private_ipv4_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  (void)c;
  (void)d;
  if (a == 0 || a == 10 || a == 127) return true;                          // 0/8, 10/8, 127/8
  if (a == 100 && (b >= 64 && b <= 127)) return true;                      // 100.64.0.0/10 CGNAT
  if (a == 169 && b == 254) return true;                                   // 169.254.0.0/16 Link-local
  if (a == 172 && (b >= 16 && b <= 31)) return true;                       // 172.16.0.0/12 RFC1918
  if (a == 192 && b == 168) return true;                                   // 192.168.0.0/16 RFC1918
  if (a == 192 && b == 0 && (c == 0 || c == 2)) return true;               // 192.0.0/24, 192.0.2/24
  if (a == 198 && (b == 18 || b == 19)) return true;                       // 198.18.0.0/15 benchmark
  if (a >= 224) return true;                                               // Multicast & reserved
  return false;
}

// Extracts the lowercase hostname from an https:// URL into `host_out`, rejecting userinfo ('@'),
// control chars, and malformed authorities.
inline bool extract_https_host(const char *url, char *host_out, size_t host_cap) {
  if (url == nullptr || host_out == nullptr || host_cap < 4) return false;
  host_out[0] = '\0';
  if (strncasecmp(url, "https://", 8) != 0) return false;
  const char *p = url + 8;
  const char *auth_end = p;
  while (*auth_end != '\0' && *auth_end != '/' && *auth_end != '?' && *auth_end != '#') {
    if ((unsigned char)*auth_end <= 32 || *auth_end == '@' || *auth_end == '\\') return false;
    auth_end++;
  }
  if (auth_end == p) return false;

  const char *h_start = p;
  const char *h_end = auth_end;
  if (*h_start == '[') {
    // IPv6 literal [::1]
    const char *rb = (const char *)memchr(h_start, ']', (size_t)(auth_end - h_start));
    if (rb == nullptr || rb == h_start + 1) return false;
    h_start++;
    h_end = rb;
  } else {
    const char *colon = (const char *)memchr(h_start, ':', (size_t)(auth_end - h_start));
    if (colon != nullptr) h_end = colon;
  }
  size_t hlen = (size_t)(h_end - h_start);
  if (hlen == 0 || hlen + 1 >= host_cap) return false;
  for (size_t i = 0; i < hlen; i++) {
    host_out[i] = (char)tolower((unsigned char)h_start[i]);
  }
  host_out[hlen] = '\0';
  return true;
}

// Validates that `url` is a clean public HTTPS URL (blocks http://, localhost, .local, LAN/RFC1918/IPv6-ULA literals).
inline bool is_public_https_url(const char *url) {
  if (url == nullptr) return false;
  for (const char *p = url; *p != '\0'; p++) {
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return false;
  }
  char host[128];
  if (!extract_https_host(url, host, sizeof(host))) return false;
  size_t hlen = strlen(host);
  if (hlen == 0) return false;
  while (hlen > 0 && host[hlen - 1] == '.') host[--hlen] = '\0';
  if (hlen == 0) return false;

  if (strcmp(host, "localhost") == 0) return false;
  if (hlen > 10 && strcmp(host + hlen - 10, ".localhost") == 0) return false;
  if (hlen > 6 && strcmp(host + hlen - 6, ".local") == 0) return false;
  if (hlen > 5 && strcmp(host + hlen - 5, ".home") == 0) return false;
  if (hlen > 4 && strcmp(host + hlen - 4, ".lan") == 0) return false;
  if (hlen > 9 && strcmp(host + hlen - 9, ".internal") == 0) return false;

  // Reject IPv6 literals pointing to loopback/ULA/link-local/unspecified/IPv4-mapped
  if (strchr(host, ':') != nullptr) {
    if (strcmp(host, "::1") == 0 || strcmp(host, "::") == 0) return false;
    if (strncmp(host, "fc", 2) == 0 || strncmp(host, "fd", 2) == 0) return false;
    if (strncmp(host, "fe8", 3) == 0 || strncmp(host, "fe9", 3) == 0 || strncmp(host, "fea", 3) == 0 ||
        strncmp(host, "feb", 3) == 0)
      return false;
    if (strncmp(host, "ff", 2) == 0 || strncmp(host, "::ffff:", 7) == 0) return false;
    return true;
  }

  // Reject dotted-quad private/loopback/link-local/CGNAT IPv4 literals
  unsigned a = 0, b = 0, c = 0, d = 0;
  char extra = 0;
  if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4 && a <= 255 && b <= 255 && c <= 255 && d <= 255) {
    return !is_private_ipv4_octets((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
  }
  // Reject bare numeric hosts (decimal/octal/hex IPv4 obfuscation like 2130706433 or 0x7f000001)
  if (strchr(host, '.') == nullptr) return false;
  return true;
}

// Normalizes iCloud / Apple / Google / School calendar URLs:
// Trims whitespace, rewrites webcal:// or webcals:// -> https://, and rejects plaintext http:// & private LAN hosts!
inline bool normalize_calendar_url(const char *raw, char *out, size_t cap) {
  if (raw == nullptr || out == nullptr || cap < 16) return false;
  out[0] = '\0';
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
  size_t len = strlen(raw);
  while (len > 0 && (raw[len - 1] == ' ' || raw[len - 1] == '\t' || raw[len - 1] == '\r' || raw[len - 1] == '\n')) {
    len--;
  }
  if (len < 10) return false;

  int w = -1;
  if (len > 9 && strncasecmp(raw, "webcal://", 9) == 0) {
    w = snprintf(out, cap, "https://%.*s", (int)(len - 9), raw + 9);
  } else if (len > 10 && strncasecmp(raw, "webcals://", 10) == 0) {
    w = snprintf(out, cap, "https://%.*s", (int)(len - 10), raw + 10);
  } else if (strncasecmp(raw, "https://", 8) == 0) {
    w = snprintf(out, cap, "%.*s", (int)len, raw);
  } else {
    return false;  // Reject plaintext http:// and unknown schemes!
  }
  if (w <= 0 || (size_t)w >= cap) {
    out[0] = '\0';
    return false;
  }
  if (!is_public_https_url(out)) {
    out[0] = '\0';
    return false;
  }
  return true;
}

struct TimedEvent {
  bool valid{false};
  int minute_of_day{0};  // 0..1439
  char title[44]{""};
};

struct CountdownItem {
  bool valid{false};
  bool priority{false};
  int days_away{-1};
  char title[40]{""};
};

struct IcsFeedSummary {
  bool valid{false};
  int events_seen{0};
  // Up to 2 sorted timed events today + 1 all-day event today + legacy primary today_title
  TimedEvent today_timed[2]{};
  char today_allday[44]{""};
  char today_title[48]{""};
  char tomorrow_title[48]{""};
  // Up to 2 concurrent upcoming countdowns (priority "Countdown:" tags first, then nearest!)
  CountdownItem countdowns[2]{};
  char next_title[48]{""};
  int next_days_away{-1};
  bool next_is_priority_countdown{false};
  // School-calendar awareness: today/tomorrow holiday flags + auto Holiday & Back-to-School countdowns!
  bool today_is_school_holiday{false};
  bool tomorrow_is_school_holiday{false};
  char school_note[48]{""};
  int next_school_holiday_days{-1};
  char next_school_holiday_name[36]{""};
  int back_to_school_days{-1};
  uint64_t hol_mask{0};  // 64-day bitmask (d=0..62) so Family [holiday] + School INSET merge across feeds!
  int cur_dow_mon0{0};   // 0=Mon..6=Sun for cur_jdn_ (used when recomputing merged back-to-school)
};

// Priority mode for resolving multiple concurrent calendar events:
// 0 = Smart Auto (imminent timed -> today all-day -> priority countdown -> tomorrow -> auto school)
// 1 = Today & Routine First (focuses on today's schedule & school status first)
// 2 = Countdowns First (highlights upcoming holidays/trips alongside imminent timed events)
struct CalendarView {
  std::string headline;        // Best single-line ticker message for the current slot
  std::string dock_title;      // Prominent Band 3 dock card title
  std::string dock_sub;        // Prominent Band 3 dock card subtitle
  std::string plan_lines[3];   // Up to 3 crisp bullets for Morning Launchpad
  std::string school_badge;    // "SCHOOL DAY", "NO SCHOOL · HALF TERM", "WEEKEND", etc.
  std::string holiday_badge;   // "8 SLEEPS TO HALF TERM" or "3 SLEEPS TO BACK TO SCHOOL"
};

inline std::string weather_prep_hint(const std::string &cond, float temp_c, bool use_f) {
  std::string c = cond;
  for (char &ch : c) ch = (char)tolower((unsigned char)ch);
  bool rain = (c.find("rain") != std::string::npos || c.find("pour") != std::string::npos ||
               c.find("lightning") != std::string::npos || c.find("storm") != std::string::npos);
  bool snow = (c.find("snow") != std::string::npos || c.find("sleet") != std::string::npos);
  bool sun = (c.find("sunny") != std::string::npos);
  (void)use_f;
  if (snow) return "Warm Coat, Hat & Boots!";
  if (rain && !std::isnan(temp_c) && temp_c < 11.0f) return "Warm Raincoat & Umbrella";
  if (rain) return "Pack Raincoat / Umbrella";
  if (!std::isnan(temp_c) && temp_c < 7.0f) return "Big Coat, Gloves & Scarf";
  if (!std::isnan(temp_c) && temp_c < 14.0f) return "Jumper or Light Jacket";
  if (sun && !std::isnan(temp_c) && temp_c >= 21.0f) return "Sun Hat, Suncream & Water";
  if (!std::isnan(temp_c) && temp_c >= 19.0f) return "T-Shirt Weather + Water";
  return "Layers · Check Outside";
}

// Whole-day school-run briefing for the Morning Launchpad ("PACK & WEAR FOR TODAY" card).
// Focuses on what changes across the whole day (rain later when dry now, morning->afternoon swings,
// all-day cold/heat) rather than duplicating Band 3's current-moment temperature readout.
struct DayPrepBriefing {
  std::string headline;
  std::string pack_hint;
};

inline DayPrepBriefing build_day_prep_briefing(const std::string &cur_cond, const std::string &today_cond,
                                               float cur_c, float high_c, float low_c, int precip_pct, bool use_f) {
  auto lower = [](std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
  };
  auto is_wet = [](const std::string &s) {
    return s.find("rain") != std::string::npos || s.find("pour") != std::string::npos ||
           s.find("lightning") != std::string::npos || s.find("storm") != std::string::npos;
  };
  auto is_snow = [](const std::string &s) {
    return s.find("snow") != std::string::npos || s.find("sleet") != std::string::npos ||
           s.find("hail") != std::string::npos;
  };

  if (std::isnan(cur_c) && std::isnan(high_c)) {
    return {"Day Forecast Syncing...", "Check outside before leaving"};
  }

  std::string cc = lower(cur_cond);
  std::string dc = lower(today_cond);
  bool cur_w = is_wet(cc);
  bool cur_s = is_snow(cc);
  bool day_s = is_snow(dc);
  bool day_w = is_wet(dc) || (precip_pct >= 50);

  float hi_c = !std::isnan(high_c) ? high_c : cur_c;
  float lo_c = !std::isnan(low_c) ? low_c : cur_c;
  float disp_hi = use_f ? c_to_f(hi_c) : hi_c;
  if (fabsf(disp_hi) < 0.5f) disp_hi = 0.0f;
  char unit = use_f ? 'F' : 'C';
  bool big_swing = !std::isnan(cur_c) && !std::isnan(high_c) && ((high_c - cur_c) >= 7.0f);

  char hdr[48];
  if (cur_s || day_s) {
    if (!cur_s && day_s) {
      snprintf(hdr, sizeof(hdr), "Dry Now · Snow Later (%.0f°%c)", disp_hi, unit);
    } else {
      snprintf(hdr, sizeof(hdr), "Snow & Ice Today (High %.0f°%c)", disp_hi, unit);
    }
    return {hdr, "Warm Coat, Hat, Gloves & Boots"};
  }

  if (!cur_w && day_w) {
    if (precip_pct >= 50) {
      snprintf(hdr, sizeof(hdr), "Dry Now · Rain Later (%d%%)!", std::min(precip_pct, 100));
    } else {
      snprintf(hdr, sizeof(hdr), "Dry Now · Rain Expected Later!");
    }
    if (hi_c < 13.0f) return {hdr, "Pack Warm Raincoat & Umbrella"};
    if (big_swing && hi_c >= 19.0f) return {hdr, "Layers + Light Raincoat!"};
    return {hdr, "Pack Raincoat for Home Time!"};
  }

  if (cur_w) {
    if (precip_pct >= 50) {
      snprintf(hdr, sizeof(hdr), "Wet Today (High %.0f°%c · %d%%)", disp_hi, unit, std::min(precip_pct, 100));
    } else {
      snprintf(hdr, sizeof(hdr), "Wet & Showery (High %.0f°%c)", disp_hi, unit);
    }
    return {hdr, (hi_c < 13.0f) ? "Warm Raincoat & Umbrella" : "Light Raincoat & Umbrella"};
  }

  if (big_swing && hi_c >= 18.0f) {
    if (hi_c >= 24.0f) {
      snprintf(hdr, sizeof(hdr), "Cool AM \u2192 Hot PM (\u2191%.0f°%c)", disp_hi, unit);
      return {hdr, "Layers + Sun Hat & Water"};
    }
    snprintf(hdr, sizeof(hdr), "Cool AM \u2192 Warm PM (\u2191%.0f°%c)", disp_hi, unit);
    return {hdr, "Layers: Jumper Now, T-Shirt Later"};
  }

  if (hi_c >= 22.0f) {
    snprintf(hdr, sizeof(hdr), "Warm & Bright Later (\u2191%.0f°%c)", disp_hi, unit);
    return {hdr, "Sun Hat, Suncream & Water"};
  }
  if (hi_c < 8.5f || (lo_c < 2.0f && hi_c < 11.0f)) {
    snprintf(hdr, sizeof(hdr), "Cold All Day (High %.0f°%c)", disp_hi, unit);
    return {hdr, "Big Coat, Hat & Gloves"};
  }
  if (hi_c < 15.0f) {
    snprintf(hdr, sizeof(hdr), "Cool Day Ahead (High %.0f°%c)", disp_hi, unit);
    return {hdr, "Jumper or Light Jacket"};
  }
  if (hi_c >= 19.0f) {
    snprintf(hdr, sizeof(hdr), "Warm Afternoon (High %.0f°%c)", disp_hi, unit);
    return {hdr, "T-Shirt Weather + Water"};
  }
  snprintf(hdr, sizeof(hdr), "Mild Day Ahead (High %.0f°%c)", disp_hi, unit);
  return {hdr, "Light Layers · No Rain Due"};
}

// Parses Home Assistant's 3-day forecast sensor payload.
// Supports both legacy 3-day strings ("FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7")
// and v1.3.1+ payloads with an optional leading today-prep segment:
// "TODAY,rainy,80|FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7"
// so HA clocks unlock "Dry Now · Rain Later (80%)!" with zero breaking changes.
inline void parse_ha_forecast_payload(const std::string &raw, std::string &out_3day,
                                      std::string &out_today_cond, int &out_precip_pct) {
  if (raw.size() >= 6 && strncasecmp(raw.c_str(), "TODAY,", 6) == 0) {
    size_t pipe = raw.find('|');
    std::string seg = (pipe == std::string::npos) ? raw.substr(6) : raw.substr(6, pipe - 6);
    size_t comma = seg.find(',');
    std::string cond = (comma == std::string::npos) ? seg : seg.substr(0, comma);
    // Trim whitespace around condition
    size_t s = 0;
    while (s < cond.size() && cond[s] == ' ') s++;
    size_t e = cond.size();
    while (e > s && cond[e - 1] == ' ') e--;
    out_today_cond = cond.substr(s, e - s);

    int pct = -1;
    if (comma != std::string::npos) {
      const char *p = seg.c_str() + comma + 1;
      while (*p == ' ') p++;
      if (isdigit((unsigned char)*p)) {
        pct = std::clamp(atoi(p), 0, 100);
      }
    }
    out_precip_pct = pct;
    out_3day = (pipe == std::string::npos) ? "" : raw.substr(pipe + 1);
    return;
  }
  out_3day = raw;
}

// Evaluates Family + School calendars + Custom Countdown into unified display strings
// across all 3 priority modes (0=Smart Auto, 1=Today & Routine First, 2=Countdowns First).
inline CalendarView build_calendar_view(const IcsFeedSummary &fam, const IcsFeedSummary &sch,
                                        bool school_configured, bool is_weekend, int now_min,
                                        int priority_mode, int rotation_slot, bool use_24h,
                                        const std::string &custom_cd_name, int custom_cd_days) {
  CalendarView v;

  bool is_hol = sch.today_is_school_holiday || fam.today_is_school_holiday;
  std::string hol_note = sch.school_note[0] ? sch.school_note : fam.school_note;
  if (is_hol) {
    v.school_badge = hol_note.empty() ? "NO SCHOOL · HOLIDAY" : ("NO SCHOOL · " + to_upper_ascii(hol_note));
  } else if (is_weekend) {
    v.school_badge = "WEEKEND · NO SCHOOL";
  } else if (school_configured) {
    v.school_badge = "SCHOOL DAY";
  }

  // Auto school holiday / back-to-school badge (merges Family [holiday] + School INSET bitmasks!)
  uint64_t merged_hol_mask = fam.hol_mask | sch.hol_mask;
  int merged_bts = sch.back_to_school_days;
  if (is_hol && merged_hol_mask != 0) {
    int base_dow = (sch.hol_mask != 0) ? sch.cur_dow_mon0 : fam.cur_dow_mon0;
    for (int d = 1; d <= 62; d++) {
      int jdow = (base_dow + d) % 7;
      bool is_wk = (jdow == 5 || jdow == 6);
      if (!is_wk && ((merged_hol_mask & (1ULL << d)) == 0)) {
        merged_bts = d;
        break;
      }
    }
  } else if (is_hol && merged_bts <= 0 && fam.back_to_school_days > 0) {
    merged_bts = fam.back_to_school_days;
  }

  int next_hol_days = -1;
  const char *next_hol_name = "HOLIDAY";
  if (sch.next_school_holiday_days > 0 && sch.next_school_holiday_days <= 60) {
    next_hol_days = sch.next_school_holiday_days;
    if (sch.next_school_holiday_name[0]) next_hol_name = sch.next_school_holiday_name;
  }
  if (fam.next_school_holiday_days > 0 && fam.next_school_holiday_days <= 60 &&
      (next_hol_days < 0 || fam.next_school_holiday_days < next_hol_days)) {
    next_hol_days = fam.next_school_holiday_days;
    if (fam.next_school_holiday_name[0]) next_hol_name = fam.next_school_holiday_name;
  }

  if (is_hol && merged_bts > 0) {
    char b[48];
    snprintf(b, sizeof(b), "%d %s TO BACK TO SCHOOL", merged_bts, (merged_bts == 1) ? "SLEEP" : "SLEEPS");
    v.holiday_badge = b;
  } else if (!is_hol && next_hol_days > 0) {
    char b[56];
    snprintf(b, sizeof(b), "%d %s TO %s!", next_hol_days, (next_hol_days == 1) ? "SLEEP" : "SLEEPS",
             to_upper_ascii(next_hol_name).c_str());
    v.holiday_badge = b;
  }

  // Merge up to 4 timed events today across BOTH Family & School calendars (sorted by start minute, Family wins ties)
  TimedEvent merged_timed[4]{};
  int mt_n = 0;
  auto push_timed = [&](const TimedEvent &ev) {
    if (!ev.valid || mt_n >= 4) return;
    int pos = mt_n;
    while (pos > 0 && ev.minute_of_day < merged_timed[pos - 1].minute_of_day) {
      merged_timed[pos] = merged_timed[pos - 1];
      pos--;
    }
    merged_timed[pos] = ev;
    mt_n++;
  };
  for (int i = 0; i < 2; i++) push_timed(fam.today_timed[i]);
  for (int i = 0; i < 2; i++) push_timed(sch.today_timed[i]);

  // Pick next upcoming timed event today (automatically expires 30m after start time!)
  std::string active_timed_str;
  std::string second_timed_str;
  bool imminent_timed = false;
  for (int i = 0; i < mt_n; i++) {
    int diff = merged_timed[i].minute_of_day - now_min;
    if (diff < -30) continue;  // Already finished/passed
    std::string nm = to_upper_ascii(merged_timed[i].title);
    char tb[64];
    if (active_timed_str.empty()) {
      if (diff >= 0 && diff <= 90) {
        imminent_timed = true;
        if (diff <= 5) snprintf(tb, sizeof(tb), "%s NOW!", nm.c_str());
        else snprintf(tb, sizeof(tb), "%s IN %d MIN!", nm.c_str(), diff);
      } else {
        std::string ts = format_short_time(merged_timed[i].minute_of_day / 60,
                                           merged_timed[i].minute_of_day % 60, use_24h);
        snprintf(tb, sizeof(tb), "%s (%s)", nm.c_str(), ts.c_str());
      }
      active_timed_str = tb;
    } else if (second_timed_str.empty()) {
      std::string ts = format_short_time(merged_timed[i].minute_of_day / 60,
                                         merged_timed[i].minute_of_day % 60, use_24h);
      snprintf(tb, sizeof(tb), "%s (%s)", nm.c_str(), ts.c_str());
      second_timed_str = tb;
    }
  }

  // Merge all-day events across Family + non-holiday School events (e.g. Christmas Dinner Day, School Photos!)
  std::string fam_allday = fam.today_allday[0] ? to_upper_ascii(fam.today_allday) : "";
  std::string sch_allday;
  if (sch.today_allday[0]) {
    std::string cand = to_upper_ascii(sch.today_allday);
    // Avoid duplicating the school holiday name already shown in the top NO SCHOOL badge
    if (!is_hol || cand != to_upper_ascii(hol_note)) {
      sch_allday = cand;
    }
  }
  std::string allday_str = !fam_allday.empty() ? fam_allday : sch_allday;
  std::string extra_allday_str = (!fam_allday.empty() && !sch_allday.empty() && sch_allday != fam_allday) ? sch_allday : "";

  const char *tmrw_raw = fam.tomorrow_title[0] ? fam.tomorrow_title : sch.tomorrow_title;
  std::string tmrw_str = tmrw_raw[0] ? ("TOMORROW \u2192 " + to_upper_ascii(tmrw_raw)) : "";

  // Collect up to 3 concurrent countdowns (custom UI + Family/School ICS countdowns + merged school holiday badge)
  std::string cd_pool[3];
  int cd_n = 0;
  if (!custom_cd_name.empty() && custom_cd_days >= 0 && custom_cd_days <= 90) {
    char cb[56];
    if (custom_cd_days == 0) snprintf(cb, sizeof(cb), "TODAY IS %s!", to_upper_ascii(custom_cd_name).c_str());
    else snprintf(cb, sizeof(cb), "%d %s TO %s!", custom_cd_days, (custom_cd_days == 1) ? "SLEEP" : "SLEEPS",
                  to_upper_ascii(custom_cd_name).c_str());
    cd_pool[cd_n++] = cb;
  }
  for (int i = 0; i < 2 && cd_n < 3; i++) {
    if (fam.countdowns[i].valid && fam.countdowns[i].days_away >= 1) {
      char cb[56];
      snprintf(cb, sizeof(cb), "%d %s TO %s!", fam.countdowns[i].days_away,
               (fam.countdowns[i].days_away == 1) ? "SLEEP" : "SLEEPS",
               to_upper_ascii(fam.countdowns[i].title).c_str());
      cd_pool[cd_n++] = cb;
    }
  }
  for (int i = 0; i < 2 && cd_n < 3; i++) {
    if (sch.countdowns[i].valid && sch.countdowns[i].priority && sch.countdowns[i].days_away >= 1) {
      char cb[56];
      snprintf(cb, sizeof(cb), "%d %s TO %s!", sch.countdowns[i].days_away,
               (sch.countdowns[i].days_away == 1) ? "SLEEP" : "SLEEPS",
               to_upper_ascii(sch.countdowns[i].title).c_str());
      cd_pool[cd_n++] = cb;
    }
  }
  if (cd_n < 3 && !v.holiday_badge.empty()) {
    cd_pool[cd_n++] = v.holiday_badge;
  }
  std::string active_cd = (cd_n > 0) ? cd_pool[rotation_slot % cd_n] : "";

  // Populate Morning Launchpad plan bullets (up to 3 crisp items across Family + School!)
  int pl = 0;
  if (!active_timed_str.empty() && pl < 3) v.plan_lines[pl++] = active_timed_str;
  if (!allday_str.empty() && pl < 3) v.plan_lines[pl++] = allday_str;
  if (!extra_allday_str.empty() && pl < 3) v.plan_lines[pl++] = extra_allday_str;
  if (!second_timed_str.empty() && second_timed_str != active_timed_str && pl < 3) v.plan_lines[pl++] = second_timed_str;
  if (!tmrw_str.empty() && pl < 3) v.plan_lines[pl++] = tmrw_str;
  if (!active_cd.empty() && pl < 3) v.plan_lines[pl++] = active_cd;
  if (pl == 0) v.plan_lines[0] = is_hol || is_weekend ? "FREE DAY · NO EVENTS SCHEDULED" : "REGULAR SCHOOL DAY";

  // If no school holiday badge is active, promote the custom UI / priority countdown to the Launchpad pill!
  if (v.holiday_badge.empty() && !active_cd.empty()) {
    v.holiday_badge = active_cd;
  }

  // Build Prominent Dock Card title + subtitle
  if (!active_timed_str.empty()) {
    v.dock_title = v.school_badge.empty() ? "TODAY'S PLAN" : v.school_badge;
    v.dock_sub = active_timed_str;
  } else if (!allday_str.empty()) {
    v.dock_title = v.school_badge.empty() ? "TODAY'S PLAN" : v.school_badge;
    v.dock_sub = allday_str;
  } else if (!active_cd.empty()) {
    v.dock_title = v.school_badge.empty() ? "UPCOMING COUNTDOWN" : v.school_badge;
    v.dock_sub = active_cd;
  } else if (!v.school_badge.empty()) {
    v.dock_title = v.school_badge;
    v.dock_sub = !tmrw_str.empty() ? tmrw_str : "Have a wonderful day!";
  }

  // Build Headline according to Priority Mode
  std::string today_combined;
  if (!active_timed_str.empty()) {
    today_combined = v.school_badge.empty() ? ("TODAY \u2192 " + active_timed_str)
                                            : (v.school_badge + " · " + active_timed_str);
  } else if (!allday_str.empty()) {
    today_combined = v.school_badge.empty() ? ("TODAY \u2192 " + allday_str)
                                            : (v.school_badge + " · " + allday_str);
  } else if (!v.school_badge.empty()) {
    today_combined = v.school_badge;
  }

  if (imminent_timed) {
    // Imminent event (< 90m) ALWAYS wins in every priority mode!
    v.headline = active_timed_str;
  } else if (priority_mode == 2) {
    // Countdowns First
    if (!active_cd.empty() && (rotation_slot % 3 != 2 || today_combined.empty())) {
      v.headline = active_cd;
    } else if (!today_combined.empty()) {
      v.headline = today_combined;
    } else if (!tmrw_str.empty()) {
      v.headline = tmrw_str;
    }
  } else if (priority_mode == 1) {
    // Today & Routine First
    if (!today_combined.empty() && (rotation_slot % 3 != 2 || active_cd.empty())) {
      v.headline = today_combined;
    } else if (now_min >= 15 * 60 && !tmrw_str.empty()) {
      v.headline = tmrw_str;
    } else if (!active_cd.empty()) {
      v.headline = active_cd;
    }
  } else {
    // Smart Auto (balanced rotation between today's plan, afternoon tomorrow preview & countdowns)
    if (now_min >= 15 * 60 && !tmrw_str.empty() && (rotation_slot % 3 == 1)) {
      v.headline = tmrw_str;
    } else if (!active_cd.empty() && (rotation_slot % 3 == 2 || today_combined.empty())) {
      v.headline = active_cd;
    } else if (!today_combined.empty()) {
      v.headline = today_combined;
    } else if (!tmrw_str.empty()) {
      v.headline = tmrw_str;
    } else if (!active_cd.empty()) {
      v.headline = active_cd;
    }
  }
  return v;
}

// Zero-allocation streaming RFC 5545 (.ics) parser designed for Core 0 HTTP callbacks.
// Handles multi-megabyte iCloud / Google / School feeds using < 2 KB of stack/member memory!
class IcsStreamParser {
 public:
  IcsStreamParser(int cur_y, int cur_m, int cur_d, bool is_school_feed = false, int local_utc_offset_min = 0)
      : cur_y_(cur_y),
        cur_m_(cur_m),
        cur_d_(cur_d),
        is_school_(is_school_feed),
        local_utc_offset_min_(local_utc_offset_min) {
    cur_jdn_ = ymd_to_jdn(cur_y, cur_m, cur_d);
    summary_.cur_dow_mon0 = (int)(((cur_jdn_ % 7) + 7) % 7);
  }

  // Resets all streaming & accumulated summary state cleanly (e.g. before retrying after an HTTP redirect).
  void reset() {
    saw_vcalendar_ = false;
    in_vevent_ = false;
    has_pending_ = false;
    raw_len_ = 0;
    raw_line_[0] = '\0';
    pending_line_[0] = '\0';
    hol_mask_ = 0;
    tomorrow_prio_ = -1;
    summary_ = IcsFeedSummary{};
    summary_.cur_dow_mon0 = (int)(((cur_jdn_ % 7) + 7) % 7);
    reset_event_();
  }

  void feed(const char *data, size_t len) {
    if (data == nullptr || len == 0) return;
    for (size_t i = 0; i < len; i++) {
      char c = data[i];
      if (c == '\r') continue;
      if (c == '\n') {
        handle_raw_line_();
        raw_len_ = 0;
        raw_line_[0] = '\0';
      } else if (raw_len_ + 1 < sizeof(raw_line_)) {
        raw_line_[raw_len_++] = c;
        raw_line_[raw_len_] = '\0';
      }
    }
  }

  void finish() {
    if (raw_len_ > 0) {
      handle_raw_line_();
      raw_len_ = 0;
    }
    if (has_pending_) {
      process_unfolded_line_(pending_line_);
      has_pending_ = false;
    }
    if (saw_vcalendar_) summary_.valid = true;

    // Derive continuous school-holiday status & merged Back-to-School countdown from 64-day bitmask
    // (accurately merges adjacent Mon-Fri Half Term + Monday INSET Day regardless of VEVENT order!)
    summary_.hol_mask = hol_mask_;
    summary_.cur_dow_mon0 = (int)(((cur_jdn_ % 7) + 7) % 7);
    if ((hol_mask_ & 1ULL) != 0) summary_.today_is_school_holiday = true;
    if ((hol_mask_ & 2ULL) != 0) summary_.tomorrow_is_school_holiday = true;
    if (summary_.today_is_school_holiday) {
      for (int d = 1; d <= 62; d++) {
        int64_t day_jdn = cur_jdn_ + d;
        // JDN % 7: 0=Mon, 1=Tue, 2=Wed, 3=Thu, 4=Fri, 5=Sat, 6=Sun
        int jdow = (int)(((day_jdn % 7) + 7) % 7);
        bool is_wknd = (jdow == 5 || jdow == 6);
        bool is_hol_d = ((hol_mask_ & (1ULL << d)) != 0);
        if (!is_wknd && !is_hol_d) {
          summary_.back_to_school_days = d;
          break;
        }
      }
    }

    // Sync legacy convenience fields from rich arrays
    if (summary_.countdowns[0].valid) {
      summary_.next_days_away = summary_.countdowns[0].days_away;
      summary_.next_is_priority_countdown = summary_.countdowns[0].priority;
      snprintf(summary_.next_title, sizeof(summary_.next_title), "%s", summary_.countdowns[0].title);
    }
  }

  const IcsFeedSummary &summary() const { return summary_; }

 private:
  static bool icontains(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
      size_t k = 0;
      while (k < nlen && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)needle[k])) k++;
      if (k == nlen) return true;
    }
    return false;
  }

  // Quote-aware RFC 5545 content-line splitter: skips ':' inside double-quoted parameters
  // (e.g., SUMMARY;ALTREP="https://school.org/info":Swimming Lesson).
  static const char *find_ics_value_colon(const char *line) {
    if (line == nullptr) return nullptr;
    bool in_quote = false;
    for (const char *p = line; *p != '\0'; p++) {
      if (*p == '"') {
        in_quote = !in_quote;
      } else if (*p == ':' && !in_quote) {
        return p;
      }
    }
    return nullptr;
  }

  // Phrase-aware school-holiday classifier (prevents false positives like "After-School Club Booking Closed").
  static bool is_holiday_keyword(const char *s) {
    if (s == nullptr || s[0] == '\0') return false;
    // Explicit high-confidence school-closed phrases always match
    if (icontains(s, "school closed") || icontains(s, "academy closed") || icontains(s, "nursery closed") ||
        icontains(s, "college closed") || icontains(s, "half term") || icontains(s, "half-term") ||
        icontains(s, "inset") || icontains(s, "no school") || icontains(s, "training day") ||
        icontains(s, "teacher day") || icontains(s, "staff day") || icontains(s, "bank hol") ||
        icontains(s, "easter break") || icontains(s, "summer break") || icontains(s, "christmas break") ||
        icontains(s, "winter break") || icontains(s, "spring break") || icontains(s, "term break") ||
        icontains(s, "school holiday")) {
      return true;
    }
    // Reject admin/booking/club notices containing "holiday" or "closed"
    if (icontains(s, "club") || icontains(s, "camp") || icontains(s, "booking") || icontains(s, "deposit") ||
        icontains(s, "photo") || icontains(s, "homework") || icontains(s, "deadline") || icontains(s, "forms") ||
        icontains(s, "sale") || icontains(s, "application")) {
      return false;
    }
    return icontains(s, "holiday");
  }

  static bool parse_ics_date(const char *val, int &y, int &m, int &d, bool &has_time, int &hr, int &mn, bool &is_utc) {
    if (val == nullptr || strlen(val) < 8) return false;
    for (int i = 0; i < 8; i++) {
      if (!isdigit((unsigned char)val[i])) return false;
    }
    y = (val[0] - '0') * 1000 + (val[1] - '0') * 100 + (val[2] - '0') * 10 + (val[3] - '0');
    m = (val[4] - '0') * 10 + (val[5] - '0');
    d = (val[6] - '0') * 10 + (val[7] - '0');
    if (y < 1970 || y > 2150 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    has_time = false;
    is_utc = false;
    hr = 0;
    mn = 0;
    size_t vlen = strlen(val);
    if (val[8] == 'T' && vlen >= 13 && isdigit((unsigned char)val[9]) && isdigit((unsigned char)val[10]) &&
        isdigit((unsigned char)val[11]) && isdigit((unsigned char)val[12])) {
      hr = (val[9] - '0') * 10 + (val[10] - '0');
      mn = (val[11] - '0') * 10 + (val[12] - '0');
      if (hr >= 0 && hr <= 23 && mn >= 0 && mn <= 59) {
        has_time = true;
        // Check for trailing 'Z' (either YYYYMMDDTHHMMZ or YYYYMMDDTHHMMSSZ)
        for (size_t k = 13; k < vlen && k <= 16; k++) {
          if (val[k] == 'Z' || val[k] == 'z') {
            is_utc = true;
            break;
          }
        }
      }
    }
    return true;
  }

  // Applies local_utc_offset_min_ to a UTC DATE-TIME, rolling JDN across midnight when needed.
  void apply_utc_offset_(int64_t &jdn, int &hr, int &mn) const {
    int total_m = hr * 60 + mn + local_utc_offset_min_;
    while (total_m < 0) {
      total_m += 1440;
      jdn -= 1;
    }
    while (total_m >= 1440) {
      total_m -= 1440;
      jdn += 1;
    }
    hr = total_m / 60;
    mn = total_m % 60;
  }

  static void clean_summary(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    for (size_t i = 0; src[i] != '\0' && w + 1 < cap; i++) {
      if (src[i] == '\\' && src[i + 1] != '\0') {
        char n = src[++i];
        if (n == 'n' || n == 'N') {
          if (w > 0 && dst[w - 1] != ' ') dst[w++] = ' ';
        } else {
          dst[w++] = n;
        }
        continue;
      }
      unsigned char uc = (unsigned char)src[i];
      if (uc >= 32 && uc < 127) {
        dst[w++] = (char)uc;
      } else if (uc == 0xC2 || uc == 0xC3) {
        if (src[i + 1] != '\0' && w + 2 < cap) {
          dst[w++] = src[i++];
          dst[w++] = src[i];
        }
      } else if (uc == 0xE2 && (unsigned char)src[i + 1] == 0x80 && src[i + 2] != '\0') {
        // Transliterate UTF-8 smart quotes & en/em dashes so Fira Code renders them cleanly!
        unsigned char b2 = (unsigned char)src[i + 2];
        if (b2 == 0x98 || b2 == 0x99) {
          dst[w++] = '\'';
        } else if (b2 == 0x9C || b2 == 0x9D) {
          dst[w++] = '"';
        } else if (b2 == 0x93 || b2 == 0x94) {
          dst[w++] = '-';
        } else if (b2 == 0xA6 && w + 3 < cap) {
          dst[w++] = '.';
          dst[w++] = '.';
          dst[w++] = '.';
        }
        i += 2;
      }
    }
    while (w > 0 && dst[w - 1] == ' ') w--;
    dst[w] = '\0';
  }

  static void strip_tag_ci(char *buf, const char *tag) {
    size_t tlen = strlen(tag);
    for (char *p = buf; *p; p++) {
      size_t k = 0;
      while (k < tlen && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)tag[k])) k++;
      if (k == tlen) {
        memmove(p, p + tlen, strlen(p + tlen) + 1);
        break;
      }
    }
    char *s = buf;
    while (*s == ' ') s++;
    if (s != buf) memmove(buf, s, strlen(s) + 1);
    size_t n = strlen(buf);
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = '\0';
  }

  void insert_timed_today_(int min_of_day, const char *title) {
    TimedEvent ev;
    ev.valid = true;
    ev.minute_of_day = min_of_day;
    snprintf(ev.title, sizeof(ev.title), "%.43s", title);
    if (!summary_.today_timed[0].valid || min_of_day < summary_.today_timed[0].minute_of_day) {
      summary_.today_timed[1] = summary_.today_timed[0];
      summary_.today_timed[0] = ev;
    } else if (!summary_.today_timed[1].valid || min_of_day < summary_.today_timed[1].minute_of_day) {
      summary_.today_timed[1] = ev;
    }
  }

  void insert_countdown_(int days_away, bool priority, const char *title) {
    auto is_better = [](const CountdownItem &a, const CountdownItem &b) {
      if (!b.valid) return true;
      if (a.priority != b.priority) return a.priority;
      return a.days_away < b.days_away;
    };
    CountdownItem item;
    item.valid = true;
    item.priority = priority;
    item.days_away = days_away;
    snprintf(item.title, sizeof(item.title), "%.39s", title);
    if (is_better(item, summary_.countdowns[0])) {
      summary_.countdowns[1] = summary_.countdowns[0];
      summary_.countdowns[0] = item;
    } else if (is_better(item, summary_.countdowns[1])) {
      summary_.countdowns[1] = item;
    }
  }

  void handle_raw_line_() {
    if (has_pending_ && (raw_line_[0] == ' ' || raw_line_[0] == '\t')) {
      size_t cur_len = strlen(pending_line_);
      size_t add_len = (raw_len_ > 1) ? (raw_len_ - 1) : 0;
      size_t avail = (cur_len + 1 < sizeof(pending_line_)) ? (sizeof(pending_line_) - 1 - cur_len) : 0;
      size_t n = std::min(add_len, avail);
      if (n > 0) {
        memcpy(pending_line_ + cur_len, raw_line_ + 1, n);
        pending_line_[cur_len + n] = '\0';
      }
      return;
    }
    if (has_pending_) {
      process_unfolded_line_(pending_line_);
    }
    snprintf(pending_line_, sizeof(pending_line_), "%s", raw_line_);
    has_pending_ = true;
  }

  void add_exdate_(int64_t jdn) {
    // Only retain EXDATEs near the active window so historical cancellations don't evict upcoming ones
    if (jdn < cur_jdn_ - 14 || jdn > cur_jdn_ + 400) return;
    for (int i = 0; i < ev_exdate_n_; i++) {
      if (ev_exdates_[i] == jdn) return;
    }
    if (ev_exdate_n_ < kMaxExdates) {
      ev_exdates_[ev_exdate_n_++] = jdn;
    }
  }

  bool is_exdate_(int64_t jdn) const {
    for (int i = 0; i < ev_exdate_n_; i++) {
      if (ev_exdates_[i] == jdn) return true;
    }
    return false;
  }

  void reset_event_() {
    ev_has_start_ = false;
    ev_has_end_ = false;
    ev_has_time_ = false;
    ev_is_utc_ = false;
    ev_end_has_time_ = false;
    ev_end_is_utc_ = false;
    ev_yearly_ = false;
    ev_weekly_ = false;
    ev_interval_ = 1;
    ev_byday_mask_ = 0;
    ev_has_until_ = false;
    ev_cancelled_ = false;
    ev_exdate_n_ = 0;
    ev_sy_ = ev_sm_ = ev_sd_ = 0;
    ev_ey_ = ev_em_ = ev_ed_ = 0;
    ev_hr_ = ev_mn_ = 0;
    ev_ehr_ = ev_emn_ = 0;
    ev_until_jdn_ = 0;
    ev_summary_[0] = '\0';
  }

  void commit_event_() {
    if (!ev_has_start_ || ev_summary_[0] == '\0' || ev_cancelled_) return;
    // Skip events prefixed with "Cancelled" / "Canceled" in summary text (common on school feeds!)
    if (strncasecmp(ev_summary_, "cancelled", 9) == 0 || strncasecmp(ev_summary_, "canceled", 8) == 0) return;
    summary_.events_seen++;

    int64_t orig_start_jdn = ymd_to_jdn(ev_sy_, ev_sm_, ev_sd_);
    int64_t start_jdn = orig_start_jdn;
    int hr = ev_hr_;
    int mn = ev_mn_;
    if (ev_has_time_ && ev_is_utc_) {
      apply_utc_offset_(start_jdn, hr, mn);
    }

    int64_t end_jdn_incl = start_jdn;
    if (ev_has_end_) {
      int64_t ej = ymd_to_jdn(ev_ey_, ev_em_, ev_ed_);
      int ehr = ev_ehr_, emn = ev_emn_;
      if (ev_end_has_time_ && ev_end_is_utc_) {
        apply_utc_offset_(ej, ehr, emn);
      }
      if (!ev_has_time_ && ej > start_jdn) {
        end_jdn_incl = ej - 1;
      } else if (ev_has_time_ && ej > start_jdn && ehr == 0 && emn == 0) {
        // Timed event ending cleanly at 00:00 of next day does not spill into next day
        end_jdn_incl = ej - 1;
      } else if (ej >= start_jdn) {
        end_jdn_incl = ej;
      }
    }

    int64_t span = end_jdn_incl - start_jdn;
    if (ev_yearly_) {
      int ly = ev_sy_, lm = ev_sm_, ld = ev_sd_;
      jdn_to_ymd(start_jdn, ly, lm, ld);
      int64_t cand = ymd_to_jdn(cur_y_, lm, ld);
      if (cand + span < cur_jdn_) cand = ymd_to_jdn(cur_y_ + 1, lm, ld);
      if (cand < start_jdn) cand = start_jdn;
      if (is_exdate_(cand)) {
        cand = ymd_to_jdn(cur_y_ + 1, lm, ld);
        if (cand < start_jdn || is_exdate_(cand)) return;
      }
      if (ev_has_until_ && cand > ev_until_jdn_) return;
      start_jdn = cand;
      end_jdn_incl = cand + span;
    } else if (ev_weekly_) {
      int iv = std::clamp(ev_interval_, 1, 52);
      int orig_dow = (int)(((start_jdn % 7) + 7) % 7);  // 0=Mon..6=Sun
      uint8_t byday = (ev_byday_mask_ != 0) ? ev_byday_mask_ : (uint8_t)(1U << orig_dow);
      int64_t week0_mon = start_jdn - orig_dow;
      int64_t best_cand = -1;
      // Search active occurrences from (cur_jdn_ - span) forward up to iv*7*(exdates+1)+7 days
      int64_t scan_lo = std::max(start_jdn, cur_jdn_ - span);
      int64_t extra_span = std::min<int64_t>(180, (int64_t)iv * 7 * (ev_exdate_n_ + 1) + 7);
      int64_t scan_hi = cur_jdn_ + extra_span;
      for (int64_t cand = scan_lo; cand <= scan_hi; cand++) {
        if (ev_has_until_ && cand > ev_until_jdn_) break;
        int cdow = (int)(((cand % 7) + 7) % 7);
        if ((byday & (1U << cdow)) == 0) continue;
        int64_t wk_idx = (cand - week0_mon) / 7;
        if (wk_idx < 0 || (wk_idx % iv) != 0) continue;
        if (is_exdate_(cand)) continue;
        best_cand = cand;
        break;
      }
      if (best_cand < 0) return;
      start_jdn = best_cand;
      end_jdn_incl = best_cand + span;
    } else if (is_exdate_(start_jdn)) {
      return;
    }

    bool hits_today = (cur_jdn_ >= start_jdn && cur_jdn_ <= end_jdn_incl);
    bool hits_tomorrow = ((cur_jdn_ + 1) >= start_jdn && (cur_jdn_ + 1) <= end_jdn_incl);
    int days_away = (int)(start_jdn - cur_jdn_);

    bool is_tagged_countdown = icontains(ev_summary_, "countdown:") || icontains(ev_summary_, "[countdown]");
    bool is_tagged_noschool = icontains(ev_summary_, "[no school]") || icontains(ev_summary_, "[holiday]");

    char display_sum[48];
    snprintf(display_sum, sizeof(display_sum), "%s", ev_summary_);
    if (is_tagged_countdown) {
      strip_tag_ci(display_sum, "countdown:");
      strip_tag_ci(display_sum, "[countdown]");
    }
    if (is_tagged_noschool) {
      strip_tag_ci(display_sum, "[no school]");
      strip_tag_ci(display_sum, "[holiday]");
    }
    if (display_sum[0] == '\0') snprintf(display_sum, sizeof(display_sum), "%s", ev_summary_);

    // Only treat an event as timed today on its actual start day (prevents multi-day timed events
    // from repeating Day 1's start hour on Day 2/3, while properly preserving 00:00 midnight timed events!)
    bool is_timed_today = hits_today && ev_has_time_ && (cur_jdn_ == start_jdn);

    char formatted[48];
    if (is_timed_today) {
      std::string ts = format_short_time(hr, mn, false);
      snprintf(formatted, sizeof(formatted), "%.34s (%.8s)", display_sum, ts.c_str());
    } else {
      snprintf(formatted, sizeof(formatted), "%.47s", display_sum);
    }

    if (is_school_ || is_tagged_noschool) {
      bool is_hol = is_tagged_noschool || is_holiday_keyword(ev_summary_);
      if (is_hol) {
        // Stamp every overlapping day [0..62] into the 64-day holiday bitmask!
        int64_t lo = std::max(cur_jdn_, start_jdn);
        int64_t hi = std::min(cur_jdn_ + 62, end_jdn_incl);
        for (int64_t j = lo; j <= hi; j++) {
          hol_mask_ |= (1ULL << (j - cur_jdn_));
        }
        if (days_away >= 1 && days_away <= 60) {
          if (summary_.next_school_holiday_days < 0 || days_away < summary_.next_school_holiday_days) {
            summary_.next_school_holiday_days = days_away;
            snprintf(summary_.next_school_holiday_name, sizeof(summary_.next_school_holiday_name), "%.35s",
                     display_sum);
          }
        }
      }
      if (hits_today && (summary_.school_note[0] == '\0' || is_hol)) {
        snprintf(summary_.school_note, sizeof(summary_.school_note), "%s", formatted);
      }
    }

    if (hits_today) {
      if (is_timed_today) {
        insert_timed_today_(hr * 60 + mn, display_sum);
      } else if (summary_.today_allday[0] == '\0') {
        snprintf(summary_.today_allday, sizeof(summary_.today_allday), "%.43s", display_sum);
      }
      if (summary_.today_title[0] == '\0' || is_timed_today) {
        snprintf(summary_.today_title, sizeof(summary_.today_title), "%s", formatted);
      }
    }
    if (hits_tomorrow) {
      int prio = is_tagged_countdown ? 3 : (is_tagged_noschool ? 2 : (ev_has_time_ ? 1 : 0));
      if (summary_.tomorrow_title[0] == '\0' || prio > tomorrow_prio_) {
        tomorrow_prio_ = prio;
        snprintf(summary_.tomorrow_title, sizeof(summary_.tomorrow_title), "%s", display_sum);
      }
    }
    int min_cd = is_tagged_countdown ? 1 : 2;
    int max_horizon = is_tagged_countdown ? 90 : 14;
    if (days_away >= min_cd && days_away <= max_horizon) {
      insert_countdown_(days_away, is_tagged_countdown, display_sum);
    }
  }

  void process_unfolded_line_(const char *line) {
    if (strncmp(line, "BEGIN:VCALENDAR", 15) == 0) {
      saw_vcalendar_ = true;
      return;
    }
    if (strncmp(line, "BEGIN:VEVENT", 12) == 0) {
      in_vevent_ = true;
      reset_event_();
      return;
    }
    if (strncmp(line, "END:VEVENT", 10) == 0) {
      if (in_vevent_) commit_event_();
      in_vevent_ = false;
      return;
    }
    if (!in_vevent_) return;

    const char *colon = find_ics_value_colon(line);
    if (colon == nullptr) return;
    const char *val = colon + 1;

    if (strncmp(line, "DTSTART", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
      ev_has_start_ = parse_ics_date(val, ev_sy_, ev_sm_, ev_sd_, ev_has_time_, ev_hr_, ev_mn_, ev_is_utc_);
    } else if (strncmp(line, "DTEND", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
      ev_has_end_ = parse_ics_date(val, ev_ey_, ev_em_, ev_ed_, ev_end_has_time_, ev_ehr_, ev_emn_, ev_end_is_utc_);
    } else if (strncmp(line, "SUMMARY", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
      clean_summary(val, ev_summary_, sizeof(ev_summary_));
    } else if (strncmp(line, "STATUS", 6) == 0 && (line[6] == ':' || line[6] == ';')) {
      if (icontains(val, "CANCELLED")) ev_cancelled_ = true;
    } else if (strncmp(line, "EXDATE", 6) == 0 && (line[6] == ':' || line[6] == ';')) {
      const char *p = val;
      while (*p != '\0') {
        int ey = 0, em = 0, ed = 0, eh = 0, emn = 0;
        bool eht = false, eutc = false;
        if (parse_ics_date(p, ey, em, ed, eht, eh, emn, eutc)) {
          int64_t ej = ymd_to_jdn(ey, em, ed);
          if (eht && eutc) apply_utc_offset_(ej, eh, emn);
          add_exdate_(ej);
        }
        const char *comma = strchr(p, ',');
        if (comma == nullptr) break;
        p = comma + 1;
      }
    } else if (strncmp(line, "RRULE", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
      if (icontains(val, "FREQ=YEARLY")) ev_yearly_ = true;
      else if (icontains(val, "FREQ=WEEKLY")) ev_weekly_ = true;
      for (const char *p = val; *p; p++) {
        if (strncasecmp(p, "UNTIL=", 6) == 0) {
          int uy = 0, um = 0, ud = 0, uh = 0, umn = 0;
          bool uht = false, uutc = false;
          if (parse_ics_date(p + 6, uy, um, ud, uht, uh, umn, uutc)) {
            int64_t uj = ymd_to_jdn(uy, um, ud);
            if (uht && uutc) apply_utc_offset_(uj, uh, umn);
            ev_has_until_ = true;
            ev_until_jdn_ = uj;
          }
        } else if (strncasecmp(p, "INTERVAL=", 9) == 0) {
          int iv = atoi(p + 9);
          if (iv >= 1 && iv <= 52) ev_interval_ = iv;
        } else if (strncasecmp(p, "BYDAY=", 6) == 0) {
          const char *q = p + 6;
          while (*q && *q != ';') {
            if (strncasecmp(q, "MO", 2) == 0) { ev_byday_mask_ |= (1U << 0); q += 2; }
            else if (strncasecmp(q, "TU", 2) == 0) { ev_byday_mask_ |= (1U << 1); q += 2; }
            else if (strncasecmp(q, "WE", 2) == 0) { ev_byday_mask_ |= (1U << 2); q += 2; }
            else if (strncasecmp(q, "TH", 2) == 0) { ev_byday_mask_ |= (1U << 3); q += 2; }
            else if (strncasecmp(q, "FR", 2) == 0) { ev_byday_mask_ |= (1U << 4); q += 2; }
            else if (strncasecmp(q, "SA", 2) == 0) { ev_byday_mask_ |= (1U << 5); q += 2; }
            else if (strncasecmp(q, "SU", 2) == 0) { ev_byday_mask_ |= (1U << 6); q += 2; }
            else q++;
          }
        }
      }
    }
  }

  int cur_y_{2026}, cur_m_{1}, cur_d_{1};
  int64_t cur_jdn_{0};
  bool is_school_{false};
  int local_utc_offset_min_{0};
  bool saw_vcalendar_{false};
  bool in_vevent_{false};
  bool has_pending_{false};
  char raw_line_[256]{};
  size_t raw_len_{0};
  char pending_line_[256]{};

  uint64_t hol_mask_{0};
  int tomorrow_prio_{-1};

  bool ev_has_start_{false};
  bool ev_has_end_{false};
  bool ev_has_time_{false};
  bool ev_is_utc_{false};
  bool ev_end_has_time_{false};
  bool ev_end_is_utc_{false};
  bool ev_yearly_{false};
  bool ev_weekly_{false};
  int ev_interval_{1};
  uint8_t ev_byday_mask_{0};
  bool ev_has_until_{false};
  bool ev_cancelled_{false};
  int ev_sy_{0}, ev_sm_{0}, ev_sd_{0};
  int ev_ey_{0}, ev_em_{0}, ev_ed_{0};
  int ev_hr_{0}, ev_mn_{0};
  int ev_ehr_{0}, ev_emn_{0};
  static constexpr int kMaxExdates = 8;
  int64_t ev_exdates_[kMaxExdates]{};
  int ev_exdate_n_{0};
  int64_t ev_until_jdn_{0};
  char ev_summary_[48]{};

  IcsFeedSummary summary_;
};

}  // namespace world_clock
