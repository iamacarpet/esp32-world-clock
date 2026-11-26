#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace world_clock {

// Circadian sleep phases:
// 0 = Daytime, 1 = Deep Sleep (Red), 2 = Morning Quiet Play (Amber), 3 = Evening Wind-Down (Amber)
struct ScheduleConfig {
  bool winddown_enabled{true};
  bool sleep_enabled{true};
  bool quiet_enabled{true};
  int winddown_m{19 * 60};       // 19:00
  int bedtime_m{19 * 60 + 30};   // 19:30
  int quiet_m{4 * 60};           // 04:00
  int wakeup_m{6 * 60};          // 06:00
  int special_delay_m{0};        // Optional delay on Xmas Eve / NYE / Bonfire Night
};

inline int wrap_minutes(int m) {
  m %= 1440;
  return (m < 0) ? (m + 1440) : m;
}

inline bool in_window(int cur, int start, int end) {
  start = wrap_minutes(start);
  end = wrap_minutes(end);
  cur = wrap_minutes(cur);
  if (start == end) return false;
  return (start < end) ? (cur >= start && cur < end) : (cur >= start || cur < end);
}

// Evaluates the 4-phase circadian schedule safely across midnight and disabled phases.
inline uint8_t evaluate_schedule(int mins, const ScheduleConfig &cfg, bool is_special_night) {
  const int delay = is_special_night ? std::clamp(cfg.special_delay_m, 0, 180) : 0;
  const int wd_m = wrap_minutes(cfg.winddown_m + delay);
  const int bed_m = wrap_minutes(cfg.bedtime_m + delay);
  const int quiet_m = wrap_minutes(cfg.quiet_m);
  const int wake_m = wrap_minutes(cfg.wakeup_m);

  // Determine the effective end of deep sleep (quiet_m if quiet play enabled, else wake_m)
  const int sleep_end = cfg.quiet_enabled ? quiet_m : wake_m;

  // 1. Deep Sleep takes precedence over evening wind-down once bedtime arrives
  if (cfg.sleep_enabled && in_window(mins, bed_m, sleep_end)) {
    return 1;
  }
  // 2. Morning Quiet Play (between quiet_m and wake_m)
  if (cfg.quiet_enabled && in_window(mins, quiet_m, wake_m)) {
    return 2;
  }
  // 3. Evening Wind-Down ("Get Ready for Bed")
  if (cfg.winddown_enabled) {
    const int wd_end = cfg.sleep_enabled ? bed_m : sleep_end;
    if (in_window(mins, wd_m, wd_end)) {
      return 3;
    }
  }
  return 0;
}

// Exact Anonymous Gregorian Easter algorithm + March/April calendar boundary check (+/- 1 day).
inline void compute_easter_date(int yr, int &out_month, int &out_day) {
  int a = yr % 19;
  int b = yr / 100;
  int c = yr % 100;
  int d_e = b / 4;
  int e = b % 4;
  int f = (b + 8) / 25;
  int g = (b - f + 1) / 3;
  int h = (19 * a + b - d_e - g + 15) % 30;
  int i = c / 4;
  int k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m_e = (a + 11 * h + 22 * l) / 451;
  out_month = (h + l - 7 * m_e + 114) / 31;
  out_day = ((h + l - 7 * m_e + 114) % 31) + 1;
}

inline bool is_easter_window(int yr, int month, int day) {
  int emon = 0, eday = 0;
  compute_easter_date(yr, emon, eday);
  auto mar_apr_day = [](int mo, int dy) -> int {
    return (mo == 3) ? dy : ((mo == 4) ? (31 + dy) : -99);
  };
  int d_now = mar_apr_day(month, day);
  int d_eas = mar_apr_day(emon, eday);
  return (d_now > 0 && d_eas > 0 && std::abs(d_now - d_eas) <= 1);
}

// Lunar phase in [0.0, 1.0): 0.0 = New Moon, 0.25 = First Quarter, 0.5 = Full Moon, 0.75 = Last Quarter.
inline float lunar_phase_01(int year, int month, int day) {
  // Julian Day Number at 12:00 UTC
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  // Reference New Moon: 2000-01-06 18:14 UTC (JD 2451550.26)
  double days_since = (double)jdn - 2451550.26;
  double cycles = days_since / 29.530588853;
  double phase = cycles - std::floor(cycles);
  if (phase < 0.0) phase += 1.0;
  return (float)phase;
}

// Format hour:minute as compact 12h ("4 AM", "7:30 PM") or 24h ("04:00", "19:30").
inline std::string format_short_time(int hour, int minute, bool use_24h) {
  char buf[16];
  if (use_24h) {
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    const char *ap = (hour >= 12) ? "PM" : "AM";
    if (minute == 0) {
      snprintf(buf, sizeof(buf), "%d %s", h12, ap);
    } else {
      snprintf(buf, sizeof(buf), "%d:%02d %s", h12, minute, ap);
    }
  }
  return std::string(buf);
}

