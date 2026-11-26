#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef USE_TIME_TIMEZONE
#include "esphome/components/time/posix_tz.h"
#endif

namespace world_clock {

// Cross-task lock-free OTA UI state so async HTTP web_server / ESPHome OTA callbacks
// never touch LVGL objects directly (Core 1's 100ms loop consumes this safely).
struct OtaUiState {
  // 0 = idle, 1 = active/progress, 2 = complete, 3 = error
  std::atomic<uint8_t> state{0};
  std::atomic<int> pct{0};
};

inline OtaUiState &ota_ui_state() {
  static OtaUiState s;
  return s;
}

// Formats a 64-bit random token into an unambiguous 8-character lowercase setup password
// (32-symbol alphabet => 40 bits of entropy, easy to read on screen and type on a phone).
inline std::string format_random_setup_password(uint64_t token) {
  static const char kAlpha[33] = "abcdefghjkmnpqrstuvwxyz23456789";  // 31 chars? No -> exactly 32 below
  (void)kAlpha;
  static const char kSym[33] = "abcdefghjkmnpqrstuvwxyz23456789@";
  // Prefer pure alphanumeric 32-char Crockford-style lowercase set (omits i,l,o,0,1 -> 23 letters + 8 digits = 31; include '0'->no, include 'z' -> a..z(26)-i,l,o(3)=23; 2..9=8 -> 31, add 'L'->no, use 31 modulo!)
  static const char kSafe[] = "abcdefghjkmnpqrstuvwxyz23456789";  // 31 unambiguous alphanumeric chars
  constexpr uint64_t kBase = sizeof(kSafe) - 1;
  (void)kSym;
  std::string out;
  out.reserve(8);
  uint64_t v = token;
  for (int i = 0; i < 8; i++) {
    out.push_back(kSafe[v % kBase]);
    v /= kBase;
  }
  return out;
}

// Circadian sleep phases:
// 0 = Daytime, 1 = Deep Sleep (Red), 2 = Morning Quiet Play (Amber), 3 = Evening Wind-Down (Amber)
struct ScheduleConfig {
  bool winddown_enabled{true};
  bool sleep_enabled{true};
  bool quiet_enabled{true};
  bool weekend_liein_enabled{false};
  bool non_school_bedtime_enabled{false};
  int winddown_m{19 * 60};         // 19:00
  int bedtime_m{19 * 60 + 30};     // 19:30
  int quiet_m{5 * 60};             // 05:00
  int wakeup_m{6 * 60 + 30};       // 06:30
  int special_delay_m{0};          // Optional bedtime shift on Xmas Eve / NYE / Bonfire Night
  int weekend_offset_m{0};         // Optional morning lie-in shift (0..90 min) on non-school mornings
  int non_school_bedtime_m{0};     // Optional evening late-bedtime shift (0..90 min) on non-school nights
};

// Exact Gregorian calendar -> Julian Day Number (valid for all Gregorian dates).
inline int64_t ymd_to_jdn(int y, int m, int d) {
  int a = (14 - m) / 12;
  int yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  return (int64_t)d + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

// Inverse Gregorian JDN -> (year, month, day).
inline void jdn_to_ymd(int64_t jdn, int &y, int &m, int &d) {
  int64_t a = jdn + 32044;
  int64_t b = (4 * a + 3) / 146097;
  int64_t c = a - (146097 * b) / 4;
  int64_t dd = (4 * c + 3) / 1461;
  int64_t e = c - (1461 * dd) / 4;
  int64_t mm = (5 * e + 2) / 153;
  d = (int)(e - (153 * mm + 2) / 5 + 1);
  m = (int)(mm + 3 - 12 * (mm / 10));
  y = (int)(100 * b + dd - 4800 + (mm / 10));
}

inline int days_between_ymd(int y1, int m1, int d1, int y2, int m2, int d2) {
  return (int)(ymd_to_jdn(y2, m2, d2) - ymd_to_jdn(y1, m1, d1));
}

// Day of week: 0=SUN, 1=MON, ..., 6=SAT
inline int ymd_dow_num(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12 || d < 1 || d > 31) return 1;
  if (m < 3) y -= 1;
  int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return (w + 7) % 7;
}

inline bool is_leap_year(int y) { return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0); }

