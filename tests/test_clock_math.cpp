#include "clock_math.h"
#include "ics_parser.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  using namespace world_clock;

  // 1. 4-Phase Circadian Schedule (including midnight wrap, weekend lie-in & disabled phases)
  ScheduleConfig cfg;
  cfg.winddown_enabled = true;
  cfg.sleep_enabled = true;
  cfg.quiet_enabled = true;
  cfg.weekend_liein_enabled = true;
  cfg.winddown_m = 19 * 60;       // 19:00
  cfg.bedtime_m = 19 * 60 + 30;   // 19:30
  cfg.quiet_m = 4 * 60;           // 04:00
  cfg.wakeup_m = 6 * 60;          // 06:00
  cfg.special_delay_m = 0;
  cfg.weekend_offset_m = 60;      // +60m lie-in on Sat/Sun & school holidays

  assert(evaluate_schedule(18 * 60 + 59, cfg, false, false) == 0);  // Day
  assert(evaluate_schedule(19 * 60, cfg, false, false) == 3);       // Evening Wind-Down (Amber)
  assert(evaluate_schedule(19 * 60 + 29, cfg, false, false) == 3);
  assert(evaluate_schedule(19 * 60 + 30, cfg, false, false) == 1);  // Deep Sleep (Red)
  assert(evaluate_schedule(0 * 60 + 15, cfg, false, false) == 1);   // Midnight wrap -> Deep Sleep
  assert(evaluate_schedule(3 * 60 + 59, cfg, false, false) == 1);
  assert(evaluate_schedule(4 * 60, cfg, false, false) == 2);        // Morning Quiet Play (Amber)
  assert(evaluate_schedule(5 * 60 + 59, cfg, false, false) == 2);
  assert(evaluate_schedule(6 * 60, cfg, false, false) == 0);        // Morning Wake-Up -> Day

  // Weekend / School Holiday Lie-In (+60 min -> quiet 05:00, wake 07:00)
  assert(evaluate_schedule(4 * 60 + 30, cfg, false, true) == 1);    // Still Deep Sleep!
  assert(evaluate_schedule(5 * 60, cfg, false, true) == 2);         // Quiet Play starts at 05:00
  assert(evaluate_schedule(6 * 60 + 30, cfg, false, true) == 2);    // Still Quiet Play at 06:30!
  assert(evaluate_schedule(7 * 60, cfg, false, true) == 0);         // Wake at 07:00

  // Countdown stars remaining fraction
  assert(fabsf(sleep_phase_remaining_01(19 * 60, 3, cfg, false, false) - 1.0f) < 1e-3f);
  assert(fabsf(sleep_phase_remaining_01(19 * 60 + 15, 3, cfg, false, false) - 0.5f) < 1e-3f);
  assert(fabsf(sleep_phase_remaining_01(19 * 60 + 30, 1, cfg, false, false) - 1.0f) < 1e-3f);
  float mid_sleep = sleep_phase_remaining_01(23 * 60 + 45, 1, cfg, false, false);
  assert(fabsf(mid_sleep - 0.5f) < 1e-3f);  // 255m of 510m remaining

  // Special-night delay (+60m shifts 19:00 -> 20:00, 19:30 -> 20:30)
  cfg.special_delay_m = 60;
  assert(evaluate_schedule(19 * 60 + 30, cfg, true) == 0);
  assert(evaluate_schedule(20 * 60 + 15, cfg, true) == 3);
  assert(evaluate_schedule(20 * 60 + 30, cfg, true) == 1);

  // Disabled individual phases
  ScheduleConfig no_wd = cfg;
  no_wd.winddown_enabled = false;
  assert(evaluate_schedule(19 * 60 + 15, no_wd, false) == 0);
  assert(evaluate_schedule(19 * 60 + 30, no_wd, false) == 1);

  ScheduleConfig no_quiet = cfg;
  no_quiet.quiet_enabled = false;
  no_quiet.weekend_liein_enabled = false;
  assert(evaluate_schedule(4 * 60 + 30, no_quiet, false) == 1);  // Sleep extends directly to wake-up
  assert(evaluate_schedule(6 * 60, no_quiet, false) == 0);

  // 2. Exact Gregorian Easter & UK Special Occasions
  // 2026: Easter Sunday = Apr 5 -> Pancake Day = Feb 17, UK Mother's Day = Mar 15
  // World Book Day = 1st Thu of Mar = Mar 5; Father's Day = 3rd Sun of Jun = Jun 21
  assert(is_easter_window(2025, 4, 20));
  assert(is_easter_window(2026, 4, 3));   // Good Friday
  assert(is_easter_window(2026, 4, 5));   // Easter Sunday
  assert(is_easter_window(2026, 4, 6));   // Easter Monday
  assert(!is_easter_window(2026, 4, 7));
  assert(is_easter_window(2027, 3, 28));
  assert(is_easter_window(2028, 4, 16));
  assert(is_pancake_day(2026, 2, 17));
  assert(!is_pancake_day(2026, 2, 18));
  assert(is_uk_mothers_day(2026, 3, 15));
  assert(is_world_book_day(2026, 3, 5));
  assert(is_fathers_day(2026, 6, 21));
  assert(is_meteor_shower(8, 12));
  assert(is_meteor_shower(12, 14));
  assert(solstice_kind(6, 21) == 1);
  assert(solstice_kind(12, 21) == 2);

  // 3. Real Lunar Phase sanity
  float full_p = lunar_phase_01(2026, 1, 3);
  assert(full_p > 0.45f && full_p < 0.55f);
  float new_p = lunar_phase_01(2026, 1, 18);
  assert(new_p < 0.08f || new_p > 0.92f);

  // 4. Short Time Formatting & Token Expansion & UTF-8 Truncation
  assert(format_short_time(4, 0, false) == "4 AM");
  assert(format_short_time(19, 30, false) == "7:30 PM");
  assert(format_short_time(19, 30, true) == "19:30");
  TokenContext ctx{"LEO", "7 PM", "7:30 PM", "4:30 AM", "6 AM"};
  assert(expand_tokens("BEFORE {quiet} · SLEEP TIME", ctx) == "BEFORE 4:30 AM · SLEEP TIME");
  assert(expand_tokens("GOOD MORNING, {name}!", ctx) == "GOOD MORNING, LEO!");
  std::string long_utf8 = "HELLO \u2192 WORLD \u2192 THIS IS A VERY LONG CUSTOM TICKER MESSAGE";
  utf8_truncate_codepoints(long_utf8, 15);
  assert(long_utf8 == "HELLO \u2192 WORLD \u2192");

  // 5. JSON & Unit Helpers
  float v = 0.0f;
  assert(extract_json_num("{\"latitude\": \"51.5074\", \"longitude\": \"-0.1278\"}", "\"latitude\"", v));
  assert(fabsf(v - 51.5074f) < 1e-4f);
  float arr[4] = {};
  assert(parse_csv_floats(": [10.5, 11.2, -1.5, 8.0]", arr, 4));
  assert(fabsf(arr[2] - (-1.5f)) < 1e-4f);
  assert(std::string(ymd_to_dow(2026, 11, 26)) == "THU");
  assert(fabsf(c_to_f(0.0f) - 32.0f) < 1e-4f);
  assert(km_to_mi(100) == 62);

  // 6. Calendar URL Normalization & Streaming RFC 5545 (.ics) Parser
  char norm[128] = "";
  assert(normalize_calendar_url("  webcal://p123-caldav.icloud.com/published/2/ABC  ", norm, sizeof(norm)));
  assert(std::string(norm) == "https://p123-caldav.icloud.com/published/2/ABC");

  const char *kFamilyIcs =
      "BEGIN:VCALENDAR\r\n"
      "VERSION:2.0\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20261112T160000\r\n"
      "SUMMARY:Swimming Lesson\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261113\r\n"
      "SUMMARY:Grandma Visit\r\n"
      " "
      "After School\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261120\r\n"
      "DTEND;VALUE=DATE:20261123\r\n"
      "SUMMARY:Cornwall Trip\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20270110\r\n"
      "SUMMARY:Countdown: Disney Holiday\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";

  // Feed in tiny 7-byte chunks to verify streaming boundary safety!
  IcsStreamParser fam(2026, 11, 12, false);
  for (size_t i = 0; i < strlen(kFamilyIcs); i += 7) {
    size_t n = std::min((size_t)7, strlen(kFamilyIcs) - i);
    fam.feed(kFamilyIcs + i, n);
  }
  fam.finish();
  assert(fam.summary().valid);
  assert(std::string(fam.summary().today_title) == "Swimming Lesson (4 PM)");
  assert(std::string(fam.summary().tomorrow_title) == "Grandma VisitAfter School");
  // Priority "Countdown:" tag (59 days out) beats untagged regular event (8 days out) and strips the prefix!
  assert(fam.summary().next_is_priority_countdown);
  assert(fam.summary().next_days_away == 59);
  assert(std::string(fam.summary().next_title) == "Disney Holiday");

  const char *kSchoolIcs =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261026\r\n"
      "DTEND;VALUE=DATE:20261031\r\n"
      "SUMMARY:Autumn Half Term Holiday\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  IcsStreamParser sch_hit(2026, 10, 28, true);
  sch_hit.feed(kSchoolIcs, strlen(kSchoolIcs));
  sch_hit.finish();
  assert(sch_hit.summary().valid);
  assert(sch_hit.summary().today_is_school_holiday);
  assert(std::string(sch_hit.summary().school_note) == "Autumn Half Term Holiday");

  // Exclusive DTEND = Oct 31 -> Oct 31 itself is NOT inside the Oct 26..31 all-day span!
  IcsStreamParser sch_end(2026, 10, 31, true);
  sch_end.feed(kSchoolIcs, strlen(kSchoolIcs));
  sch_end.finish();
  assert(!sch_end.summary().today_is_school_holiday);

  printf("ALL CLOCK_MATH & ICS_PARSER UNIT TESTS PASSED!\n");
  return 0;
}
