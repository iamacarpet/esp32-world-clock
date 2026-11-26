#pragma once

#include "clock_math.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace world_clock {

// Normalizes iCloud / Apple / Google / School calendar URLs:
// Trims whitespace and rewrites webcal:// or webcals:// -> https:// so parents can paste directly!
inline bool normalize_calendar_url(const char *raw, char *out, size_t cap) {
  if (raw == nullptr || out == nullptr || cap < 16) return false;
  out[0] = '\0';
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
  size_t len = strlen(raw);
  while (len > 0 && (raw[len - 1] == ' ' || raw[len - 1] == '\t' || raw[len - 1] == '\r' || raw[len - 1] == '\n')) {
    len--;
  }
  if (len < 10) return false;

  if (len > 9 && strncasecmp(raw, "webcal://", 9) == 0) {
    int w = snprintf(out, cap, "https://%.*s", (int)(len - 9), raw + 9);
    return (w > 0 && (size_t)w < cap);
  }
  if (len > 10 && strncasecmp(raw, "webcals://", 10) == 0) {
    int w = snprintf(out, cap, "https://%.*s", (int)(len - 10), raw + 10);
    return (w > 0 && (size_t)w < cap);
  }
  if (strncasecmp(raw, "https://", 8) == 0 || strncasecmp(raw, "http://", 7) == 0) {
    int w = snprintf(out, cap, "%.*s", (int)len, raw);
    return (w > 0 && (size_t)w < cap);
  }
  return false;
}

struct IcsFeedSummary {
  bool valid{false};
  int events_seen{0};
  char today_title[48]{""};
  char tomorrow_title[48]{""};
  char next_title[48]{""};
  int next_days_away{-1};
  bool next_is_priority_countdown{false};
  bool today_is_school_holiday{false};
  bool tomorrow_is_school_holiday{false};
  char school_note[48]{""};
};

// Zero-allocation streaming RFC 5545 (.ics) parser designed for Core 0 HTTP callbacks.
// Handles multi-megabyte iCloud / Google / School feeds using < 2 KB of stack/member memory!
class IcsStreamParser {
 public:
  IcsStreamParser(int cur_y, int cur_m, int cur_d, bool is_school_feed = false)
      : cur_y_(cur_y), cur_m_(cur_m), cur_d_(cur_d), is_school_(is_school_feed) {
    cur_jdn_ = ymd_to_jdn(cur_y, cur_m, cur_d);
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

  static bool is_holiday_keyword(const char *s) {
    return icontains(s, "holiday") || icontains(s, "half term") || icontains(s, "half-term") ||
           icontains(s, "inset") || icontains(s, "no school") || icontains(s, "school closed") ||
           icontains(s, "training day") || icontains(s, "teacher day") || icontains(s, "bank hol") ||
           icontains(s, "easter break") || icontains(s, "summer break") || icontains(s, "christmas break") ||
           icontains(s, "winter break") || icontains(s, "spring break") || icontains(s, "closed");
  }

  // Parses YYYYMMDD or YYYYMMDDTHHMMSS[Z]. Returns true on success.
  static bool parse_ics_date(const char *val, int &y, int &m, int &d, bool &has_time, int &hr, int &mn) {
    if (val == nullptr || strlen(val) < 8) return false;
    for (int i = 0; i < 8; i++) {
      if (!isdigit((unsigned char)val[i])) return false;
    }
    y = (val[0] - '0') * 1000 + (val[1] - '0') * 100 + (val[2] - '0') * 10 + (val[3] - '0');
    m = (val[4] - '0') * 10 + (val[5] - '0');
    d = (val[6] - '0') * 10 + (val[7] - '0');
    if (y < 1970 || y > 2150 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    has_time = false;
    hr = 0;
    mn = 0;
    if (val[8] == 'T' && strlen(val) >= 13 && isdigit((unsigned char)val[9]) && isdigit((unsigned char)val[10]) &&
        isdigit((unsigned char)val[11]) && isdigit((unsigned char)val[12])) {
      hr = (val[9] - '0') * 10 + (val[10] - '0');
      mn = (val[11] - '0') * 10 + (val[12] - '0');
      if (hr >= 0 && hr <= 23 && mn >= 0 && mn <= 59) has_time = true;
    }
    return true;
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
        // Keep common Latin-1 accented letters, skip 4-byte emojis unsupported by Fira Code
        if (src[i + 1] != '\0' && w + 2 < cap) {
          dst[w++] = src[i++];
          dst[w++] = src[i];
        }
      }
    }
    while (w > 0 && dst[w - 1] == ' ') w--;
    dst[w] = '\0';
  }