inline bool is_special_night_ymd(int m, int d) {
  return (m == 12 && (d == 24 || d == 31)) || (m == 11 && d == 5);
}

// Returns 0 on the child's birthday, or 1..366 days until the next birthday (handles 1st-of-month & year rollover!).
inline int days_until_next_birthday(int cur_y, int cur_m, int cur_d, int b_mon, int b_day) {
  if (b_mon < 1 || b_mon > 12 || b_day < 1 || b_day > 31) return -1;
  auto clamp_bday = [](int yr, int bm, int bd) {
    if (bm == 2 && bd == 29 && !is_leap_year(yr)) return 28;
    return bd;
  };
  int d_this = days_between_ymd(cur_y, cur_m, cur_d, cur_y, b_mon, clamp_bday(cur_y, b_mon, b_day));
  if (d_this >= 0) return d_this;
  return days_between_ymd(cur_y, cur_m, cur_d, cur_y + 1, b_mon, clamp_bday(cur_y + 1, b_mon, b_day));
}

inline bool is_in_circular_range(int now_m, int start_m, int end_m) {
  start_m = ((start_m % 1440) + 1440) % 1440;
  end_m = ((end_m % 1440) + 1440) % 1440;
  if (start_m == end_m) return false;
  if (start_m < end_m) return (now_m >= start_m && now_m < end_m);
  return (now_m >= start_m || now_m < end_m);
}

inline int circular_minutes_between(int start_m, int end_m) {
  start_m = ((start_m % 1440) + 1440) % 1440;
  end_m = ((end_m % 1440) + 1440) % 1440;
  int diff = end_m - start_m;
  if (diff <= 0) diff += 1440;
  return diff;
}

inline int effective_winddown_m(const ScheduleConfig &cfg, bool is_special_night,
                                bool is_non_school_night = false) {
  int delay = is_special_night ? std::clamp(cfg.special_delay_m, 0, 180) : 0;
  int eve = (is_non_school_night && cfg.non_school_bedtime_enabled)
                ? std::clamp(cfg.non_school_bedtime_m, 0, 120)
                : 0;
  return (cfg.winddown_m + std::max(delay, eve)) % 1440;
}

inline int effective_bedtime_m(const ScheduleConfig &cfg, bool is_special_night,
                               bool is_non_school_night = false) {
  int delay = is_special_night ? std::clamp(cfg.special_delay_m, 0, 180) : 0;
  int eve = (is_non_school_night && cfg.non_school_bedtime_enabled)
                ? std::clamp(cfg.non_school_bedtime_m, 0, 120)
                : 0;
  return (cfg.bedtime_m + std::max(delay, eve)) % 1440;
}

inline int effective_quiet_m(const ScheduleConfig &cfg, bool is_non_school_morning) {
  int shift = (is_non_school_morning && cfg.weekend_liein_enabled) ? std::clamp(cfg.weekend_offset_m, 0, 120) : 0;
  return (cfg.quiet_m + shift) % 1440;
}

inline int effective_wakeup_m(const ScheduleConfig &cfg, bool is_non_school_morning) {
  int shift = (is_non_school_morning && cfg.weekend_liein_enabled) ? std::clamp(cfg.weekend_offset_m, 0, 120) : 0;
  return (cfg.wakeup_m + shift) % 1440;
}

// Overnight-continuous cycle helpers (noon-split at 12:00 / 720m):
// - Afternoon/Evening (now_m >= 720): active sleep cycle runs TONIGHT -> TOMORROW MORNING,
//   so both bedtime and morning quiet/wake targets use `is_tomorrow_non_school` and `is_today_special_night`.
// - Morning/Post-Midnight (now_m < 720): active sleep cycle started YESTERDAY EVENING -> THIS MORNING,
//   so both bedtime and morning quiet/wake targets use `is_today_non_school` and `is_yesterday_special_night`.
// This guarantees zero jump at 00:00 in sleep phase, {quiet} card wording, and 5 countdown stars!
inline int effective_cycle_winddown_m(int now_m, const ScheduleConfig &cfg, bool is_today_special_night,
                                      bool is_today_non_school, bool is_tomorrow_non_school,
                                      bool is_yesterday_special_night = false) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  bool morning = (now_m < 720);
  return effective_winddown_m(cfg, morning ? is_yesterday_special_night : is_today_special_night,
                              morning ? is_today_non_school : is_tomorrow_non_school);
}

