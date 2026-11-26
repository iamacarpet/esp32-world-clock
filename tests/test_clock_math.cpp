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

  // 7. v1.1 Symmetric School-Night vs Non-School-Night Bedtime (18:30/19:30/04:00/06:00 schedule)
  ScheduleConfig sym;
  sym.winddown_enabled = true;
  sym.sleep_enabled = true;
  sym.quiet_enabled = true;
  sym.weekend_liein_enabled = true;
  sym.non_school_bedtime_enabled = true;
  sym.winddown_m = 18 * 60 + 30;  // 18:30
  sym.bedtime_m = 19 * 60 + 30;   // 19:30
  sym.quiet_m = 4 * 60;           // 04:00
  sym.wakeup_m = 6 * 60;          // 06:00
  sym.weekend_offset_m = 60;      // +60m morning lie-in -> 05:00 quiet / 07:00 wake
  sym.non_school_bedtime_m = 30;  // +30m late bedtime -> 19:00 wind-down / 20:00 bed

  // Friday night (today=school day, tomorrow=Sat non-school): late bedtime applies!
  assert(evaluate_schedule(18 * 60 + 45, sym, false, false, true) == 0);  // Still Day at 18:45
  assert(evaluate_schedule(19 * 60 + 15, sym, false, false, true) == 3);  // Wind-down 19:00..20:00
  assert(evaluate_schedule(20 * 60, sym, false, false, true) == 1);       // Bedtime 20:00

  // Sunday (today=Sun non-school, tomorrow=Mon school day): morning lie-in AND early school-night bedtime!
  assert(evaluate_schedule(6 * 60 + 30, sym, false, true, false) == 2);   // Sunday morning lie-in (still Quiet Play!)
  assert(evaluate_schedule(18 * 60 + 45, sym, false, true, false) == 3);  // Sunday evening: early 18:30 wind-down!
  assert(evaluate_schedule(19 * 60 + 30, sym, false, true, false) == 1);  // Sunday evening: early 19:30 bedtime!

  // Sunday before Monday Inset Day (today=Sun, tomorrow=Mon holiday): BOTH morning lie-in AND late bedtime!
  assert(evaluate_schedule(18 * 60 + 45, sym, false, true, true) == 0);   // Still Day on Sunday before Inset Day!
  assert(evaluate_schedule(19 * 60 + 45, sym, false, true, true) == 3);   // Wind-down at 19:45!

  // 8. v1.1 Multi-Event Priority Engine + Auto School Holiday & Back-to-School Countdowns
  // Before Half Term (Oct 18, 8 days before Oct 26 Half Term; Oct 25 is eve of Half Term!)
  IcsStreamParser sch_pre(2026, 10, 18, true);
  sch_pre.feed(kSchoolIcs, strlen(kSchoolIcs));
  sch_pre.finish();
  assert(!sch_pre.summary().today_is_school_holiday);
  assert(!sch_pre.summary().tomorrow_is_school_holiday);
  assert(sch_pre.summary().next_school_holiday_days == 8);

  IcsStreamParser sch_eve(2026, 10, 25, true);
  sch_eve.feed(kSchoolIcs, strlen(kSchoolIcs));
  sch_eve.finish();
  assert(!sch_eve.summary().today_is_school_holiday);
  assert(sch_eve.summary().tomorrow_is_school_holiday);  // Sunday before Half Term -> late bedtime!

  // During Half Term (Wed Oct 28): skips Sat Oct 31 & Sun Nov 1 -> 5 sleeps until Mon Nov 2 back to school!
  assert(sch_hit.summary().back_to_school_days == 5);
  auto cv_hol = build_calendar_view(IcsFeedSummary{}, sch_hit.summary(), true, false, 9 * 60, 0, 0, false, "", -1);
  assert(cv_hol.school_badge.find("NO SCHOOL") != std::string::npos);
  assert(cv_hol.holiday_badge == "5 SLEEPS TO BACK TO SCHOOL");

  // Multi-event priority & timed-event countdown + 30m auto-expiry
  auto cv_imminent = build_calendar_view(fam.summary(), sch_pre.summary(), true, false, 15 * 60 + 30, 0, 0, false, "", -1);
  assert(cv_imminent.headline.find("SWIMMING LESSON IN 30 MIN!") != std::string::npos);
  assert(cv_imminent.holiday_badge.find("8 SLEEPS TO") != std::string::npos);

  // Countdowns First (mode 2) highlights the priority Disney Holiday countdown when timed event isn't imminent
  auto cv_cd = build_calendar_view(fam.summary(), sch_pre.summary(), true, false, 10 * 60, 2, 0, false, "", -1);
  assert(cv_cd.headline.find("59 SLEEPS TO DISNEY HOLIDAY") != std::string::npos);

  // After 16:35 (>30m past 16:00 Swimming), timed event auto-expires and does not clutter headline!
  auto cv_after = build_calendar_view(fam.summary(), sch_pre.summary(), true, false, 16 * 60 + 35, 1, 0, false, "", -1);
  assert(cv_after.headline.find("SWIMMING") == std::string::npos);

  // Weather prep hint check
  assert(weather_prep_hint("rainy", 9.0f, false) == "Warm Raincoat & Umbrella");
  assert(weather_prep_hint("sunny", 24.0f, false) == "Sun Hat, Suncream & Water");

  // 9. v1.2 Overnight Circadian Continuity Across Midnight (Fri 20:15 -> Sat 00:15, Sun 20:15 -> Mon 00:15, & Post-Midnight Wrap)
  // Friday night (today=Fri false, tomorrow=Sat true): at 20:15, {quiet} is ALREADY Sat's 05:00 (+60m lie-in)
  assert(effective_cycle_quiet_m(20 * 60 + 15, sym, false, true) == 5 * 60);
  assert(effective_cycle_quiet_m(0 * 60 + 15, sym, true, true) == 5 * 60);  // Sat 00:15 matches Fri 20:15!
  float fri_2359 = sleep_phase_remaining_01(23 * 60 + 59, 1, sym, false, false, true, false);
  float sat_0000 = sleep_phase_remaining_01(0, 1, sym, false, true, true, false);
  assert(fri_2359 > sat_0000 && (fri_2359 - sat_0000) < 0.005f);  // Perfectly continuous across midnight!

  // Sunday night (today=Sun true, tomorrow=Mon false): at 20:15, {quiet} is ALREADY Mon's early 04:00 (no lie-in)
  assert(effective_cycle_quiet_m(20 * 60 + 15, sym, true, false) == 4 * 60);
  assert(effective_cycle_quiet_m(0 * 60 + 15, sym, false, false) == 4 * 60);  // Mon 00:15 matches Sun 20:15!
  float sun_2359 = sleep_phase_remaining_01(23 * 60 + 59, 1, sym, false, true, false, false);
  float mon_0000 = sleep_phase_remaining_01(0, 1, sym, false, false, false, false);
  assert(sun_2359 > mon_0000 && (sun_2359 - mon_0000) < 0.005f);

  // Post-midnight bedtime wrap (23:30 + 60m = 00:30 on NYE -> Jan 1 00:15 still Wind-Down via is_yesterday_special_night!)
  ScheduleConfig wrap_cfg;
  wrap_cfg.winddown_enabled = true;
  wrap_cfg.sleep_enabled = true;
  wrap_cfg.quiet_enabled = false;
  wrap_cfg.winddown_m = 23 * 60;      // 23:00
  wrap_cfg.bedtime_m = 23 * 60 + 30;  // 23:30
  wrap_cfg.wakeup_m = 7 * 60;         // 07:00
  wrap_cfg.special_delay_m = 60;      // NYE -> 00:00 wind-down, 00:30 bedtime!
  assert(evaluate_schedule(23 * 60 + 45, wrap_cfg, true, false, false, false) == 0);  // Dec 31 23:45 still Day
  assert(evaluate_schedule(0 * 60 + 15, wrap_cfg, false, false, false, true) == 3);   // Jan 1 00:15 Wind-Down!
  assert(evaluate_schedule(0 * 60 + 30, wrap_cfg, false, false, false, true) == 1);   // Jan 1 00:30 Deep Sleep!

  // 1st-of-month birthday countdown & year rollover (Dec 25 -> Jan 1 = 7 sleeps!)
  assert(days_until_next_birthday(2026, 12, 25, 1, 1) == 7);
  assert(days_until_next_birthday(2026, 5, 25, 6, 1) == 7);
  assert(days_until_next_birthday(2026, 6, 1, 6, 1) == 0);

  // 10. v1.2 Security: HTTPS-only URL Normalization, SSRF Rejection, Bounded JSON & Random Password
  assert(!normalize_calendar_url("http://example.com/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://localhost/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://192.168.1.50/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://10.0.0.1/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://169.254.169.254/latest/meta-data", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://[::1]/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://[fd00::1]/cal.ics", norm, sizeof(norm)));
  assert(!normalize_calendar_url("https://user:pass@calendar.google.com/a.ics", norm, sizeof(norm)));
  assert(normalize_calendar_url("https://calendar.google.com/calendar/ical/abc/basic.ics", norm, sizeof(norm)));

  // Bounded JSON extraction (missing key in "current" must NOT scan into "daily"!)
  const char *kWxJson = "{\"current\":{\"temperature_2m\":12.5},\"daily\":{\"apparent_temperature\":99.0}}";
  const char *cur_s = strstr(kWxJson, "\"current\"");
  const char *cur_e = find_json_object_end(cur_s);
  assert(cur_e != nullptr);
  float tmp_v = 0.0f;
  assert(extract_json_num_bounded(cur_s, cur_e, "\"temperature_2m\"", tmp_v) && fabsf(tmp_v - 12.5f) < 1e-3f);
  assert(!extract_json_num_bounded(cur_s, cur_e, "\"apparent_temperature\"", tmp_v));
  assert(json_key_equals_str("{\"visibility\": \"eclipsed\"}", "\"visibility\"", "eclipsed"));
  assert(!json_key_equals_str("{\"visibility\": \"daylight\", \"note\": \"eclipsed\"}", "\"visibility\"", "eclipsed"));

  // Random setup password format (8 lowercase unambiguous alphanumeric chars)
  std::string rp1 = format_random_setup_password(0x123456789ABCDEF0ULL);
  std::string rp2 = format_random_setup_password(0xFEDCBA9876543210ULL);
  assert(rp1.size() == 8 && rp2.size() == 8 && rp1 != rp2);

  // 11. v1.2 RFC 5545 .ics Edge Cases: UTC 'Z' Conversion, Quote-Aware Colons, STATUS:CANCELLED,
  //     Smart Quotes/Dashes, Midnight 00:00 Timed Event, Multi-Day Timed Event, Keyword Tightening,
  //     1-Sleep Countdown & Adjacent Half-Term + Monday INSET Day 64-Day Bitmask!
  const char *kEdgeIcs =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260715T150000Z\r\n"
      "SUMMARY;ALTREP=\"https://school.org/info\":Swimming Lesson\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260715T120000Z\r\n"
      "STATUS:CANCELLED\r\n"
      "SUMMARY:Cancelled Dentist\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260715T000000\r\n"
      "SUMMARY:Leo\xE2\x80\x99s Party \xE2\x80\x93 3pm\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260714T160000\r\n"
      "DTEND:20260716T120000\r\n"
      "SUMMARY:Camping Weekend\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20260716\r\n"
      "SUMMARY:Countdown: Holiday Flight\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";

  // BST (+60 min): 15:00Z -> 16:00 (4 PM); 00:00 stays timed; Camping Weekend (started Jul 14) is allday on Jul 15!
  IcsStreamParser bst(2026, 7, 15, false, +60);
  bst.feed(kEdgeIcs, strlen(kEdgeIcs));
  bst.finish();
  assert(bst.summary().valid);
  assert(bst.summary().today_timed[0].valid && bst.summary().today_timed[0].minute_of_day == 0);
  assert(std::string(bst.summary().today_timed[0].title) == "Leo's Party - 3pm");
  assert(bst.summary().today_timed[1].valid && bst.summary().today_timed[1].minute_of_day == 16 * 60);
  assert(std::string(bst.summary().today_timed[1].title) == "Swimming Lesson");
  // Multi-day timed event does NOT repeat 16:00 on Day 2 (classified into today_allday!)
  assert(std::string(bst.summary().today_allday) == "Camping Weekend");
  // 1-sleep priority countdown works!
  assert(bst.summary().countdowns[0].valid && bst.summary().countdowns[0].days_away == 1);
  assert(std::string(bst.summary().countdowns[0].title) == "Holiday Flight");

  // US EDT (-240 min): 15:00Z -> 11:00 AM, and 02:00Z on Jul 16 shifts back to 22:00 on Jul 15!
  const char *kUtcShiftIcs =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260715T150000Z\r\n"
      "SUMMARY:Morning Club\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART:20260716T020000Z\r\n"
      "SUMMARY:Late Match\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  IcsStreamParser edt(2026, 7, 15, false, -240);
  edt.feed(kUtcShiftIcs, strlen(kUtcShiftIcs));
  edt.finish();
  assert(edt.summary().today_timed[0].valid && edt.summary().today_timed[0].minute_of_day == 11 * 60);
  assert(edt.summary().today_timed[1].valid && edt.summary().today_timed[1].minute_of_day == 22 * 60);
  assert(std::string(edt.summary().today_timed[1].title) == "Late Match");

  // Tightened holiday keywords + Adjacent Half Term (Oct 26..30) + Monday INSET (Nov 2) -> Back to school Tue Nov 3!
  const char *kAdjSchoolIcs =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261028\r\n"
      "SUMMARY:After-School Club Booking Closed\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261102\r\n"
      "SUMMARY:INSET Day\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261026\r\n"
      "DTEND;VALUE=DATE:20261031\r\n"
      "SUMMARY:Autumn Half Term\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  // Notice VEVENTs are deliberately out-of-order (Nov 2 INSET before Oct 26..30 Half Term)!
  // On Wed Oct 28: Oct 29-30 (Hol), Oct 31-Nov 1 (Sat/Sun), Nov 2 (INSET) -> First school day is Tue Nov 3 (6 sleeps)!
  IcsStreamParser sch_adj(2026, 10, 28, true);
  sch_adj.feed(kAdjSchoolIcs, strlen(kAdjSchoolIcs));
  sch_adj.finish();
  assert(sch_adj.summary().today_is_school_holiday);
  assert(sch_adj.summary().back_to_school_days == 6);

  // "After-School Club Booking Closed" alone must NOT trigger a school holiday!
  const char *kFalsePosIcs =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261015\r\n"
      "SUMMARY:After-School Club Booking Closed\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "DTSTART;VALUE=DATE:20261015\r\n"
      "SUMMARY:Holiday Club Deposit Deadline\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  IcsStreamParser sch_fp(2026, 10, 15, true);
  sch_fp.feed(kFalsePosIcs, strlen(kFalsePosIcs));
  sch_fp.finish();
  assert(!sch_fp.summary().today_is_school_holiday);

  // Accented UTF-8 uppercasing
  assert(to_upper_ascii("Caf\xC3\xA9") == "CAF\xC3\x89");

  // 12. v1.2.1 Polish Regressions: R-01 reset() after redirect HTML, R-03 WEEKLY INTERVAL & BYDAY + UTC UNTIL,
  //     and Dual-Calendar Merge (School non-holiday events + "Cancelled!" skip + Family [holiday] + School INSET merge!)
  {
    // R-01: Garbage redirect HTML without newline before reset() must NOT corrupt subsequent valid ICS!
    IcsStreamParser redir(2026, 12, 11, true);
    const char *html_302 = "<html><body>Moved Permanently</body></html>";
    redir.feed(html_302, strlen(html_302));
    redir.reset();
    const char *school_ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;TZID=Europe/London;VALUE=DATE:20261211\r\n"
        "DTEND;TZID=Europe/London;VALUE=DATE:20261212\r\n"
        "SUMMARY;LANGUAGE=en-gb:Christmas Dinner Day\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;TZID=Europe/London:20261211T090000\r\n"
        "SUMMARY;LANGUAGE=en-gb:Cancelled! - Car Boot Sale\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    redir.feed(school_ics, strlen(school_ics));
    redir.finish();
    assert(redir.summary().valid);
    assert(!redir.summary().today_is_school_holiday);
    assert(std::string(redir.summary().today_allday) == "Christmas Dinner Day");
    assert(!redir.summary().today_timed[0].valid);  // "Cancelled! - Car Boot Sale" skipped!

    // Dual-calendar build_calendar_view surfaces School non-holiday event when Family feed is empty!
    auto cv_sch = build_calendar_view(IcsFeedSummary{}, redir.summary(), true, false, 8 * 60, 0, 0, false, "", -1);
    assert(cv_sch.headline.find("CHRISTMAS DINNER DAY") != std::string::npos);
    assert(cv_sch.plan_lines[0] == "CHRISTMAS DINNER DAY");
  }

  {
    // Cross-feed holiday bitmask merge: Family Calendar has Mon-Fri Half Term [holiday] (Oct 26..30),
    // School Calendar has adjacent Monday INSET (Nov 2) -> on Wed Oct 28, merged Back-to-School = 6 sleeps (Tue Nov 3)!
    const char *fam_hol =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20261026\r\n"
        "DTEND;VALUE=DATE:20261031\r\n"
        "SUMMARY:Autumn Half Term [holiday]\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    const char *sch_inset =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART;VALUE=DATE:20261102\r\n"
        "DTEND;VALUE=DATE:20261103\r\n"
        "SUMMARY:INSET\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    IcsStreamParser pf(2026, 10, 28, false);
    pf.feed(fam_hol, strlen(fam_hol));
    pf.finish();
    IcsStreamParser ps(2026, 10, 28, true);
    ps.feed(sch_inset, strlen(sch_inset));
    ps.finish();
    auto cv_m = build_calendar_view(pf.summary(), ps.summary(), true, false, 9 * 60, 0, 0, false, "", -1);
    assert(cv_m.school_badge.find("NO SCHOOL") != std::string::npos);
    assert(cv_m.holiday_badge == "6 SLEEPS TO BACK TO SCHOOL");
  }

  {
    // R-03: FREQ=WEEKLY;INTERVAL=2 (fortnightly Mon starting Oct 5 -> hits Oct 19, skips Oct 12!)
    //       + FREQ=WEEKLY;BYDAY=MO,WE,FR (starting Mon Oct 5 -> hits Wed Oct 14!)
    //       + UTC UNTIL=20261020T010000Z in US EDT (-240m) -> Oct 19 21:00 local, so Oct 19 hits & Oct 20 does NOT!
    const char *rec_ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:20261005T160000\r\n"
        "RRULE:FREQ=WEEKLY;INTERVAL=2;UNTIL=20261020T010000Z\r\n"
        "SUMMARY:Fortnightly Piano\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:20261005T153000\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR\r\n"
        "SUMMARY:Swim Squad\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    // Week 1 Monday (Oct 12): Fortnightly Piano MUST NOT fire; Swim Squad (MO) MUST fire!
    IcsStreamParser w1_mon(2026, 10, 12, false, -240);
    w1_mon.feed(rec_ics, strlen(rec_ics));
    w1_mon.finish();
    assert(w1_mon.summary().today_timed[0].valid && std::string(w1_mon.summary().today_timed[0].title) == "Swim Squad");
    assert(!w1_mon.summary().today_timed[1].valid);

    // Week 1 Wednesday (Oct 14): Swim Squad (WE) MUST fire!
    IcsStreamParser w1_wed(2026, 10, 14, false, -240);
    w1_wed.feed(rec_ics, strlen(rec_ics));
    w1_wed.finish();
    assert(w1_wed.summary().today_timed[0].valid && std::string(w1_wed.summary().today_timed[0].title) == "Swim Squad");

    // Week 2 Monday (Oct 19): BOTH Swim Squad (15:30) AND Fortnightly Piano (16:00, on UNTIL local day Oct 19!) fire!
    IcsStreamParser w2_mon(2026, 10, 19, false, -240);
    w2_mon.feed(rec_ics, strlen(rec_ics));
    w2_mon.finish();
    assert(w2_mon.summary().today_timed[0].valid && std::string(w2_mon.summary().today_timed[0].title) == "Swim Squad");
    assert(w2_mon.summary().today_timed[1].valid && std::string(w2_mon.summary().today_timed[1].title) == "Fortnightly Piano");

    // Week 4 Monday (Nov 2): Past UNTIL -> Fortnightly Piano expired; Swim Squad still active!
    IcsStreamParser w4_mon(2026, 11, 2, false, -240);
    w4_mon.feed(rec_ics, strlen(rec_ics));
    w4_mon.finish();
    assert(w4_mon.summary().today_timed[0].valid && std::string(w4_mon.summary().today_timed[0].title) == "Swim Squad");
    assert(!w4_mon.summary().today_timed[1].valid);
  }

  // 13. v1.3.0 Whole-Day "PACK & WEAR FOR TODAY" Briefing (Zero Redundancy with Current Weather Dock!)
  {
    // Dry morning right now (partlycloudy 9C) BUT 75% rain forecast later -> warns to pack raincoat!
    auto b1 = build_day_prep_briefing("partlycloudy", "rainy", 9.0f, 12.0f, 5.0f, 75, false);
    assert(b1.headline == "Dry Now · Rain Later (75%)!");
    assert(b1.pack_hint == "Pack Warm Raincoat & Umbrella");

    // Chilly summer morning (11C) climbing to hot afternoon (25C) -> layers + sun hat & water!
    auto b2 = build_day_prep_briefing("sunny", "sunny", 11.0f, 25.0f, 9.0f, 10, false);
    assert(b2.headline.find("Cool AM") != std::string::npos && b2.headline.find("Hot PM") != std::string::npos);
    assert(b2.pack_hint == "Layers + Sun Hat & Water");

    // Cool morning (10C) climbing to warm afternoon (21C) -> Jumper now, T-Shirt later!
    auto b3 = build_day_prep_briefing("sunny", "partlycloudy", 10.0f, 21.0f, 8.0f, 15, false);
    assert(b3.headline.find("Warm PM") != std::string::npos);
    assert(b3.pack_hint == "Layers: Jumper Now, T-Shirt Later");

    // Freezing all day (High 5C) in Fahrenheit -> 41°F
    auto b4 = build_day_prep_briefing("cloudy", "cloudy", 2.0f, 5.0f, 0.0f, 10, true);
    assert(b4.headline == "Cold All Day (High 41°F)");
    assert(b4.pack_hint == "Big Coat, Hat & Gloves");
  }

  // 14. v1.3.1 Polish: HA TODAY,<cond>,<precip_pct>| forecast payload + RFC 5545 EXDATE exclusions
  {
    // Legacy 3-day HA string stays untouched and does not overwrite today's fields
    std::string out3, tcond = "sunny";
    int tpct = 10;
    parse_ha_forecast_payload("FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7", out3, tcond, tpct);
    assert(out3 == "FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7");
    assert(tcond == "sunny" && tpct == 10);

    // v1.3.1 optional leading TODAY,<cond>,<precip_pct>| unlocks "Dry Now · Rain Later (80%)!" on HA clocks
    parse_ha_forecast_payload("TODAY,rainy,80|FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7", out3, tcond, tpct);
    assert(out3 == "FRI,cloudy,11,6|SAT,rainy,10,4|SUN,sunny,13,7");
    assert(tcond == "rainy" && tpct == 80);
    auto b_ha = build_day_prep_briefing("partlycloudy", tcond, 10.0f, 12.0f, 6.0f, tpct, false);
    assert(b_ha.headline == "Dry Now · Rain Later (80%)!");

    // EXDATE suppresses single cancelled week of a weekly club (including comma-separated + UTC Z dates)
    const char *exdate_ics =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTART:20261005T160000\r\n"
        "RRULE:FREQ=WEEKLY\r\n"
        "EXDATE;TZID=Europe/London:20261012T160000,20261019T160000\r\n"
        "SUMMARY:Swimming Club\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    // Mon Oct 12 (excluded via EXDATE) -> MUST NOT fire today; next upcoming occurrence is Oct 26 (14 days away)!
    IcsStreamParser ex_skip(2026, 10, 12, false, 60);
    ex_skip.feed(exdate_ics, strlen(exdate_ics));
    ex_skip.finish();
    assert(!ex_skip.summary().today_timed[0].valid);
    assert(ex_skip.summary().countdowns[0].valid && ex_skip.summary().countdowns[0].days_away == 14);

    // Mon Oct 26 (not excluded) -> fires normally at 16:00!
    IcsStreamParser ex_hit(2026, 10, 26, false, 60);
    ex_hit.feed(exdate_ics, strlen(exdate_ics));
    ex_hit.finish();
    assert(ex_hit.summary().today_timed[0].valid && std::string(ex_hit.summary().today_timed[0].title) == "Swimming Club");
  }

  printf("ALL CLOCK_MATH & ICS_PARSER UNIT TESTS PASSED!\n");
  return 0;
}