  void handle_raw_line_() {
    // RFC 5545 folded line continuation (starts with single space or tab)
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

  void reset_event_() {
    ev_has_start_ = false;
    ev_has_end_ = false;
    ev_has_time_ = false;
    ev_yearly_ = false;
    ev_sy_ = ev_sm_ = ev_sd_ = 0;
    ev_ey_ = ev_em_ = ev_ed_ = 0;
    ev_hr_ = ev_mn_ = 0;
    ev_summary_[0] = '\0';
  }

  void commit_event_() {
    if (!ev_has_start_ || ev_summary_[0] == '\0') return;
    summary_.events_seen++;

    int sy = ev_sy_, sm = ev_sm_, sd = ev_sd_;
    int64_t start_jdn = ymd_to_jdn(sy, sm, sd);
    int64_t end_jdn_incl = start_jdn;

    if (ev_has_end_) {
      int64_t ej = ymd_to_jdn(ev_ey_, ev_em_, ev_ed_);
      if (!ev_has_time_ && ej > start_jdn) {
        // RFC 5545 all-day DTEND is exclusive (e.g. Jul 20..25 covers Jul 20..24)
        end_jdn_incl = ej - 1;
      } else if (ej >= start_jdn) {
        end_jdn_incl = ej;
      }
    }

    if (ev_yearly_) {
      int64_t span = end_jdn_incl - start_jdn;
      int64_t this_yr = ymd_to_jdn(cur_y_, sm, sd);
      if (this_yr + span < cur_jdn_) {
        this_yr = ymd_to_jdn(cur_y_ + 1, sm, sd);
      }
      start_jdn = this_yr;
      end_jdn_incl = this_yr + span;
    }

    bool hits_today = (cur_jdn_ >= start_jdn && cur_jdn_ <= end_jdn_incl);
    bool hits_tomorrow = ((cur_jdn_ + 1) >= start_jdn && (cur_jdn_ + 1) <= end_jdn_incl);
    int days_away = (int)(start_jdn - cur_jdn_);

    bool is_tagged_countdown = icontains(ev_summary_, "countdown:") || icontains(ev_summary_, "[countdown]");
    bool is_tagged_noschool = icontains(ev_summary_, "[no school]") || icontains(ev_summary_, "[holiday]");

    // Strip optional parent tags ("Countdown:", "[countdown]", "[no school]", "[holiday]") for clean display
    char display_sum[48];
    snprintf(display_sum, sizeof(display_sum), "%s", ev_summary_);
    auto strip_tag_ci = [](char *buf, const char *tag) {
      size_t tlen = strlen(tag);
      for (char *p = buf; *p; p++) {
        size_t k = 0;
        while (k < tlen && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)tag[k])) k++;
        if (k == tlen) {
          memmove(p, p + tlen, strlen(p + tlen) + 1);
          break;
        }
      }
      // Trim leading/trailing spaces
      char *s = buf;
      while (*s == ' ') s++;
      if (s != buf) memmove(buf, s, strlen(s) + 1);
      size_t n = strlen(buf);
      while (n > 0 && buf[n - 1] == ' ') buf[--n] = '\0';
    };
    if (is_tagged_countdown) {
      strip_tag_ci(display_sum, "countdown:");
      strip_tag_ci(display_sum, "[countdown]");
    }
    if (is_tagged_noschool) {
      strip_tag_ci(display_sum, "[no school]");
      strip_tag_ci(display_sum, "[holiday]");
    }
    if (display_sum[0] == '\0') snprintf(display_sum, sizeof(display_sum), "%s", ev_summary_);