inline int effective_cycle_bedtime_m(int now_m, const ScheduleConfig &cfg, bool is_today_special_night,
                                     bool is_today_non_school, bool is_tomorrow_non_school,
                                     bool is_yesterday_special_night = false) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  bool morning = (now_m < 720);
  return effective_bedtime_m(cfg, morning ? is_yesterday_special_night : is_today_special_night,
                             morning ? is_today_non_school : is_tomorrow_non_school);
}

inline int effective_cycle_quiet_m(int now_m, const ScheduleConfig &cfg, bool is_today_non_school,
                                   bool is_tomorrow_non_school) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  return effective_quiet_m(cfg, (now_m < 720) ? is_today_non_school : is_tomorrow_non_school);
}

inline int effective_cycle_wakeup_m(int now_m, const ScheduleConfig &cfg, bool is_today_non_school,
                                    bool is_tomorrow_non_school) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  return effective_wakeup_m(cfg, (now_m < 720) ? is_today_non_school : is_tomorrow_non_school);
}

// Evaluates the 4-phase circadian schedule continuously across midnight!
inline uint8_t evaluate_schedule(int now_m, const ScheduleConfig &cfg, bool is_today_special_night,
                                 bool is_today_non_school = false, bool is_tomorrow_non_school = false,
                                 bool is_yesterday_special_night = false) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  int wd_start = effective_cycle_winddown_m(now_m, cfg, is_today_special_night, is_today_non_school,
                                            is_tomorrow_non_school, is_yesterday_special_night);
  int bed_start = effective_cycle_bedtime_m(now_m, cfg, is_today_special_night, is_today_non_school,
                                            is_tomorrow_non_school, is_yesterday_special_night);
  int quiet_start = effective_cycle_quiet_m(now_m, cfg, is_today_non_school, is_tomorrow_non_school);
  int wake_end = effective_cycle_wakeup_m(now_m, cfg, is_today_non_school, is_tomorrow_non_school);

  if (cfg.winddown_enabled && is_in_circular_range(now_m, wd_start, bed_start)) {
    return 3;  // Evening Wind-Down (Amber)
  }

  if (cfg.sleep_enabled) {
    int sleep_end = cfg.quiet_enabled ? quiet_start : wake_end;
    if (is_in_circular_range(now_m, bed_start, sleep_end)) {
      return 1;  // Deep Sleep (Red)
    }
  }

  if (cfg.quiet_enabled && is_in_circular_range(now_m, quiet_start, wake_end)) {
    return 2;  // Morning Quiet Play (Amber)
  }

  return 0;  // Daytime
}

// Computes continuous remaining fraction (0.0 .. 1.0) in the active Sleep (1) or Wind-Down (3) phase
// across midnight so 1..5 glowing countdown stars never jump at 00:00!
inline float sleep_phase_remaining_01(int now_m, uint8_t phase, const ScheduleConfig &cfg,
                                      bool is_today_special_night, bool is_today_non_school = false,
                                      bool is_tomorrow_non_school = false,
                                      bool is_yesterday_special_night = false) {
  now_m = ((now_m % 1440) + 1440) % 1440;
  int start_m = 0;
  int end_m = 0;
  if (phase == 3) {
    start_m = effective_cycle_winddown_m(now_m, cfg, is_today_special_night, is_today_non_school,
                                         is_tomorrow_non_school, is_yesterday_special_night);
    end_m = effective_cycle_bedtime_m(now_m, cfg, is_today_special_night, is_today_non_school,
                                      is_tomorrow_non_school, is_yesterday_special_night);
  } else if (phase == 1) {
    start_m = effective_cycle_bedtime_m(now_m, cfg, is_today_special_night, is_today_non_school,
                                        is_tomorrow_non_school, is_yesterday_special_night);
    end_m = cfg.quiet_enabled ? effective_cycle_quiet_m(now_m, cfg, is_today_non_school, is_tomorrow_non_school)
                              : effective_cycle_wakeup_m(now_m, cfg, is_today_non_school, is_tomorrow_non_school);
  } else {
    return 0.0f;
  }
  int total = circular_minutes_between(start_m, end_m);
  if (total <= 0) return 0.0f;
  int elapsed = circular_minutes_between(start_m, now_m);
  if (now_m == start_m) elapsed = 0;
  if (elapsed >= total) return 0.0f;
  return std::clamp(1.0f - (float)elapsed / (float)total, 0.0f, 1.0f);
}

