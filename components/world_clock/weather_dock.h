#pragma once

#include "clock_math.h"
#include "world_map.h"
#include <lvgl.h>
#include "esphome/core/application.h"

namespace world_clock {

struct WeatherDockWidgets {
  lv_obj_t *dock_canvas{nullptr};
  lv_obj_t *fc_day[3]{nullptr, nullptr, nullptr};
  lv_obj_t *fc_hl[3]{nullptr, nullptr, nullptr};
  lv_obj_t *card_title{nullptr};
  lv_obj_t *card_sub{nullptr};
};

struct WeatherDockParams {
  uint8_t pal{0};
  uint8_t sleep_phase{0};  // 0=Day, 1=Sleep, 2=Morning Quiet, 3=Evening Wind-Down
  uint8_t mode{1};
  std::string cur_cond{"cloudy"};
  std::string raw_3day;
  std::string child_name{"LEO"};
  int birth_year{2019};
  int loc_year{2026};
  int loc_month{1};
  int loc_day{1};
  bool loc_valid{false};
  bool use_fahrenheit{false};
  bool use_miles{false};
  bool keep_santa_winddown{true};
  int santa_wp_idx{-1};
  std::string santa_city{"NORTH POLE"};
  int santa_dist_km{0};
  int santa_gifts_m{0};
  std::string sleep_title;
  std::string sleep_sub;
  std::string quiet_title;
  std::string quiet_sub;
  std::string winddown_title;
  std::string winddown_sub;
  std::string xmas_title;
  std::string xmas_sub;
  float sleep_remaining_01{-1.0f};  // 0..1 remaining fraction -> draws 5 countdown stars
  bool tooth_fairy_mode{false};
  bool nap_active{false};
  // Prominent Band 3 Calendar Card (when Calendar Display Style == Prominent Dock Card)
  bool prominent_cal_active{false};
  std::string prominent_cal_title;
  std::string prominent_cal_sub;
  // Teen / Study Desk Focus Pomodoro Timer (takes over right Band 3 card when active)
  bool focus_active{false};
  bool focus_is_break{false};
  float focus_remaining_01{-1.0f};
  std::string focus_title;
  std::string focus_sub;
};

inline int map_wx_code(std::string s) {
  for (char &c : s) c = (char)tolower((unsigned char)c);
  if (s.find("sunny") != std::string::npos) return 1;
  if (s.find("clear") != std::string::npos) return 6;
  if (s.find("partly") != std::string::npos) return 2;
  if (s.find("lightning") != std::string::npos || s.find("storm") != std::string::npos) return 4;
  if (s.find("snow") != std::string::npos || s.find("sleet") != std::string::npos ||
      s.find("hail") != std::string::npos)
    return 5;
  if (s.find("rain") != std::string::npos || s.find("pour") != std::string::npos) return 3;
  if (s.find("fog") != std::string::npos || s.find("mist") != std::string::npos) return 7;
  return 0;
}

inline void render_weather_dock_canvas(const WeatherDockWidgets &w, uint8_t *&bg_cache,
                                       const WeatherDockParams &p) {
  lv_obj_t *cv = w.dock_canvas;
  if (cv == nullptr) return;
  esphome::App.feed_wdt();

  uint8_t *bg = ensure_bg_cache(bg_cache);
  if (bg == nullptr) return;
  static_assert(kScratchBytes >= 64UL * 64UL * 3UL, "kScratchBytes too small for 64x64 weather dock icon pad");
  uint8_t *s_pad = bg + kBgBytes;

#if LVGL_VERSION_MAJOR >= 9
  lv_draw_buf_t *db_dk = lv_canvas_get_draw_buf(cv);
  bool fast_dk = (db_dk != nullptr && db_dk->data != nullptr && db_dk->header.cf == LV_COLOR_FORMAT_RGB565);
#else
  lv_img_dsc_t *dsc_dk = lv_canvas_get_img(cv);
  lv_color_t *raw_dk = (dsc_dk != nullptr && dsc_dk->data != nullptr && dsc_dk->header.cf == LV_IMG_CF_TRUE_COLOR)
                           ? (lv_color_t *)const_cast<uint8_t *>(dsc_dk->data)
                           : nullptr;
#endif

  auto put_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= 800 || y < 0 || y >= 80) return;
#if LVGL_VERSION_MAJOR >= 9
    if (fast_dk) {
      uint16_t *row16 = (uint16_t *)(db_dk->data + (size_t)y * db_dk->header.stride);
      row16[x] = ((uint16_t)(r & 0xF8U) << 8) | ((uint16_t)(g & 0xFCU) << 3) | ((uint16_t)b >> 3);
    } else {
      lv_canvas_set_px(cv, x, y, lv_color_make(r, g, b), LV_OPA_COVER);
    }
#else
    if (raw_dk != nullptr) {
      raw_dk[(size_t)y * 800UL + (size_t)x] = lv_color_make(r, g, b);
    } else {
      lv_canvas_set_px_color(cv, x, y, lv_color_make(r, g, b));
    }
#endif
  };

  auto set_hidden = [](lv_obj_t *o, bool hide) {
    if (o == nullptr) return;
    if (hide) {
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
#if LVGL_VERSION_MAJOR >= 9
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
#else
      lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
#endif
    }
  };