inline void replace_all(std::string &s, const char *from, const std::string &to) {
  size_t from_len = strlen(from);
  if (from_len == 0) return;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from_len, to);
    pos += to.size();
  }
}

struct TokenContext {
  std::string name;
  std::string winddown;
  std::string bedtime;
  std::string quiet;
  std::string wakeup;
};

inline std::string expand_tokens(std::string s, const TokenContext &ctx) {
  replace_all(s, "{name}", ctx.name);
  replace_all(s, "{winddown}", ctx.winddown);
  replace_all(s, "{bedtime}", ctx.bedtime);
  replace_all(s, "{quiet}", ctx.quiet);
  replace_all(s, "{wakeup}", ctx.wakeup);
  return s;
}

// UTF-8 safe truncation to at most `max_cp` Unicode codepoints (never splits multi-byte chars).
inline void utf8_truncate_codepoints(std::string &s, size_t max_cp) {
  size_t cp = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (((unsigned char)s[i] & 0xC0U) != 0x80U) {
      if (cp == max_cp) {
        s.resize(i);
        return;
      }
      cp++;
    }
  }
}

inline std::string to_upper_ascii(std::string s) {
  for (char &ch : s) ch = (char)toupper((unsigned char)ch);
  return s;
}

inline float c_to_f(float c) { return c * 1.8f + 32.0f; }
inline int km_to_mi(int km) { return (int)roundf((float)km * 0.621371f); }

// --- Lightweight JSON & Weather Helpers (shared by NetWorker + unit tests) ---
inline bool extract_json_num(const char *src, const char *key, float &out_v) {
  if (src == nullptr || key == nullptr) return false;
  const char *p = strstr(src, key);
  if (p == nullptr) return false;
  p += strlen(key);
  while (*p == ' ' || *p == ':' || *p == '"') p++;
  char *endp = nullptr;
  float v = strtof(p, &endp);
  if (endp == p || !std::isfinite(v)) return false;
  out_v = v;
  return true;
}

inline bool parse_csv_floats(const char *arr_start, float *out, int count) {
  if (arr_start == nullptr || out == nullptr || count <= 0) return false;
  const char *b = strchr(arr_start, '[');
  if (b == nullptr) return false;
  b++;
  for (int i = 0; i < count; i++) {
    while (*b == ' ' || *b == ',') b++;
    char *endp = nullptr;
    float v = strtof(b, &endp);
    if (endp == b || !std::isfinite(v)) return false;
    out[i] = v;
    b = endp;
  }
  return true;
}

inline const char *wmo_to_cond(int wmo, bool is_day) {
  if (wmo == 0) return is_day ? "sunny" : "clear-night";
  if (wmo == 1 || wmo == 2) return "partlycloudy";
  if (wmo == 3) return "cloudy";
  if (wmo == 45 || wmo == 48) return "fog";
  if (wmo >= 51 && wmo <= 57) return "rainy";
  if (wmo == 61 || wmo == 63 || wmo == 80 || wmo == 81) return "rainy";
  if (wmo == 65 || wmo == 66 || wmo == 67 || wmo == 82) return "pouring";
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return "snowy";
  if (wmo >= 95) return "lightning-rainy";
  return "cloudy";
}

inline const char *ymd_to_dow(int y, int m, int d) {
  static const char *kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12 || d < 1 || d > 31) return "DAY";
  if (m < 3) y -= 1;
  int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return kDays[(w + 7) % 7];
}

// Miller cylindrical map projection matching the 72x33 continent dot grid (800x322 canvas).
inline void latlon_to_map_xy(float lat_d, float lon_d, float &ox, float &oy) {
  constexpr float kDeg2Rad = (float)M_PI / 180.0f;
  constexpr float kYTopM = 1.6637572f;
  constexpr float kYSpanM = 2.6869962f;
  float lc = std::clamp(lat_d, -85.0f, 85.0f) * kDeg2Rad;
  float ym = 1.25f * logf(tanf(0.7853982f + 0.4f * lc));
  ox = 10.0f + (lon_d + 167.0f) * (781.0f / 345.0f);
  oy = 11.0f + ((kYTopM - ym) / kYSpanM) * 288.0f;
}

inline float haversine_km(float lat1, float lon1, float lat2, float lon2) {
  constexpr float kDeg2Rad = (float)M_PI / 180.0f;
  float dlat = (lat2 - lat1) * kDeg2Rad;
  float dlon = (lon2 - lon1) * kDeg2Rad;
  float hav = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(lat1 * kDeg2Rad) * cosf(lat2 * kDeg2Rad) * sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
  return 12742.0f * asinf(sqrtf(std::clamp(hav, 0.0f, 1.0f)));
}

}  // namespace world_clock