// Exact Anonymous Gregorian (Meeus/Jones/Butcher) Easter Sunday date.
inline void easter_sunday_ymd(int y, int &m, int &d) {
  int a = y % 19;
  int b = y / 100;
  int c = y % 100;
  int d_ = b / 4;
  int e = b % 4;
  int f = (b + 8) / 25;
  int g = (b - f + 1) / 3;
  int h = (19 * a + b - d_ - g + 15) % 30;
  int i = c / 4;
  int k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int mm = (a + 11 * h + 22 * l) / 451;
  m = (h + l - 7 * mm + 114) / 31;
  d = ((h + l - 7 * mm + 114) % 31) + 1;
}

// Easter Egg Hunt window: Good Friday through Easter Monday inclusive (-2 .. +1 days around Easter Sunday).
inline bool is_easter_window(int y, int m, int d) {
  int em = 4, ed = 1;
  easter_sunday_ymd(y, em, ed);
  int diff = days_between_ymd(y, em, ed, y, m, d);
  return (diff >= -2 && diff <= 1);
}

// Shrove Tuesday / Pancake Day = Easter Sunday - 47 days.
inline bool is_pancake_day(int y, int m, int d) {
  int em = 4, ed = 1;
  easter_sunday_ymd(y, em, ed);
  return days_between_ymd(y, em, ed, y, m, d) == -47;
}

// UK Mother's Day (Mothering Sunday) = Easter Sunday - 21 days.
inline bool is_uk_mothers_day(int y, int m, int d) {
  int em = 4, ed = 1;
  easter_sunday_ymd(y, em, ed);
  return days_between_ymd(y, em, ed, y, m, d) == -21;
}

// Father's Day (UK/US/CA) = 3rd Sunday of June.
inline bool is_fathers_day(int y, int m, int d) {
  return (m == 6 && d >= 15 && d <= 21 && ymd_dow_num(y, m, d) == 0);
}

// World Book Day (UK/Ireland) = 1st Thursday of March.
inline bool is_world_book_day(int y, int m, int d) {
  return (m == 3 && d >= 1 && d <= 7 && ymd_dow_num(y, m, d) == 4);
}

// Peak annual meteor showers (Perseids Aug 11-13, Geminids Dec 13-14).
inline bool is_meteor_shower(int m, int d) {
  return (m == 8 && d >= 11 && d <= 13) || (m == 12 && (d == 13 || d == 14));
}

// Astronomical Solstices (Northern hemisphere longest/shortest day).
inline int solstice_kind(int m, int d) {
  if (m == 6 && (d == 20 || d == 21)) return 1;   // Summer Solstice
  if (m == 12 && (d == 21 || d == 22)) return 2;  // Winter Solstice
  return 0;
}

// Synodic lunar phase in [0.0, 1.0): 0.0=New Moon, 0.25=First Quarter, 0.50=Full Moon, 0.75=Last Quarter.
inline float lunar_phase_01(int y, int m, int d) {
  int64_t jdn = ymd_to_jdn(y, m, d);
  double days_since_new = (double)jdn - 2451550.25972;
  double cycles = days_since_new / 29.53058867;
  double frac = cycles - floor(cycles);
  if (frac < 0.0) frac += 1.0;
  return (float)frac;
}

