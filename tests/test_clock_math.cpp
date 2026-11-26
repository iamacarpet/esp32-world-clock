#include "../components/world_clock/clock_math.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <string>

using namespace world_clock;

int main() {
  // 1. 4-Phase Schedule Tests
  ScheduleConfig cfg;
  cfg.winddown_enabled = true;
  cfg.sleep_enabled = true;
  cfg.quiet_enabled = true;
  cfg.winddown_m = 19 * 60;       // 19:00
  cfg.bedtime_m = 19 * 60 + 30;   // 19:30
  cfg.quiet_m = 4 * 60;           // 04:00
  cfg.wakeup_m = 6 * 60;          // 06:00
  cfg.special_delay_m = 30;

  assert(evaluate_schedule(12 * 60, cfg, false) == 0);       // Noon -> Day
  assert(evaluate_schedule(18 * 60 + 59, cfg, false) == 0);  // 18:59 -> Day
  assert(evaluate_schedule(19 * 60, cfg, false) == 3);       // 19:00 -> Wind-Down
  assert(evaluate_schedule(19 * 60 + 29, cfg, false) == 3);  // 19:29 -> Wind-Down
  assert(evaluate_schedule(19 * 60 + 30, cfg, false) == 1);  // 19:30 -> Deep Sleep
  assert(evaluate_schedule(2 * 60, cfg, false) == 1);        // 02:00 -> Deep Sleep
  assert(evaluate_schedule(3 * 60 + 59, cfg, false) == 1);   // 03:59 -> Deep Sleep
  assert(evaluate_schedule(4 * 60, cfg, false) == 2);        // 04:00 -> Morning Quiet Play
  assert(evaluate_schedule(5 * 60 + 59, cfg, false) == 2);   // 05:59 -> Morning Quiet Play
  assert(evaluate_schedule(6 * 60, cfg, false) == 0);        // 06:00 -> Day

  // Special night +30m delay (19:30 wind-down -> 20:00 sleep)
  assert(evaluate_schedule(19 * 60 + 15, cfg, true) == 0);   // 19:15 -> Day
  assert(evaluate_schedule(19 * 60 + 45, cfg, true) == 3);   // 19:45 -> Wind-Down
  assert(evaluate_schedule(20 * 60, cfg, true) == 1);        // 20:00 -> Deep Sleep

  // Quiet Play disabled -> Deep Sleep runs straight to wakeup_m (06:00)
  ScheduleConfig no_quiet = cfg;
  no_quiet.quiet_enabled = false;
  assert(evaluate_schedule(4 * 60 + 30, no_quiet, false) == 1);
  assert(evaluate_schedule(6 * 60, no_quiet, false) == 0);

  // Wind-Down disabled -> Day goes straight to Deep Sleep at 19:30
  ScheduleConfig no_wd = cfg;
  no_wd.winddown_enabled = false;
  assert(evaluate_schedule(19 * 60 + 15, no_wd, false) == 0);
  assert(evaluate_schedule(19 * 60 + 30, no_wd, false) == 1);

  // Quiet time crossing midnight (23:30 -> 06:00)
  ScheduleConfig mid_q = cfg;
  mid_q.bedtime_m = 20 * 60;
  mid_q.quiet_m = 23 * 60 + 30;
  mid_q.wakeup_m = 6 * 60;
  assert(evaluate_schedule(22 * 60, mid_q, false) == 1);
  assert(evaluate_schedule(23 * 60 + 45, mid_q, false) == 2);
  assert(evaluate_schedule(1 * 60, mid_q, false) == 2);

  // All phases disabled (Adult Office Day-Only mode)
  ScheduleConfig office_cfg{false, false, false, 0, 0, 0, 0, 0};
  assert(evaluate_schedule(2 * 60, office_cfg, false) == 0);
  assert(evaluate_schedule(19 * 60 + 15, office_cfg, false) == 0);

  // 2. Exact Easter Boundary Tests (2024: Mar 31, 2029: Apr 1, 2026: Apr 5)
  assert(is_easter_window(2024, 3, 30));  // Easter Saturday
  assert(is_easter_window(2024, 3, 31));  // Easter Sunday
  assert(is_easter_window(2024, 4, 1));   // Easter Monday across Mar/Apr boundary!
  assert(!is_easter_window(2024, 4, 2));
  assert(is_easter_window(2029, 3, 31));  // Easter Saturday across Mar/Apr boundary!
  assert(is_easter_window(2029, 4, 1));   // Easter Sunday
  assert(is_easter_window(2029, 4, 2));   // Easter Monday
  assert(is_easter_window(2026, 4, 5));

  // 3. Real Lunar Phase Tests
  // 2026-11-24 is Full Moon (~0.50); 2026-11-09 is New Moon (~0.00 or ~0.99)
  float p_full = lunar_phase_01(2026, 11, 24);
  assert(p_full > 0.43f && p_full < 0.57f);
  float p_new = lunar_phase_01(2026, 11, 9);
  assert(p_new < 0.08f || p_new > 0.92f);

  // 4. Dynamic Time & Token Expansion Tests
  assert(format_short_time(4, 0, false) == "4 AM");
  assert(format_short_time(19, 30, false) == "7:30 PM");
  assert(format_short_time(4, 0, true) == "04:00");
  assert(format_short_time(19, 30, true) == "19:30");

  TokenContext ctx{"LEO", "7 PM", "7:30 PM", "4:30 AM", "6 AM"};
  assert(expand_tokens("BEFORE {quiet} · SLEEP TIME", ctx) == "BEFORE 4:30 AM · SLEEP TIME");
  assert(expand_tokens("GOOD MORNING, {name}!", ctx) == "GOOD MORNING, LEO!");

  // 5. UTF-8 Codepoint Truncation (never splits multi-byte chars!)
  std::string adv = "ADVENT \uEE03\uEE04\uEE04\uEE04\uEE04\uEE01\uEE01\uEE01\uEE01\uEE02 16 SLEEPS TO XMAS!";
  std::string adv_orig = adv;
  utf8_truncate_codepoints(adv, 44);
  assert(adv == adv_orig);  // 36 codepoints (56 bytes) must NOT be truncated!

  std::string long_utf8 = "HELLO \u2192 WORLD \u2192 THIS IS A VERY LONG CUSTOM TICKER MESSAGE";
  utf8_truncate_codepoints(long_utf8, 15);
  assert(long_utf8 == "HELLO \u2192 WORLD \u2192");

  // 6. JSON & Unit Helpers
  float v = 0.0f;
  assert(extract_json_num("{\"latitude\": \"51.5074\", \"longitude\": \"-0.1278\"}", "\"latitude\"", v));
  assert(fabsf(v - 51.5074f) < 1e-4f);
  float arr[4] = {};
  assert(parse_csv_floats(": [10.5, 11.2, -1.5, 8.0]", arr, 4));
  assert(fabsf(arr[2] - (-1.5f)) < 1e-4f);
  assert(std::string(ymd_to_dow(2026, 11, 26)) == "THU");
  assert(fabsf(c_to_f(0.0f) - 32.0f) < 1e-4f);
  assert(km_to_mi(100) == 62);

  printf("ALL CLOCK_MATH UNIT TESTS PASSED!\n");
  return 0;
}