#if LVGL_VERSION_MAJOR >= 9
  if (fast_dk) {
    memset(db_dk->data, 0, (size_t)db_dk->header.stride * 80UL);
  } else {
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
  }
#else
  if (raw_dk != nullptr) {
    memset(raw_dk, 0, 800UL * 80UL * sizeof(lv_color_t));
  } else {
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
  }
#endif

  const uint8_t pal = p.pal;
  const uint8_t slp = p.sleep_phase;
  const uint8_t mode = p.mode;
  const bool show_card =
      (slp != 0) || (mode == 2 || mode == 4 || mode == 5 || mode == 6 || mode == 7 || mode == 8 || mode == 9) ||
      p.focus_active || p.prominent_cal_active;

  for (int i = 0; i < 3; i++) {
    set_hidden(w.fc_day[i], show_card);
    set_hidden(w.fc_hl[i], show_card);
  }
  set_hidden(w.card_title, !show_card);
  set_hidden(w.card_sub, !show_card);

  uint8_t div_r = (pal == 1) ? 42 : ((pal == 2) ? 58 : 34);
  uint8_t div_g = (pal == 1) ? 5 : ((pal == 2) ? 24 : 37);
  uint8_t div_b = (pal == 1) ? 9 : ((pal == 2) ? 4 : 44);
  for (int y = 12; y <= 68; y++) {
    put_px(398, y, div_r, div_g, div_b);
    if (!show_card) {
      put_px(534, y, div_r, div_g, div_b);
      put_px(666, y, div_r, div_g, div_b);
    }
  }

  constexpr int kPad = 64;
  constexpr size_t kPadBytes = 64UL * 64UL * 3UL;

  auto clear_pad = [&]() { memset(s_pad, 0, kPadBytes); };
  auto pad_disc = [&](float lx, float ly, float rad, float cr, float cg, float cb, float op) {
    int x0 = std::max(0, (int)floorf(lx - rad - 1.0f));
    int x1 = std::min(kPad - 1, (int)ceilf(lx + rad + 1.0f));
    int y0 = std::max(0, (int)floorf(ly - rad - 1.0f));
    int y1 = std::min(kPad - 1, (int)ceilf(ly + rad + 1.0f));
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        float d = sqrtf(((float)x - lx) * ((float)x - lx) + ((float)y - ly) * ((float)y - ly));
        float a = std::clamp(rad + 0.5f - d, 0.0f, 1.0f) * op;
        if (a <= 0.01f) continue;
        int idx = (y * kPad + x) * 3;
        s_pad[idx] = (uint8_t)((float)s_pad[idx] * (1.0f - a) + cr * a);
        s_pad[idx + 1] = (uint8_t)((float)s_pad[idx + 1] * (1.0f - a) + cg * a);
        s_pad[idx + 2] = (uint8_t)((float)s_pad[idx + 2] * (1.0f - a) + cb * a);
      }
    }
  };
  auto pad_box = [&](int x0, int y0, int x1, int y1, uint8_t cr, uint8_t cg, uint8_t cb) {
    for (int y = std::max(0, y0); y <= std::min(kPad - 1, y1); y++) {
      for (int x = std::max(0, x0); x <= std::min(kPad - 1, x1); x++) {
        int idx = (y * kPad + x) * 3;
        s_pad[idx] = cr;
        s_pad[idx + 1] = cg;
        s_pad[idx + 2] = cb;
      }
    }
  };
  auto flush_pad = [&](int screen_cx, int screen_cy) {
    for (int py = 0; py < kPad; py++) {
      for (int px = 0; px < kPad; px++) {
        int idx = (py * kPad + px) * 3;
        if (s_pad[idx] > 0 || s_pad[idx + 1] > 0 || s_pad[idx + 2] > 0) {
          put_px(screen_cx - 32 + px, screen_cy - 32 + py, s_pad[idx], s_pad[idx + 1], s_pad[idx + 2]);
        }
      }
    }
  };

  auto paint_wx = [&](int screen_cx, int screen_cy, float sc, int code) {
    clear_pad();
    const float cx = 32.0f, cy = 32.0f;
    float sr = 255.0f, sg = (pal == 1) ? 42.0f : ((pal == 2) ? 140.0f : 188.0f), sb = (pal == 0) ? 12.0f : 14.0f;
    float cr = (pal == 1) ? 200.0f : ((pal == 2) ? 235.0f : 242.0f);
    float cg = (pal == 1) ? 22.0f : ((pal == 2) ? 115.0f : 244.0f);
    float cb = (pal == 1) ? 28.0f : ((pal == 2) ? 16.0f : 248.0f);
    float sh_r = cr * 0.65f, sh_g = cg * 0.65f, sh_b = cb * 0.65f;
    float rr = (pal == 0) ? 56.0f : 240.0f, rg = (pal == 0) ? 189.0f : ((pal == 2) ? 120.0f : 48.0f),
          rb = (pal == 0) ? 248.0f : 18.0f;

    if (code == 1 || code == 2) {
      float sx = (code == 2) ? (cx - 5.2f * sc) : cx;
      float sy = (code == 2) ? (cy - 5.2f * sc) : cy;
      float srad = (code == 2) ? (7.4f * sc) : (9.2f * sc);
      pad_disc(sx, sy, srad + 3.2f * sc, sr, sg, sb, 0.22f);
      for (int k = 0; k < 8; k++) {
        float a = (float)k * 0.785398f;
        pad_disc(sx + cosf(a) * (srad + 4.4f * sc), sy + sinf(a) * (srad + 4.4f * sc), 1.5f * sc, sr, sg, sb, 0.95f);
      }
      pad_disc(sx, sy, srad, sr, sg, sb, 1.0f);
    } else if (code == 6) {
      pad_disc(cx, cy, 8.8f * sc, cr, cg, (pal == 0) ? 168.0f : cb, 1.0f);
      pad_disc(cx + 4.0f * sc, cy - 3.0f * sc, 7.4f * sc, 0.0f, 0.0f, 0.0f, 1.0f);
      pad_disc(cx - 8.5f * sc, cy - 6.5f * sc, 1.4f * sc, sr, sg, sb, 0.95f);
    }

    if (code == 0 || code == 2 || code == 3 || code == 4 || code == 5 || code == 7) {
      float wy = (code >= 3 && code <= 5) ? (cy - 3.2f * sc) : ((code == 7) ? (cy - 4.0f * sc) : (cy + 2.2f * sc));
      for (int bx = -6; bx <= 6; bx++)
        pad_disc(cx + (float)bx * 1.1f * sc, wy + 4.6f * sc, 3.6f * sc, sh_r, sh_g, sh_b, 1.0f);
      pad_disc(cx - 6.2f * sc, wy + 1.8f * sc, 6.0f * sc, cr, cg, cb, 1.0f);
      pad_disc(cx + 6.2f * sc, wy + 2.2f * sc, 5.4f * sc, cr, cg, cb, 1.0f);
      pad_disc(cx - 0.4f * sc, wy - 2.4f * sc, 7.6f * sc, cr, cg, cb, 1.0f);
    }

    if (code == 3) {
      for (int d = -1; d <= 1; d++) {
        pad_disc(cx + (float)d * 5.8f * sc, cy + 10.5f * sc, 1.8f * sc, rr, rg, rb, 1.0f);
        pad_disc(cx + (float)d * 5.8f * sc + 0.9f * sc, cy + 8.0f * sc, 1.3f * sc, rr, rg, rb, 0.95f);
      }
    } else if (code == 4) {
      for (int s = 0; s < 7; s++)
        pad_disc(cx + (2.0f - (float)s * 0.65f) * sc, cy + (4.8f + (float)s * 1.4f) * sc, 1.6f * sc, sr, sg, sb, 1.0f);
    } else if (code == 5) {
      pad_disc(cx - 5.0f * sc, cy + 10.0f * sc, 1.9f * sc, cr, cg, cb, 1.0f);
      pad_disc(cx + 5.0f * sc, cy + 10.0f * sc, 1.9f * sc, cr, cg, cb, 1.0f);
      pad_disc(cx, cy + 12.2f * sc, 1.9f * sc, cr, cg, cb, 1.0f);
    } else if (code == 7) {
      // Distinct 3-band mist/fog stripes beneath cloud
      uint8_t fr = (uint8_t)(sh_r * 0.95f), fg = (uint8_t)(sh_g * 0.95f), fb = (uint8_t)(sh_b * 0.95f);
      int hw1 = (int)roundf(11.0f * sc), hw2 = (int)roundf(8.5f * sc), hw3 = (int)roundf(10.0f * sc);
      int y1 = (int)roundf(cy + 6.5f * sc);
      int y2 = (int)roundf(cy + 10.5f * sc);
      int y3 = (int)roundf(cy + 14.5f * sc);
      pad_box((int)cx - hw1, y1, (int)cx + hw1, y1 + 1, fr, fg, fb);
      pad_box((int)cx - hw2 + 2, y2, (int)cx + hw2 + 2, y2 + 1, fr, fg, fb);
      pad_box((int)cx - hw3 - 1, y3, (int)cx + hw3 - 1, y3 + 1, fr, fg, fb);
    }
    flush_pad(screen_cx, screen_cy);
  };

  paint_wx(36, 40, 1.25f, map_wx_code(p.cur_cond));

  if (!show_card) {
    static const int kFcIconX[] = {428, 560, 692};
    size_t start = 0;
    for (int i = 0; i < 3; i++) {
      std::string seg;
      if (start < p.raw_3day.size()) {
        size_t bar_p = p.raw_3day.find('|', start);
        seg = (bar_p == std::string::npos) ? p.raw_3day.substr(start) : p.raw_3day.substr(start, bar_p - start);
        start = (bar_p == std::string::npos) ? p.raw_3day.size() : (bar_p + 1);
      }
      size_t c1 = seg.find(',');
      size_t c2 = (c1 != std::string::npos) ? seg.find(',', c1 + 1) : std::string::npos;
      size_t c3 = (c2 != std::string::npos) ? seg.find(',', c2 + 1) : std::string::npos;
      if (c1 != std::string::npos && c2 != std::string::npos && c3 != std::string::npos) {
        std::string d_str = seg.substr(0, c1);
        std::string w_str = seg.substr(c1 + 1, c2 - c1 - 1);
        float hi_c = (float)atof(seg.substr(c2 + 1, c3 - c2 - 1).c_str());
        float lo_c = (float)atof(seg.substr(c3 + 1).c_str());
        int f_hi = (int)roundf(p.use_fahrenheit ? c_to_f(hi_c) : hi_c);
        int f_lo = (int)roundf(p.use_fahrenheit ? c_to_f(lo_c) : lo_c);
        paint_wx(kFcIconX[i], 40, 0.85f, map_wx_code(w_str));
        if (w.fc_day[i] != nullptr) lv_label_set_text(w.fc_day[i], d_str.c_str());
        char hl_b[24];
        snprintf(hl_b, sizeof(hl_b),
                 (f_hi >= 10 || f_hi < 0 || f_lo >= 10 || f_lo < 0) ? "\u2191%d°\u2193%d°" : "\u2191%d° \u2193%d°",
                 f_hi, f_lo);
        if (w.fc_hl[i] != nullptr) lv_label_set_text(w.fc_hl[i], hl_b);
      } else {
        paint_wx(kFcIconX[i], 40, 0.85f, 0);
        if (w.fc_day[i] != nullptr) lv_label_set_text(w.fc_day[i], (i == 0) ? "DAY 1" : ((i == 1) ? "DAY 2" : "DAY 3"));
        if (w.fc_hl[i] != nullptr) lv_label_set_text(w.fc_hl[i], "Syncing");
      }
    }
  } else {
    clear_pad();
    auto draw_santa_icon = [&]() {
      pad_box(14, 32, 38, 44, 235, 35, 45);
      pad_box(12, 46, 44, 48, 255, 200, 40);
      pad_disc(24.0f, 27.0f, 5.5f, 255.0f, 225.0f, 200.0f, 1.0f);
      pad_disc(23.0f, 22.0f, 5.0f, 235.0f, 35.0f, 45.0f, 1.0f);
      pad_disc(50.0f, 34.0f, 4.5f, 165.0f, 105.0f, 55.0f, 1.0f);
      pad_disc(55.5f, 33.5f, 2.5f, 255.0f, 30.0f, 35.0f, 1.0f);
    };

    auto draw_countdown_stars = [&](float rem01, uint8_t lr, uint8_t lg, uint8_t lb,
                                    uint8_t dr, uint8_t dg, uint8_t db) {
      if (rem01 < 0.0f) return;
      int lit_n = std::clamp((int)ceilf(rem01 * 5.0f), 1, 5);
      if (rem01 <= 0.01f) lit_n = 0;
      for (int si = 0; si < 5; si++) {
        int sx = 724 + si * 15;
        int sy = 24;
        bool lit = (si < lit_n);
        uint8_t sr = lit ? lr : dr, sg = lit ? lg : dg, sb = lit ? lb : db;
        // 4-point glowing star glyph
        for (int dy = -5; dy <= 5; dy++) {
          for (int dx = -5; dx <= 5; dx++) {
            float ax = fabsf((float)dx), ay = fabsf((float)dy);
            float d_star = (ax + ay <= 4.6f && (ax <= 1.4f || ay <= 1.4f || ax + ay <= 2.5f)) ? 1.0f : 0.0f;
            if (d_star > 0.0f) {
              float fall = std::clamp(1.0f - (ax + ay) / 6.2f, 0.35f, 1.0f);
              put_px(sx + dx, sy + dy, (uint8_t)(sr * fall), (uint8_t)(sg * fall), (uint8_t)(sb * fall));
            }
          }
        }
      }
    };

    if (slp == 1) {
      // Deep Sleep: Real Lunar Phase Moon!
      pad_disc(32.0f, 32.0f, 18.0f, 185.0f, 16.0f, 22.0f, 0.22f);
      float phase = p.loc_valid ? lunar_phase_01(p.loc_year, p.loc_month, p.loc_day) : 0.16f;
      // Draw illuminated sphere using exact 3D terminator cosine across moon radius 13
      float rad = 13.0f;
      float cos_p = cosf(phase * 2.0f * (float)M_PI);
      bool waxing = (phase < 0.5f);
      for (int py = 18; py <= 46; py++) {
        for (int px = 18; px <= 46; px++) {
          float dx = (float)px - 32.0f;
          float dy = (float)py - 32.0f;
          float r2 = dx * dx + dy * dy;
          if (r2 > (rad + 0.6f) * (rad + 0.6f)) continue;
          float edge = std::clamp(rad + 0.5f - sqrtf(r2), 0.0f, 1.0f);
          float xr = sqrtf(std::max(0.0f, rad * rad - dy * dy));
          float term_x = xr * cos_p;
          bool lit = waxing ? (dx >= term_x) : (dx <= -term_x);
          // Subtle dim disc outline even near New Moon so moon is always recognizable
          float bright = lit ? 1.0f : 0.16f;
          int idx = (py * kPad + px) * 3;
          float a = edge;
          s_pad[idx] = (uint8_t)((float)s_pad[idx] * (1.0f - a) + (232.0f * bright) * a);
          s_pad[idx + 1] = (uint8_t)((float)s_pad[idx + 1] * (1.0f - a) + (24.0f * bright) * a);
          s_pad[idx + 2] = (uint8_t)((float)s_pad[idx + 2] * (1.0f - a) + (30.0f * bright) * a);
        }
      }
      pad_disc(15.0f, 18.0f, 1.9f, 240.0f, 40.0f, 45.0f, 0.90f);
      pad_disc(48.0f, 46.0f, 1.5f, 240.0f, 40.0f, 45.0f, 0.85f);
      if (p.nap_active) {
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, "NAP TIME · RESTING");
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Quiet rest until stars fade!");
      } else {
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, p.sleep_title.c_str());
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.sleep_sub.c_str());
      }
      draw_countdown_stars(p.sleep_remaining_01, 235, 32, 38, 52, 8, 12);
    } else if (slp == 2) {
      // Morning Quiet Play: Rising Sun over Horizon
      pad_disc(32.0f, 38.0f, 19.0f, 255.0f, 130.0f, 15.0f, 0.25f);
      pad_disc(32.0f, 38.0f, 12.0f, 255.0f, 145.0f, 20.0f, 1.0f);
      pad_box(8, 39, 56, 56, 0, 0, 0);
      pad_box(12, 39, 52, 41, 255, 135, 20);
      pad_box(18, 45, 46, 46, 160, 75, 10);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, p.quiet_title.c_str());
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.quiet_sub.c_str());
    } else if (slp == 3) {
      // Evening Wind-Down ("Get Ready for Bed"): Santa on Xmas Eve, Tooth Fairy if active, else cozy dusk!
      if (mode == 4 && p.keep_santa_winddown) {
        draw_santa_icon();
        std::string st_hdr = (p.santa_wp_idx < 0) ? "SANTA PREP · BEDTIME SOON!" : (std::string("SANTA \u2192 ") + p.santa_city);
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, st_hdr.c_str());
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.winddown_sub.c_str());
      } else if (p.tooth_fairy_mode) {
        // Cute sparkling tooth + golden fairy dust
        pad_disc(32.0f, 32.0f, 19.0f, 255.0f, 170.0f, 45.0f, 0.20f);
        pad_disc(26.0f, 28.0f, 8.0f, 255.0f, 215.0f, 145.0f, 1.0f);
        pad_disc(38.0f, 28.0f, 8.0f, 255.0f, 215.0f, 145.0f, 1.0f);
        pad_box(20, 29, 44, 38, 255, 215, 145);
        pad_disc(26.0f, 42.0f, 5.2f, 255.0f, 215.0f, 145.0f, 1.0f);
        pad_disc(38.0f, 42.0f, 5.2f, 255.0f, 215.0f, 145.0f, 1.0f);
        pad_disc(15.0f, 17.0f, 2.2f, 255.0f, 195.0f, 60.0f, 1.0f);
        pad_disc(49.0f, 19.0f, 2.4f, 255.0f, 225.0f, 90.0f, 1.0f);
        pad_disc(48.0f, 45.0f, 1.8f, 255.0f, 175.0f, 45.0f, 0.9f);
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, "TOOTH FAIRY TONIGHT!");
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Pop your tooth under the pillow!");
      } else {
        // Setting dusk sun + twilight crescent & stars (distinct from morning sunrise!)
        pad_disc(26.0f, 42.0f, 16.0f, 255.0f, 105.0f, 15.0f, 0.24f);
        pad_disc(26.0f, 42.0f, 10.5f, 255.0f, 120.0f, 18.0f, 1.0f);
        pad_box(6, 43, 58, 58, 0, 0, 0);
        pad_box(10, 43, 54, 45, 235, 105, 18);
        pad_box(16, 49, 48, 50, 145, 60, 10);
        // Evening crescent moon & two twinkling stars above right
        pad_disc(44.0f, 21.0f, 6.8f, 255.0f, 175.0f, 45.0f, 1.0f);
        pad_disc(47.2f, 18.5f, 5.8f, 0.0f, 0.0f, 0.0f, 1.0f);
        pad_disc(17.0f, 18.0f, 1.8f, 255.0f, 195.0f, 70.0f, 0.95f);
        pad_disc(31.0f, 14.0f, 1.4f, 255.0f, 185.0f, 55.0f, 0.85f);
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, p.winddown_title.c_str());
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.winddown_sub.c_str());
      }
      draw_countdown_stars(p.sleep_remaining_01, 255, 165, 38, 65, 32, 8);
    } else if (p.focus_active) {
      // Teen / Study Desk Focus Pomodoro Timer Card (teal study vs green break)
      uint8_t ar = p.focus_is_break ? 34 : 20, ag = p.focus_is_break ? 197 : 184, ab = p.focus_is_break ? 94 : 166;
      pad_disc(32.0f, 32.0f, 18.0f, (float)ar, (float)ag, (float)ab, 0.20f);
      pad_disc(32.0f, 32.0f, 13.5f, (float)ar, (float)ag, (float)ab, 1.0f);
      pad_disc(32.0f, 32.0f, 10.5f, 0.0f, 0.0f, 0.0f, 1.0f);
      pad_box(31, 23, 33, 33, ar, ag, ab);
      pad_box(32, 31, 40, 33, ar, ag, ab);
      pad_box(27, 15, 37, 18, ar, ag, ab);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, p.focus_title.c_str());
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.focus_sub.c_str());
      draw_countdown_stars(p.focus_remaining_01, ar, ag, ab, 24, 34, 42);
    } else if (mode == 2) {
      pad_box(14, 38, 50, 52, 235, 135, 55);
      pad_box(14, 38, 50, 42, 255, 95, 155);
      pad_box(20, 26, 44, 38, 245, 165, 75);
      pad_box(20, 26, 44, 29, 90, 210, 255);
      for (int c = -1; c <= 1; c++) {
        int cx_c = 32 + c * 7;
        pad_box(cx_c - 1, 18, cx_c + 1, 25, 255, 250, 240);
        pad_disc((float)cx_c, 14.5f, 3.0f, 255.0f, 195.0f, 20.0f, 1.0f);
      }
      int age = std::max(1, p.loc_valid ? (p.loc_year - p.birth_year) : 7);
      const char *ord = ((age % 100 >= 11 && age % 100 <= 13))
                            ? "TH"
                            : ((age % 10 == 1) ? "ST" : ((age % 10 == 2) ? "ND" : ((age % 10 == 3) ? "RD" : "TH")));
      char bt[40];
      snprintf(bt, sizeof(bt), "HAPPY %d%s BIRTHDAY!", age, ord);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, bt);
      std::string b_sub = std::string("Have the best day, ") + p.child_name + "!";
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, b_sub.c_str());
    } else if (mode == 9) {
      pad_box(29, 46, 35, 54, 140, 80, 35);
      for (int row = 0; row < 28; row++) {
        int hw = 2 + (row * 16) / 28;
        pad_box(32 - hw, 18 + row, 32 + hw, 18 + row, 28, 175, 75);
      }
      pad_disc(32.0f, 15.0f, 4.2f, 255.0f, 215.0f, 25.0f, 1.0f);
      pad_disc(26.0f, 32.0f, 2.4f, 255.0f, 45.0f, 55.0f, 1.0f);
      pad_disc(37.0f, 39.0f, 2.4f, 255.0f, 200.0f, 30.0f, 1.0f);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, p.xmas_title.c_str());
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, p.xmas_sub.c_str());
    } else if (mode == 4) {
      draw_santa_icon();
      if (p.santa_wp_idx < 0) {
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, "SANTA PREP · NORTH POLE");
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Elves loading sleigh · Liftoff 10am!");
      } else {
        std::string st_hdr = std::string("SANTA \u2192 ") + p.santa_city;
        if (w.card_title != nullptr) lv_label_set_text(w.card_title, st_hdr.c_str());
        char sb_buf[56];
        int d_val = p.use_miles ? km_to_mi(p.santa_dist_km) : p.santa_dist_km;
        const char *d_unit = p.use_miles ? "mi" : "km";
        snprintf(sb_buf, sizeof(sb_buf), "%.2fB gifts · %d %s away", (float)p.santa_gifts_m / 1000.0f, d_val, d_unit);
        if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, sb_buf);
      }
    } else if (mode == 5) {
      pad_box(30, 12, 34, 19, 45, 165, 55);
      pad_disc(25.0f, 35.0f, 14.0f, 255.0f, 115.0f, 12.0f, 1.0f);
      pad_disc(39.0f, 35.0f, 14.0f, 255.0f, 115.0f, 12.0f, 1.0f);
      pad_disc(32.0f, 35.0f, 15.0f, 255.0f, 132.0f, 18.0f, 1.0f);
      pad_disc(25.0f, 30.0f, 3.2f, 255.0f, 235.0f, 40.0f, 1.0f);
      pad_disc(39.0f, 30.0f, 3.2f, 255.0f, 235.0f, 40.0f, 1.0f);
      pad_box(23, 41, 41, 45, 255, 230, 35);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, "HAPPY HALLOWEEN!");
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Bats & ghosts on patrol!");
    } else if (mode == 6) {
      pad_disc(32.0f, 27.0f, 13.0f, 175.0f, 105.0f, 255.0f, 1.0f);
      pad_disc(32.0f, 37.0f, 16.0f, 175.0f, 105.0f, 255.0f, 1.0f);
      pad_box(17, 30, 47, 34, 255, 215, 45);
      pad_disc(25.0f, 41.0f, 2.5f, 80.0f, 235.0f, 190.0f, 1.0f);
      pad_disc(39.0f, 41.0f, 2.5f, 255.0f, 115.0f, 175.0f, 1.0f);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, "EASTER EGG HUNT!");
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Spot all 5 sparkling eggs!");
    } else if (mode == 7) {
      for (int r = 0; r < 10; r++) {
        float a = (float)r * 0.6283f;
        pad_disc(32.0f + cosf(a) * 16.0f, 32.0f + sinf(a) * 16.0f, 2.5f, 255.0f, (r & 1) ? 205.0f : 80.0f,
                 (r & 1) ? 35.0f : 220.0f, 1.0f);
        pad_disc(32.0f + cosf(a) * 9.0f, 32.0f + sinf(a) * 9.0f, 2.0f, 255.0f, 235.0f, 90.0f, 1.0f);
      }
      if (w.card_title != nullptr)
        lv_label_set_text(w.card_title, (p.loc_valid && p.loc_month == 11) ? "BONFIRE FIREWORKS!" : "HAPPY NEW YEAR!");
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, "Rockets & starburst sky show!");
    } else if (mode == 8) {
      pad_disc(24.0f, 26.0f, 9.5f, 255.0f, 205.0f, 70.0f, 1.0f);
      pad_disc(42.0f, 38.0f, 7.0f, 255.0f, 95.0f, 30.0f, 1.0f);
      pad_box(24, 34, 26, 48, 220, 230, 245);
      pad_box(38, 34, 40, 48, 220, 230, 245);
      pad_disc(32.0f, 41.0f, 4.0f, 220.0f, 230.0f, 245.0f, 1.0f);
      if (w.card_title != nullptr) lv_label_set_text(w.card_title, "MAY THE 4TH BE WITH YOU");
      std::string sw_sub = std::string("Happy Star Wars Day, ") + p.child_name + "!";
      if (w.card_sub != nullptr) lv_label_set_text(w.card_sub, sw_sub.c_str());
    } else {
      // Prominent Calendar Card (Band 3 right side, replaces 3-day forecast during daytime)
      pad_box(14, 14, 50, 52, 230, 238, 248);
      pad_box(14, 14, 50, 25, 45, 178, 172);
      pad_box(21, 10, 25, 19, 245, 248, 252);
      pad_box(39, 10, 43, 19, 245, 248, 252);
      pad_box(20, 31, 44, 34, 30, 44, 62);
      pad_box(20, 38, 40, 41, 30, 44, 62);
      pad_box(20, 45, 34, 48, 242, 165, 42);
      if (w.card_title != nullptr)
        lv_label_set_text(w.card_title, p.prominent_cal_title.empty() ? "TODAY'S PLAN" : p.prominent_cal_title.c_str());
      if (w.card_sub != nullptr)
        lv_label_set_text(w.card_sub, p.prominent_cal_sub.empty() ? "Check family calendar" : p.prominent_cal_sub.c_str());
    }
    flush_pad(432, 40);
  }

  lv_obj_invalidate(cv);
}

}  // namespace world_clock