inline std::string format_short_time(int hour, int minute, bool use_24h) {
  hour = ((hour % 24) + 24) % 24;
  minute = ((minute % 60) + 60) % 60;
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
  return buf;
}

struct TokenContext {
  std::string name;
  std::string winddown;
  std::string bedtime;
  std::string quiet;
  std::string wakeup;
};

inline void replace_all(std::string &s, const char *needle, const std::string &val) {
  size_t nlen = strlen(needle);
  if (nlen == 0) return;
  size_t pos = 0;
  while ((pos = s.find(needle, pos)) != std::string::npos) {
    s.replace(pos, nlen, val);
    pos += val.size();
  }
}

inline std::string expand_tokens(std::string tmpl, const TokenContext &ctx) {
  replace_all(tmpl, "{name}", ctx.name);
  replace_all(tmpl, "{winddown}", ctx.winddown);
  replace_all(tmpl, "{bedtime}", ctx.bedtime);
  replace_all(tmpl, "{quiet}", ctx.quiet);
  replace_all(tmpl, "{wakeup}", ctx.wakeup);
  return tmpl;
}

// Uppercases ASCII a..z AND common UTF-8 Latin-1 accented letters (à..þ excluding ÷).
inline std::string to_upper_ascii(std::string s) {
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 'a' && c <= 'z') {
      s[i] = (char)(c - 32);
    } else if (c == 0xC3 && i + 1 < s.size()) {
      unsigned char b = (unsigned char)s[i + 1];
      if (b >= 0xA0 && b <= 0xBE && b != 0xB7) {
        s[i + 1] = (char)(b - 0x20);
      }
      i++;
    }
  }
  return s;
}

// UTF-8 safe truncation by Unicode codepoint count (never splits multi-byte sequences).
inline void utf8_truncate_codepoints(std::string &s, size_t max_cp) {
  size_t cp = 0;
  size_t i = 0;
  while (i < s.size()) {
    if (((unsigned char)s[i] & 0xC0) != 0x80) {
      if (cp == max_cp) {
        s.resize(i);
        return;
      }
      cp++;
    }
    i++;
  }
}

// Temperature & distance unit conversions
inline float c_to_f(float c) { return c * 1.8f + 32.0f; }
inline int km_to_mi(int km) { return (int)roundf((float)km * 0.621371f); }

// Miller cylindrical projection (maps world lat/lon -> 800x322 canvas pixels).
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
  constexpr float kD2R = 0.0174532925f;
  float dlat = (lat2 - lat1) * kD2R;
  float dlon = (lon2 - lon1) * kD2R;
  float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
            cosf(lat1 * kD2R) * cosf(lat2 * kD2R) * sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
  return 6371.0f * 2.0f * atan2f(sqrtf(a), sqrtf(std::max(0.0f, 1.0f - a)));
}

// Finds the matching '}' of the first JSON object starting at or after `src`.
inline const char *find_json_object_end(const char *src) {
  if (src == nullptr) return nullptr;
  const char *p = strchr(src, '{');
  if (p == nullptr) return nullptr;
  int depth = 0;
  bool in_str = false;
  bool esc = false;
  for (; *p != '\0'; p++) {
    if (in_str) {
      if (esc) {
        esc = false;
      } else if (*p == '\\') {
        esc = true;
      } else if (*p == '"') {
        in_str = false;
      }
      continue;
    }
    if (*p == '"') {
      in_str = true;
    } else if (*p == '{') {
      depth++;
    } else if (*p == '}') {
      depth--;
      if (depth == 0) return p + 1;
    }
  }
  return nullptr;
}

// Bounded JSON numeric extractor (never scans past `end`, preventing key bleed into sibling objects).
inline bool extract_json_num_bounded(const char *start, const char *end, const char *key, float &out_v) {
  if (start == nullptr || key == nullptr) return false;
  size_t klen = strlen(key);
  if (klen == 0) return false;
  const char *limit = (end != nullptr) ? end : (start + strlen(start));
  const char *p = start;
  while (p + klen <= limit) {
    if (memcmp(p, key, klen) == 0) {
      const char *q = p + klen;
      while (q < limit && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
      if (q < limit && *q == ':') {
        q++;
        while (q < limit && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == '"')) q++;
        if (q < limit) {
          char *endp = nullptr;
          float v = strtof(q, &endp);
          if (endp != q && endp <= limit && std::isfinite(v)) {
            out_v = v;
            return true;
          }
        }
      }
    }
    p++;
  }
  return false;
}