    char formatted[48];
    if (hits_today && ev_has_time_ && (ev_hr_ != 0 || ev_mn_ != 0)) {
      std::string ts = format_short_time(ev_hr_, ev_mn_, false);
      snprintf(formatted, sizeof(formatted), "%.34s (%.8s)", display_sum, ts.c_str());
    } else {
      snprintf(formatted, sizeof(formatted), "%.47s", display_sum);
    }

    if (is_school_ || is_tagged_noschool) {
      bool is_hol = is_tagged_noschool || is_holiday_keyword(ev_summary_);
      if (hits_today) {
        if (is_hol) summary_.today_is_school_holiday = true;
        if (summary_.school_note[0] == '\0' || is_hol) {
          snprintf(summary_.school_note, sizeof(summary_.school_note), "%s", formatted);
        }
      }
      if (hits_tomorrow && is_hol) {
        summary_.tomorrow_is_school_holiday = true;
      }
    }

    if (hits_today) {
      if (summary_.today_title[0] == '\0' || (ev_has_time_ && summary_.today_title[0] != '\0')) {
        snprintf(summary_.today_title, sizeof(summary_.today_title), "%s", formatted);
      }
    }
    if (hits_tomorrow && summary_.tomorrow_title[0] == '\0') {
      snprintf(summary_.tomorrow_title, sizeof(summary_.tomorrow_title), "%s", display_sum);
    }
    int max_horizon = is_tagged_countdown ? 90 : 14;
    if (days_away >= 2 && days_away <= max_horizon) {
      bool replace = false;
      if (summary_.next_days_away < 0) replace = true;
      else if (is_tagged_countdown && !summary_.next_is_priority_countdown) replace = true;
      else if (is_tagged_countdown == summary_.next_is_priority_countdown && days_away < summary_.next_days_away) replace = true;
      if (replace) {
        summary_.next_days_away = days_away;
        summary_.next_is_priority_countdown = is_tagged_countdown;
        snprintf(summary_.next_title, sizeof(summary_.next_title), "%s", display_sum);
      }
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

    const char *colon = strchr(line, ':');
    if (colon == nullptr) return;
    const char *val = colon + 1;

    if (strncmp(line, "DTSTART", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
      ev_has_start_ = parse_ics_date(val, ev_sy_, ev_sm_, ev_sd_, ev_has_time_, ev_hr_, ev_mn_);
    } else if (strncmp(line, "DTEND", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
      bool dummy_t = false;
      int dh = 0, dm = 0;
      ev_has_end_ = parse_ics_date(val, ev_ey_, ev_em_, ev_ed_, dummy_t, dh, dm);
    } else if (strncmp(line, "SUMMARY", 7) == 0 && (line[7] == ':' || line[7] == ';')) {
      clean_summary(val, ev_summary_, sizeof(ev_summary_));
    } else if (strncmp(line, "RRULE", 5) == 0 && (line[5] == ':' || line[5] == ';')) {
      if (icontains(val, "FREQ=YEARLY")) ev_yearly_ = true;
    }
  }

  int cur_y_{2026}, cur_m_{1}, cur_d_{1};
  int64_t cur_jdn_{0};
  bool is_school_{false};
  bool saw_vcalendar_{false};
  bool in_vevent_{false};
  bool has_pending_{false};
  char raw_line_[256]{};
  size_t raw_len_{0};
  char pending_line_[256]{};

  bool ev_has_start_{false};
  bool ev_has_end_{false};
  bool ev_has_time_{false};
  bool ev_yearly_{false};
  int ev_sy_{0}, ev_sm_{0}, ev_sd_{0};
  int ev_ey_{0}, ev_em_{0}, ev_ed_{0};
  int ev_hr_{0}, ev_mn_{0};
  char ev_summary_[48]{};

  IcsFeedSummary summary_;
};

}  // namespace world_clock