// Lightweight JSON & Open-Meteo parsing helpers (zero heap allocations).
inline bool extract_json_num(const char *src, const char *key, float &out_v) {
  return extract_json_num_bounded(src, nullptr, key, out_v);
}

// Checks whether a JSON key maps to an exact string value (ignoring whitespace around ':').
inline bool json_key_equals_str(const char *src, const char *key, const char *expected) {
  if (src == nullptr || key == nullptr || expected == nullptr) return false;
  const char *p = strstr(src, key);
  if (p == nullptr) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != ':') return false;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != '"') return false;
  p++;
  size_t elen = strlen(expected);
  return (strncmp(p, expected, elen) == 0 && p[elen] == '"');
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
  static const char *const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  if (m < 1 || m > 12 || d < 1 || d > 31) return "DAY";
  return kDays[ymd_dow_num(y, m, d)];
}

#ifdef USE_TIME_TIMEZONE
// Runtime timezone switcher for the universal pre-built standalone firmware!
// Note: POSIX std_offset_seconds is positive WEST of UTC (e.g. US Eastern = +5*3600).
inline bool apply_named_timezone(const std::string &name) {
  using namespace esphome::time;
  ParsedTimezone tz{};
  auto mwd = [](uint8_t mon, uint8_t wk, uint8_t dow, int32_t sec) -> DSTRule {
    DSTRule r{};
    r.time_seconds = sec;
    r.type = DSTRuleType::MONTH_WEEK_DAY;
    r.month = mon;
    r.week = wk;
    r.day_of_week = dow;
    return r;
  };
  DSTRule none{};
  none.type = DSTRuleType::NONE;

  if (name == "UK (London GMT/BST)") {
    tz = {0, -3600, mwd(3, 5, 0, 3600), mwd(10, 5, 0, 7200)};
  } else if (name == "Europe (Paris/Berlin CET)") {
    tz = {-3600, -7200, mwd(3, 5, 0, 7200), mwd(10, 5, 0, 10800)};
  } else if (name == "Europe (Helsinki/Athens EET)") {
    tz = {-7200, -10800, mwd(3, 5, 0, 10800), mwd(10, 5, 0, 14400)};
  } else if (name == "US Eastern (ET)") {
    tz = {18000, 14400, mwd(3, 2, 0, 7200), mwd(11, 1, 0, 7200)};
  } else if (name == "US Central (CT)") {
    tz = {21600, 18000, mwd(3, 2, 0, 7200), mwd(11, 1, 0, 7200)};
  } else if (name == "US Mountain (MT)") {
    tz = {25200, 21600, mwd(3, 2, 0, 7200), mwd(11, 1, 0, 7200)};
  } else if (name == "US Pacific (PT)") {
    tz = {28800, 25200, mwd(3, 2, 0, 7200), mwd(11, 1, 0, 7200)};
  } else if (name == "Canada Atlantic (AT)") {
    tz = {14400, 10800, mwd(3, 2, 0, 7200), mwd(11, 1, 0, 7200)};
  } else if (name == "Australia Sydney (AEST/AEDT)") {
    tz = {-36000, -39600, mwd(10, 1, 0, 7200), mwd(4, 1, 0, 10800)};
  } else if (name == "Australia Perth (AWST)") {
    tz = {-28800, -28800, none, none};
  } else if (name == "New Zealand (NZST/NZDT)") {
    tz = {-43200, -46800, mwd(9, 5, 0, 7200), mwd(4, 1, 0, 10800)};
  } else if (name == "UTC") {
    tz = {0, 0, none, none};
  } else {
    return false;
  }
  set_global_tz(tz);
  return true;
}
#endif

}  // namespace world_clock
