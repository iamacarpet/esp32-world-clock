#pragma once

#include "clock_math.h"
#include "world_map.h"
#include <lvgl.h>
#include "esphome/core/hal.h"

namespace world_clock {

struct SpriteFrameParams {
  lv_obj_t *world_canvas{nullptr};
  lv_obj_t *tracker_badge{nullptr};
  lv_obj_t *tracker_badge_lbl{nullptr};
  uint8_t *bg_cache{nullptr};
  uint8_t mode{1};
  uint8_t sleep_phase{0};
  bool keep_santa_winddown{true};
  bool manual_santa_preview{false};
  bool tooth_fairy_mode{false};
  float home_lat{51.5074f};
  float home_lon{-0.1278f};
  std::string home_label{"LONDON UK"};
  bool utc_valid{false};
  int utc_month{1};
  int utc_day{1};
  float utc_hr{12.0f};
  bool loc_valid{false};
  int loc_month{1};
  int loc_day{1};
  // Live ISS input (already populated by HA or Core 0 worker)
  bool iss_has_fix{false};
  float iss_cur_lat{0.0f};
  float iss_cur_lon{0.0f};
  float iss_vel_lat{0.0f};
  float iss_vel_lon{0.06f};
  uint32_t iss_fix_ms{0};
  // Santa outputs (updated in-place when mode == 4)
  int *santa_wp_idx{nullptr};
  float *santa_lat{nullptr};
  float *santa_lon{nullptr};
  std::string *santa_city{nullptr};
  int *santa_dist_km{nullptr};
  int *santa_gifts_m{nullptr};
  bool *force_redraw{nullptr};
  bool *sprites_dirty{nullptr};
  bool *map_just_flushed{nullptr};
  uint8_t *badge_mode_applied{nullptr};
};

class SpriteEngine {
 public:
  static constexpr size_t kMaxSpr = 24;

  void reset_iss_smoothing() {
    disp_init_ = false;
    tr_cnt_ = 0;
  }

  void tick(const SpriteFrameParams &p) {
    lv_obj_t *cv = p.world_canvas;
    if (cv == nullptr || p.bg_cache == nullptr) return;

    uint8_t *bg = p.bg_cache;
    static_assert(kScratchBytes >= 80UL * 80UL * 3UL, "kScratchBytes too small for 80x80 sprite scratch buffer");
    uint8_t *s_loc = bg + kBgBytes;

#if LVGL_VERSION_MAJOR >= 9
    lv_draw_buf_t *db_cv = lv_canvas_get_draw_buf(cv);
    bool fast_cv = (db_cv != nullptr && db_cv->data != nullptr && db_cv->header.cf == LV_COLOR_FORMAT_RGB565);
#else
    lv_img_dsc_t *dsc_cv = lv_canvas_get_img(cv);
    lv_color_t *raw_cv = (dsc_cv != nullptr && dsc_cv->data != nullptr && dsc_cv->header.cf == LV_IMG_CF_TRUE_COLOR)
                             ? (lv_color_t *)const_cast<uint8_t *>(dsc_cv->data)
                             : nullptr;
#endif

    auto put_cv = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
      if (x < 0 || x >= (int)kMapWidth || y < 0 || y >= (int)kMapHeight) return;
#if LVGL_VERSION_MAJOR >= 9
      if (fast_cv) {
        uint16_t *row16 = (uint16_t *)(db_cv->data + (size_t)y * db_cv->header.stride);
        row16[x] = ((uint16_t)(r & 0xF8U) << 8) | ((uint16_t)(g & 0xFCU) << 3) | ((uint16_t)b >> 3);
      } else {
        lv_canvas_set_px(cv, x, y, lv_color_make(r, g, b), LV_OPA_COVER);
      }
#else
      if (raw_cv != nullptr) {
        raw_cv[(size_t)y * kMapWidth + (size_t)x] = lv_color_make(r, g, b);
      } else {
        lv_canvas_set_px_color(cv, x, y, lv_color_make(r, g, b));
      }
#endif
    };

    auto get_cv_rgb = [&](int x, int y, uint8_t &or_c, uint8_t &og_c, uint8_t &ob_c) {
#if LVGL_VERSION_MAJOR >= 9
      if (fast_cv) {
        uint16_t px16 = ((uint16_t *)(db_cv->data + (size_t)y * db_cv->header.stride))[x];
        uint8_t r5 = (uint8_t)((px16 >> 8) & 0xF8U);
        uint8_t g6 = (uint8_t)((px16 >> 3) & 0xFCU);
        uint8_t b5 = (uint8_t)((px16 << 3) & 0xF8U);
        or_c = r5 | (r5 >> 5);
        og_c = g6 | (g6 >> 6);
        ob_c = b5 | (b5 >> 5);
        return;
      }
#else
      if (raw_cv != nullptr) {
        uint32_t c32 = lv_color_to32(raw_cv[(size_t)y * kMapWidth + (size_t)x]);
        or_c = (uint8_t)((c32 >> 16) & 0xFFU);
        og_c = (uint8_t)((c32 >> 8) & 0xFFU);
        ob_c = (uint8_t)(c32 & 0xFFU);
        return;
      }
#endif
      size_t idx = ((size_t)y * kMapWidth + (size_t)x) * 3UL;
      or_c = bg[idx];
      og_c = bg[idx + 1];
      ob_c = bg[idx + 2];
    };

    auto smooth01 = [](float t) -> float {
      t = std::clamp(t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    };

    auto inv_box = [&](int x0, int y0, int x1, int y1) {
      if (p.map_just_flushed != nullptr && *p.map_just_flushed) return;
      lv_area_t ar;
      ar.x1 = std::max(0, x0);
      ar.y1 = 74 + std::max(0, y0);
      ar.x2 = std::min((int)kMapWidth - 1, x1);
      ar.y2 = 74 + std::min((int)kMapHeight - 1, y1);
      if (ar.x2 >= ar.x1 && ar.y2 >= ar.y1) lv_obj_invalidate_area(cv, &ar);
    };

    auto hide_badge = [&]() {
      if (p.tracker_badge == nullptr || p.badge_mode_applied == nullptr) return;
      if (*p.badge_mode_applied != 0) {
        lv_obj_add_flag(p.tracker_badge, LV_OBJ_FLAG_HIDDEN);
        *p.badge_mode_applied = 0;
        badge_last_x_ = -999;
        badge_last_y_ = -999;
      }
    };

    auto update_badge = [&](uint8_t bmode, const char *txt, uint32_t bg_hex, uint32_t brd_hex, uint32_t txt_hex,
                            int bx, int by) {
      if (p.tracker_badge == nullptr || p.tracker_badge_lbl == nullptr || p.badge_mode_applied == nullptr) return;
      if (*p.badge_mode_applied != bmode) {
        lv_label_set_text(p.tracker_badge_lbl, txt);
        lv_obj_set_style_bg_color(p.tracker_badge, lv_color_hex(bg_hex), 0);
        lv_obj_set_style_border_color(p.tracker_badge, lv_color_hex(brd_hex), 0);
        lv_obj_set_style_text_color(p.tracker_badge_lbl, lv_color_hex(txt_hex), 0);
        lv_obj_set_style_bg_opa(p.tracker_badge, LV_OPA_80, 0);
        lv_obj_set_size(p.tracker_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        *p.badge_mode_applied = bmode;
        badge_last_x_ = -999;
#if LVGL_VERSION_MAJOR >= 9
        lv_obj_remove_flag(p.tracker_badge, LV_OBJ_FLAG_HIDDEN);
#else
        lv_obj_clear_flag(p.tracker_badge, LV_OBJ_FLAG_HIDDEN);
#endif
      }
      if (bx != badge_last_x_ || by != badge_last_y_) {
        lv_obj_set_pos(p.tracker_badge, bx, by);
        badge_last_x_ = bx;
        badge_last_y_ = by;
      }
    };

    if (!inited_ || (p.sprites_dirty != nullptr && *p.sprites_dirty)) {
      for (size_t i = 0; i < kMaxSpr; i++) prev_[i].used = false;
      inited_ = true;
      if (p.sprites_dirty != nullptr) *p.sprites_dirty = false;
    }

    const uint8_t mode = p.mode;
    const bool santa_winddown = (p.sleep_phase == 3 && mode == 4 && p.keep_santa_winddown);
    const bool tooth_fairy_wd = (p.sleep_phase == 3 && p.tooth_fairy_mode && !santa_winddown);
    const bool run_sprites = (p.sleep_phase == 0 && mode != 0) || santa_winddown || tooth_fairy_wd;

    if (!run_sprites) {
      hide_badge();
      for (size_t i = 0; i < kMaxSpr; i++) {
        if (!prev_[i].used) continue;
        int ax0 = std::max(0, (int)prev_[i].x0), ax1 = std::min((int)kMapWidth - 1, (int)prev_[i].x1);
        int ay0 = std::max(0, (int)prev_[i].y0), ay1 = std::min((int)kMapHeight - 1, (int)prev_[i].y1);
        for (int y = ay0; y <= ay1; y++) {
          size_t row_off = (size_t)y * kMapWidth * 3UL;
          for (int x = ax0; x <= ax1; x++) {
            size_t idx = row_off + (size_t)x * 3UL;
            put_cv(x, y, bg[idx], bg[idx + 1], bg[idx + 2]);
          }
        }
        inv_box(ax0, ay0, ax1, ay1);
        prev_[i].used = false;
      }
      if (p.map_just_flushed != nullptr && *p.map_just_flushed) {
        *p.map_just_flushed = false;
        lv_obj_invalidate(cv);
      }
      return;
    }

    const uint32_t tk = ++anim_tick_;

    DirtyBox old_prev[kMaxSpr];
    for (size_t i = 0; i < kMaxSpr; i++) {
      old_prev[i] = prev_[i];
      if (!prev_[i].used) continue;
      int ax0 = std::max(0, (int)prev_[i].x0), ax1 = std::min((int)kMapWidth - 1, (int)prev_[i].x1);
      int ay0 = std::max(0, (int)prev_[i].y0), ay1 = std::min((int)kMapHeight - 1, (int)prev_[i].y1);
      for (int y = ay0; y <= ay1; y++) {
        size_t row_off = (size_t)y * kMapWidth * 3UL;
        for (int x = ax0; x <= ax1; x++) {
          size_t idx = row_off + (size_t)x * 3UL;
          put_cv(x, y, bg[idx], bg[idx + 1], bg[idx + 2]);
        }
      }
      prev_[i].used = false;
    }

    auto blend_spr_px = [&](int x, int y, float cr, float cg, float cb, float a) {
      if (x < 0 || x >= (int)kMapWidth || y < 0 || y >= (int)kMapHeight || a <= 0.01f) return;
      a = std::clamp(a, 0.0f, 1.0f);
      uint8_t br, bg_c, bb;
      get_cv_rgb(x, y, br, bg_c, bb);
      put_cv(x, y, (uint8_t)((float)br * (1.0f - a) + cr * a), (uint8_t)((float)bg_c * (1.0f - a) + cg * a),
             (uint8_t)((float)bb * (1.0f - a) + cb * a));
    };

    auto spr_disc = [&](float cx, float cy, float rad, float cr, float cg, float cb, float op) {
      int x0 = std::max(0, (int)floorf(cx - rad - 1.0f));
      int x1 = std::min((int)kMapWidth - 1, (int)ceilf(cx + rad + 1.0f));
      int y0 = std::max(0, (int)floorf(cy - rad - 1.0f));
      int y1 = std::min((int)kMapHeight - 1, (int)ceilf(cy + rad + 1.0f));
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy));
          float a = std::clamp(rad + 0.5f - d, 0.0f, 1.0f) * op;
          if (a > 0.01f) blend_spr_px(x, y, cr, cg, cb, a);
        }
      }
    };

    auto record_spr = [&](size_t slot, int x0, int y0, int x1, int y1) {
      if (slot >= kMaxSpr) return;
      prev_[slot].x0 = (int16_t)x0;
      prev_[slot].y0 = (int16_t)y0;
      prev_[slot].x1 = (int16_t)x1;
      prev_[slot].y1 = (int16_t)y1;
      prev_[slot].used = true;
      if (old_prev[slot].used) {
        int ox0 = old_prev[slot].x0, oy0 = old_prev[slot].y0;
        int ox1 = old_prev[slot].x1, oy1 = old_prev[slot].y1;
        old_prev[slot].used = false;
        if (abs(x0 - ox0) <= 32 && abs(y0 - oy0) <= 32) {
          inv_box(std::min(ox0, x0), std::min(oy0, y0), std::max(ox1, x1), std::max(oy1, y1));
        } else {
          inv_box(ox0, oy0, ox1, oy1);
          inv_box(x0, y0, x1, y1);
        }
      } else {
        inv_box(x0, y0, x1, y1);
      }
    };

    const float home_lat = std::clamp(p.home_lat, -53.0f, 75.5f);
    const float home_lon = std::clamp(p.home_lon, -167.0f, 178.0f);

    if (mode != 1 && mode != 4) hide_badge();

    if (tooth_fairy_wd) {
      hide_badge();
      // Gentle golden fairy-dust sparkles floating across the amber map
      float hpx, hpy;
      latlon_to_map_xy(home_lat, home_lon, hpx, hpy);
      for (size_t i = 0; i < 8; i++) {
        float ang = (float)tk * 0.055f + (float)i * 0.7854f;
        float rad = 22.0f + 16.0f * sinf((float)tk * 0.04f + (float)i * 1.3f) + (float)(i % 3) * 14.0f;
        float sx = hpx + cosf(ang) * rad * 1.45f;
        float sy = hpy + sinf(ang) * rad * 0.75f;
        float tw = 0.35f + 0.65f * (0.5f + 0.5f * sinf((float)tk * 0.25f + (float)i * 2.1f));
        spr_disc(sx, sy, 2.2f * tw + 0.6f, 255.0f, 195.0f, 75.0f, 0.90f * tw);
        record_spr(i, (int)sx - 4, (int)sy - 4, (int)sx + 4, (int)sy + 4);
      }
    } else if (mode == 1) {
      if (!p.iss_has_fix) {
        hide_badge();
      } else {
        uint32_t now_ms = esphome::millis();
        float dt_interp = std::clamp((float)(now_ms - p.iss_fix_ms) / 1000.0f, 0.0f, 90.0f);
        float target_lat = std::clamp(p.iss_cur_lat + p.iss_vel_lat * dt_interp, -53.0f, 53.0f);
        float target_lon = p.iss_cur_lon + p.iss_vel_lon * dt_interp;
        while (target_lon > 180.0f) target_lon -= 360.0f;
        while (target_lon < -180.0f) target_lon += 360.0f;

        if (!disp_init_) {
          disp_lat_ = target_lat;
          disp_lon_ = target_lon;
          disp_init_ = true;
        } else {
          float err_lon = target_lon - disp_lon_;
          if (err_lon > 180.0f) err_lon -= 360.0f;
          if (err_lon < -180.0f) err_lon += 360.0f;
          float err_lat = target_lat - disp_lat_;
          if (fabsf(err_lon) > 25.0f || fabsf(err_lat) > 25.0f) {
            disp_lat_ = target_lat;
            disp_lon_ = target_lon;
          } else {
            disp_lat_ += err_lat * 0.12f;
            disp_lon_ += err_lon * 0.12f;
            while (disp_lon_ > 180.0f) disp_lon_ -= 360.0f;
            while (disp_lon_ < -180.0f) disp_lon_ += 360.0f;
          }
        }
        float live_lat = disp_lat_;
        float live_lon = disp_lon_;

        if (tr_cnt_ == 0) {
          tr_lat_[0] = live_lat;
          tr_lon_[0] = live_lon;
          tr_cnt_ = 1;
        } else {
          float d_tr_lon = live_lon - tr_lon_[0];
          if (d_tr_lon > 180.0f) d_tr_lon -= 360.0f;
          if (d_tr_lon < -180.0f) d_tr_lon += 360.0f;
          float d_tr_lat = live_lat - tr_lat_[0];
          if (sqrtf(d_tr_lat * d_tr_lat + d_tr_lon * d_tr_lon) >= 4.2f) {
            for (int k = 4; k >= 1; k--) {
              tr_lat_[k] = tr_lat_[k - 1];
              tr_lon_[k] = tr_lon_[k - 1];
            }
            tr_lat_[0] = live_lat;
            tr_lon_[0] = live_lon;
            if (tr_cnt_ < 5) tr_cnt_++;
          }
        }

        float map_lon = live_lon;
        if (map_lon < -167.0f) map_lon += 360.0f;
        if (map_lon > 178.0f) map_lon -= 360.0f;
        map_lon = std::clamp(map_lon, -166.5f, 177.5f);

        float ix, iy;
        latlon_to_map_xy(live_lat, map_lon, ix, iy);
        int cxi = (int)roundf(ix), cyi = (int)roundf(iy);

        for (int py = 0; py < 52; py++) {
          int sy = cyi - 26 + py;
          for (int px = 0; px < 52; px++) {
            int sx = cxi - 26 + px;
            int lidx = (py * 52 + px) * 3;
            if (sx >= 0 && sx < (int)kMapWidth && sy >= 0 && sy < (int)kMapHeight) {
              size_t bidx = ((size_t)sy * kMapWidth + (size_t)sx) * 3UL;
              s_loc[lidx] = bg[bidx];
              s_loc[lidx + 1] = bg[bidx + 1];
              s_loc[lidx + 2] = bg[bidx + 2];
            } else {
              s_loc[lidx] = 0;
              s_loc[lidx + 1] = 0;
              s_loc[lidx + 2] = 0;
            }
          }
        }

        auto iss_px = [&](int lx, int ly, uint8_t cr, uint8_t cg, uint8_t cb, float a) {
          if (lx < 0 || lx >= 52 || ly < 0 || ly >= 52 || a <= 0.01f) return;
          int lidx = (ly * 52 + lx) * 3;
          s_loc[lidx] = (uint8_t)((float)s_loc[lidx] * (1.0f - a) + (float)cr * a);
          s_loc[lidx + 1] = (uint8_t)((float)s_loc[lidx + 1] * (1.0f - a) + (float)cg * a);
          s_loc[lidx + 2] = (uint8_t)((float)s_loc[lidx + 2] * (1.0f - a) + (float)cb * a);
        };

        bool near_home = haversine_km(home_lat, home_lon, live_lat, live_lon) < 900.0f;
        uint8_t ring_r_c = near_home ? 255 : 56;
        uint8_t ring_g_c = 215;
        uint8_t ring_b_c = near_home ? 55 : 255;

        float pulse = (float)(tk % 24UL) / 24.0f;
        float pulse_env = sinf(pulse * (float)M_PI);
        float ring_r = 11.5f + pulse * 12.5f;
        for (int py = 0; py < 52; py++) {
          for (int px = 0; px < 52; px++) {
            float d = sqrtf((float)((px - 26) * (px - 26) + (py - 26) * (py - 26)));
            if (d < 15.5f) iss_px(px, py, 2, 10, 16, std::clamp(15.5f - d, 0.0f, 1.0f) * 0.82f);
            float rd = fabsf(d - ring_r);
            if (rd < 1.8f) iss_px(px, py, ring_r_c, ring_g_c, ring_b_c, (1.0f - rd / 1.8f) * pulse_env * 0.88f);
          }
        }

        constexpr int b_off = 14;
        for (int k = 0; k <= 4; k++) {
          iss_px(26 - b_off + k, 26 - b_off, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 - b_off, 26 - b_off + k, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 + b_off - k, 26 - b_off, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 + b_off, 26 - b_off + k, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 - b_off + k, 26 + b_off, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 - b_off, 26 + b_off - k, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 + b_off - k, 26 + b_off, ring_r_c, 225, ring_b_c, 1.0f);
          iss_px(26 + b_off, 26 + b_off - k, ring_r_c, 225, ring_b_c, 1.0f);
        }

        for (int wy = -7; wy <= 7; wy++) {
          for (int wx = -11; wx <= -8; wx++) iss_px(26 + wx, 26 + wy, 255, 195, 35, 1.0f);
          for (int wx = -6; wx <= -4; wx++) iss_px(26 + wx, 26 + wy, 255, 195, 35, 1.0f);
          for (int wx = 4; wx <= 6; wx++) iss_px(26 + wx, 26 + wy, 255, 195, 35, 1.0f);
          for (int wx = 8; wx <= 11; wx++) iss_px(26 + wx, 26 + wy, 255, 195, 35, 1.0f);
        }
        for (int tx = -12; tx <= 12; tx++) iss_px(26 + tx, 26, 255, 255, 255, 1.0f);
        for (int my = -5; my <= 5; my++) {
          for (int mx = -1; mx <= 1; mx++) iss_px(26 + mx, 26 + my, 255, 255, 255, 1.0f);
        }

        for (int py = 0; py < 52; py++) {
          for (int px = 0; px < 52; px++) {
            int lidx = (py * 52 + px) * 3;
            put_cv(cxi - 26 + px, cyi - 26 + py, s_loc[lidx], s_loc[lidx + 1], s_loc[lidx + 2]);
          }
        }
        record_spr(0, cxi - 26, cyi - 26, cxi + 26, cyi + 26);

        // Trailing 5 orbital breadcrumb dots (slots 1..5)
        for (int step = 1; step <= 5; step++) {
          float t_lat, t_lon;
          if (step < tr_cnt_) {
            t_lat = tr_lat_[step];
            t_lon = tr_lon_[step];
          } else {
            t_lat = std::clamp(live_lat - p.iss_vel_lat * (float)(step * 70), -53.0f, 53.0f);
            t_lon = live_lon - p.iss_vel_lon * (float)(step * 70);
          }
          while (t_lon > 180.0f) t_lon -= 360.0f;
          while (t_lon < -180.0f) t_lon += 360.0f;
          if (t_lon < -166.0f || t_lon > 177.0f) continue;
          float tx, ty_p;
          latlon_to_map_xy(t_lat, t_lon, tx, ty_p);
          if (abs((int)tx - cxi) < 13 && abs((int)ty_p - cyi) < 10) continue;
          spr_disc(tx, ty_p, 2.5f - (float)step * 0.25f, 56.0f, 210.0f, 255.0f, 0.88f - (float)step * 0.13f);
          record_spr((size_t)step, (int)tx - 4, (int)ty_p - 4, (int)tx + 4, (int)ty_p + 4);
        }

        // NEW: Subtle Forward Orbit Projection (4 dots ~3..12 min ahead, slots 6..9)
        for (int step = 1; step <= 4; step++) {
          float dt_fwd = (float)(step * 140);
          // Account for 51.6-degree orbital inclination curve near extremes
          float f_lat = live_lat + p.iss_vel_lat * dt_fwd;
          if (f_lat > 51.6f) f_lat = 103.2f - f_lat;
          if (f_lat < -51.6f) f_lat = -103.2f - f_lat;
          f_lat = std::clamp(f_lat, -52.0f, 52.0f);
          float f_lon = live_lon + p.iss_vel_lon * dt_fwd;
          while (f_lon > 180.0f) f_lon -= 360.0f;
          while (f_lon < -180.0f) f_lon += 360.0f;
          if (f_lon < -166.0f || f_lon > 177.0f) continue;
          float fx, fy;
          latlon_to_map_xy(f_lat, f_lon, fx, fy);
          if (abs((int)fx - cxi) < 15 && abs((int)fy - cyi) < 12) continue;
          spr_disc(fx, fy, 1.8f - (float)step * 0.15f, (float)ring_r_c, 225.0f, (float)ring_b_c,
                   0.52f - (float)step * 0.09f);
          record_spr((size_t)(5 + step), (int)fx - 3, (int)fy - 3, (int)fx + 3, (int)fy + 3);
        }

        int bx_pos = (cxi > 700) ? (cxi - 66) : (cxi + 20);
        int by_pos = 74 + std::clamp(cyi - 10, 4, 296);
        update_badge(near_home ? 11 : 1, "ISS", near_home ? 0x2B1D02 : 0x041E2B, near_home ? 0xFFD166 : 0x38BDF8,
                     near_home ? 0xFFE58F : 0x7DD3FC, bx_pos, by_pos);
      }
    } else if (mode == 2) {
      constexpr float kBalSpan = 368.0f;
      for (size_t i = 0; i < 10; i++) {
        float bx = 38.0f + (float)i * 78.0f + 8.0f * sinf((float)tk * 0.14f + (float)i);
        float prog_b = fmodf((float)tk * (1.85f + (float)(i % 3) * 0.38f) + (float)i * 36.8f, kBalSpan);
        float by = 338.0f - prog_b;
        float bfade = smooth01(prog_b / 24.0f) * smooth01((kBalSpan - prog_b) / 28.0f);
        if (bfade > 0.02f) {
          float cr = (i % 3 == 0) ? 255.0f : ((i % 3 == 1) ? 56.0f : 255.0f);
          float cg = (i % 3 == 0) ? 70.0f : ((i % 3 == 1) ? 195.0f : 205.0f);
          float cb = (i % 3 == 0) ? 145.0f : ((i % 3 == 1) ? 255.0f : 35.0f);
          for (int sy = 8; sy <= 20; sy++) {
            int sx_w = (int)roundf(bx + 2.2f * sinf((float)sy * 0.45f + (float)tk * 0.3f));
            blend_spr_px(sx_w, (int)by + sy, 230.0f, 230.0f, 235.0f, 0.70f * bfade);
          }
          spr_disc(bx, by - 2.0f, 7.6f, cr, cg, cb, 0.88f * bfade);
          spr_disc(bx, by + 2.2f, 6.4f, cr, cg, cb, 0.88f * bfade);
          spr_disc(bx, by + 8.2f, 1.6f, cr, cg, cb, 0.95f * bfade);
          spr_disc(bx - 2.6f, by - 4.2f, 2.1f, 255.0f, 255.0f, 255.0f, 0.88f * bfade);
          record_spr(i, (int)bx - 11, (int)by - 12, (int)bx + 11, (int)by + 22);
        }
      }
      constexpr float kConfSpan = 340.0f;
      for (size_t c = 0; c < 8; c++) {
        float cx_c = 72.0f + (float)c * 92.0f + 10.0f * cosf((float)tk * 0.22f + (float)c);
        float prog_c = fmodf((float)tk * (2.2f + (float)(c & 1) * 0.55f) + (float)c * 42.5f, kConfSpan);
        float cy_c = -8.0f + prog_c;
        float cfade = smooth01(prog_c / 20.0f) * smooth01((kConfSpan - prog_c) / 20.0f);
        if (cfade > 0.02f) {
          float cr = (c & 1) ? 168.0f : 52.0f, cg = (c & 1) ? 85.0f : 235.0f, cb = (c & 1) ? 255.0f : 155.0f;
          spr_disc(cx_c, cy_c, 2.6f, cr, cg, cb, 0.92f * cfade);
          record_spr(10 + c, (int)cx_c - 5, (int)cy_c - 5, (int)cx_c + 5, (int)cy_c + 5);
        }
      }
    } else if (mode == 3) {
      constexpr size_t kFlakes = 14;
      constexpr float kSpanY = 180.0f;
      for (size_t i = 0; i < kFlakes; i++) {
        float sx = 22.0f + (float)i * (756.0f / (float)kFlakes) +
                   9.5f * sinf((float)tk * (0.11f + (float)(i % 3) * 0.02f) + (float)i * 1.7f);
        float rel_y = fmodf((float)tk * (1.20f + (float)(i % 4) * 0.30f) + (float)i * 37.0f, kSpanY);
        float sy = 2.0f + rel_y;
        float alpha = 0.90f * smooth01(rel_y / 20.0f) * smooth01((kSpanY - rel_y) / 46.0f);
        if (alpha > 0.02f) {
          if (i % 3 == 0) {
            int cxs = (int)roundf(sx), cys = (int)roundf(sy);
            spr_disc(sx, sy, 2.3f, 240.0f, 248.0f, 255.0f, alpha);
            for (int arm = -4; arm <= 4; arm++) {
              blend_spr_px(cxs + arm, cys, 225.0f, 242.0f, 255.0f, alpha * 0.72f);
              blend_spr_px(cxs, cys + arm, 225.0f, 242.0f, 255.0f, alpha * 0.72f);
            }
            record_spr(i, cxs - 6, cys - 6, cxs + 6, cys + 6);
          } else {
            spr_disc(sx, sy, (i & 1) ? 2.4f : 1.9f, 245.0f, 250.0f, 255.0f, alpha);
            record_spr(i, (int)sx - 5, (int)sy - 5, (int)sx + 5, (int)sy + 5);
          }
        }
      }

      float npx, npy;
      latlon_to_map_xy(74.8f, 0.0f, npx, npy);
      float np_pulse = (float)(tk % 28UL) / 28.0f;
      float np_env = sinf(np_pulse * (float)M_PI);
      if (np_env > 0.02f) {
        spr_disc(npx, npy, 5.0f + np_pulse * 9.5f, 255.0f, 70.0f, 85.0f, np_env * 0.52f);
        record_spr(14, (int)npx - 16, (int)npy - 16, (int)npx + 16, (int)npy + 16);
      }

      const float kAdvCities[48] = {
          66.50f,  25.73f, -36.85f, 174.76f, -33.87f, 151.21f,  35.68f, 139.69f,
          37.57f, 126.98f,  39.90f, 116.41f,   1.35f, 103.82f,  13.76f, 100.50f,
          19.08f,  72.88f,  25.20f,  55.27f,  55.76f,  37.62f,  -1.29f,  36.82f,
          30.04f,  31.24f, -33.92f,  18.42f,  37.98f,  23.73f,  52.52f,  13.40f,
          41.90f,  12.50f,  48.86f,   2.35f,  64.15f, -21.94f, -22.91f, -43.17f,
          40.71f, -74.01f,  43.65f, -79.38f,  34.05f,-118.24f, home_lat, home_lon,
      };
      int adv_days = (p.loc_valid && p.loc_month == 12) ? std::clamp(p.loc_day, 1, 24) : 16;
      size_t tw_cnt = (size_t)std::min(adv_days, 5);
      for (size_t k = 0; k < tw_cnt; k++) {
        float phase = fmodf((float)tk / 32.0f + (float)k * ((float)adv_days / (float)tw_cnt), (float)adv_days);
        int ci = std::clamp((int)floorf(phase), 0, adv_days - 1);
        float frac = phase - (float)ci;
        float tw_a = sinf(frac * (float)M_PI);
        if (tw_a > 0.03f) {
          float cx_a, cy_a;
          latlon_to_map_xy(kAdvCities[ci * 2], kAdvCities[ci * 2 + 1], cx_a, cy_a);
          spr_disc(cx_a, cy_a, 4.6f, 255.0f, 230.0f, 110.0f, tw_a * 0.55f);
          spr_disc(cx_a, cy_a, 1.9f, 255.0f, 255.0f, 235.0f, tw_a * 0.95f);
          record_spr(15 + k, (int)cx_a - 7, (int)cy_a - 7, (int)cx_a + 7, (int)cy_a + 7);
        }
      }
    } else if (mode == 9) {
      constexpr float kXmasSpan = 336.0f;
      for (size_t i = 0; i < 16; i++) {
        float sx = 24.0f + (float)i * 47.0f + 8.5f * sinf((float)tk * 0.13f + (float)i * 1.4f);
        float rel_y = fmodf((float)tk * (1.45f + (float)(i % 3) * 0.42f) + (float)i * 21.0f, kXmasSpan);
        float sy = -6.0f + rel_y;
        float fade = smooth01(rel_y / 22.0f) * smooth01((kXmasSpan - rel_y) / 28.0f);
        if (fade > 0.02f) {
          spr_disc(sx, sy, (i % 3 == 0) ? 2.9f : 2.0f, 245.0f, 250.0f, 255.0f, 0.88f * fade);
          record_spr(i, (int)sx - 5, (int)sy - 5, (int)sx + 5, (int)sy + 5);
        }
      }
    } else if (mode == 4) {
      std::string h_lbl = to_upper_ascii(p.home_label);
      struct SantaStop {
        float lat;
        float lon;
        const char *name;
        float utc_h;
      };
      const SantaStop kStops[30] = {
          {-36.85f, 174.76f, "AUCKLAND NZ",     10.00f}, {-27.47f, 153.03f, "BRISBANE AUS",    11.00f},
          {-33.87f, 151.21f, "SYDNEY AUS",      11.50f}, { 35.68f, 139.69f, "TOKYO JAPAN",     12.30f},
          { 37.57f, 126.98f, "SEOUL KOREA",     13.00f}, {-31.95f, 115.86f, "PERTH AUS",       13.50f},
          { 39.90f, 116.41f, "BEIJING CHINA",   14.00f}, { 22.32f, 114.17f, "HONG KONG",       14.40f},
          {  1.35f, 103.82f, "SINGAPORE",       14.85f}, { 13.76f, 100.50f, "BANGKOK",         15.30f},
          { 28.61f,  77.21f, "NEW DELHI",       16.00f}, { 19.08f,  72.88f, "MUMBAI INDIA",    16.45f},
          { 25.20f,  55.27f, "DUBAI UAE",       17.10f}, { 55.76f,  37.62f, "MOSCOW",          17.60f},
          { -1.29f,  36.82f, "NAIROBI KENYA",   18.00f}, {-33.92f,  18.42f, "CAPE TOWN SA",    18.35f},
          { 30.04f,  31.24f, "CAIRO EGYPT",     18.65f}, { 37.98f,  23.73f, "ATHENS GREECE",   18.90f},
          { 52.23f,  21.01f, "WARSAW POLAND",   19.10f}, { 52.52f,  13.40f, "BERLIN GERMANY",  19.25f},
          { 41.90f,  12.50f, "ROME ITALY",      19.45f}, { 48.86f,   2.35f, "PARIS FRANCE",    20.40f},
          { 51.51f,  -0.13f, "LONDON UK",       23.50f}, {home_lat, home_lon, h_lbl.c_str(),   23.95f},
          { 64.15f, -21.94f, "REYKJAVIK",       24.60f}, {-22.91f, -43.17f, "RIO BRAZIL",      26.00f},
          { 40.71f, -74.01f, "NEW YORK USA",    28.50f}, { 41.88f, -87.63f, "CHICAGO USA",     29.50f},
          { 34.05f,-118.24f, "LOS ANGELES",     31.50f}, { 21.31f,-157.86f, "HONOLULU HAWAII", 33.50f},
      };

      int cur_wp = -1;
      float s_lat = 74.8f, s_lon = 0.0f;
      const char *c_name = "NORTH POLE";
      int gifts_m = 0;

      if (p.manual_santa_preview) {
        float tour = fmodf((float)tk / 60.0f, 31.0f);
        if (tour < 1.0f) {
          float e = smooth01(tour);
          cur_wp = 0;
          s_lat = 74.8f + (kStops[0].lat - 74.8f) * e;
          s_lon = 0.0f + (kStops[0].lon - 0.0f) * e;
          c_name = kStops[0].name;
          gifts_m = (int)(e * 180.0f);
        } else if (tour < 30.0f) {
          int idx_s = std::clamp((int)floorf(tour - 1.0f), 0, 28);
          float frac = smooth01((tour - 1.0f) - (float)idx_s);
          cur_wp = idx_s + 1;
          s_lat = kStops[idx_s].lat + (kStops[idx_s + 1].lat - kStops[idx_s].lat) * frac;
          s_lon = kStops[idx_s].lon + (kStops[idx_s + 1].lon - kStops[idx_s].lon) * frac;
          c_name = kStops[(frac < 0.5f) ? idx_s : (idx_s + 1)].name;
          gifts_m = (int)(((float)idx_s + frac + 1.0f) * 185.0f);
        } else {
          float e_ret = smooth01(tour - 30.0f);
          cur_wp = 30;
          s_lat = kStops[29].lat + (74.8f - kStops[29].lat) * e_ret;
          s_lon = kStops[29].lon + (0.0f - kStops[29].lon) * e_ret;
          c_name = (e_ret < 0.5f) ? kStops[29].name : "NORTH POLE";
          gifts_m = 5600;
        }
      } else {
        float u_hr = p.utc_valid ? p.utc_hr : 18.8f;
        if (p.utc_valid && p.utc_month == 12 && p.utc_day == 25 && u_hr < 10.0f) u_hr += 24.0f;
        if (u_hr < 9.5f) {
          cur_wp = -1;
          s_lat = 74.8f;
          s_lon = 0.0f;
          c_name = "NORTH POLE";
          gifts_m = 0;
        } else if (u_hr < kStops[0].utc_h) {
          float f0 = smooth01((u_hr - 9.5f) / (kStops[0].utc_h - 9.5f));
          cur_wp = 0;
          s_lat = 74.8f + (kStops[0].lat - 74.8f) * f0;
          s_lon = 0.0f + (kStops[0].lon - 0.0f) * f0;
          c_name = "AUCKLAND NZ";
          gifts_m = (int)(f0 * 120.0f);
        } else {
          cur_wp = 30;
          s_lat = kStops[29].lat;
          s_lon = kStops[29].lon;
          c_name = kStops[29].name;
          gifts_m = 5600;
          for (int w = 0; w < 29; w++) {
            if (u_hr >= kStops[w].utc_h && u_hr < kStops[w + 1].utc_h) {
              float fw = smooth01((u_hr - kStops[w].utc_h) / (kStops[w + 1].utc_h - kStops[w].utc_h));
              cur_wp = w + 1;
              s_lat = kStops[w].lat + (kStops[w + 1].lat - kStops[w].lat) * fw;
              s_lon = kStops[w].lon + (kStops[w + 1].lon - kStops[w].lon) * fw;
              c_name = (fw < 0.45f) ? kStops[w].name : kStops[w + 1].name;
              gifts_m = 120 + (int)(((float)w + fw) * 188.0f);
              break;
            }
          }
        }
      }

      int dist_km = (int)roundf(haversine_km(home_lat, home_lon, s_lat, s_lon));
      if (p.santa_wp_idx != nullptr) {
        bool wp_changed = (cur_wp != *p.santa_wp_idx);
        *p.santa_wp_idx = cur_wp;
        if (wp_changed && p.force_redraw != nullptr) *p.force_redraw = true;
      }
      if (p.santa_lat != nullptr) *p.santa_lat = s_lat;
      if (p.santa_lon != nullptr) *p.santa_lon = s_lon;
      if (p.santa_city != nullptr) *p.santa_city = c_name;
      if (p.santa_dist_km != nullptr) *p.santa_dist_km = dist_km;
      if (p.santa_gifts_m != nullptr) *p.santa_gifts_m = gifts_m;

      float sx, sy;
      latlon_to_map_xy(s_lat, s_lon, sx, sy);
      spr_disc(sx, sy, 12.5f, 5.0f, 5.0f, 5.0f, 0.82f);
      float pulse = (float)(tk % 20UL) / 20.0f;
      spr_disc(sx, sy, 6.0f + pulse * 10.0f, 255.0f, 200.0f, 40.0f, sinf(pulse * (float)M_PI) * 0.48f);
      if (cur_wp >= 0) {
        for (int tr = 1; tr <= 4; tr++) {
          spr_disc(sx + (float)tr * 5.8f, sy + sinf((float)tk * 0.35f + (float)tr) * 1.8f, 2.3f - (float)tr * 0.2f,
                   255.0f, 215.0f, 65.0f, 0.82f - (float)tr * 0.14f);
        }
      }
      spr_disc(sx, sy, 5.5f, 240.0f, 30.0f, 40.0f, 1.0f);
      spr_disc(sx - 7.0f, sy - 1.0f, 2.6f, 255.0f, 35.0f, 35.0f, 1.0f);
      record_spr(0, (int)sx - 20, (int)sy - 20, (int)sx + 30, (int)sy + 20);

      int cxi = (int)roundf(sx), cyi = (int)roundf(sy);
      int bx_pos = (cxi > 685) ? (cxi - 78) : (cxi + 20);
      int by_pos = 74 + std::clamp(cyi - 10, 4, 296);
      update_badge(4, "SANTA", 0x2B0609, 0xFFB830, 0xFFD166, bx_pos, by_pos);
    } else if (mode == 5) {
      constexpr float kBatSpan = 832.0f;
      for (size_t i = 0; i < 6; i++) {
        float prog_b = fmodf((float)tk * (2.4f + (float)(i & 1) * 0.85f) + (float)i * 138.6f, kBatSpan);
        float bx = 816.0f - prog_b;
        float bfade = smooth01(prog_b / 28.0f) * smooth01((kBatSpan - prog_b) / 28.0f);
        if (bfade > 0.02f) {
          float by = 42.0f + (float)i * 42.0f + 6.0f * sinf((float)tk * 0.25f + (float)i);
          float flap = sinf((float)tk * 0.95f + (float)i * 1.3f) * 3.6f;
          spr_disc(bx, by, 3.3f, 255.0f, 125.0f, 15.0f, 0.95f * bfade);
          spr_disc(bx - 5.5f, by + flap * 0.5f, 2.6f, 255.0f, 125.0f, 15.0f, 0.90f * bfade);
          spr_disc(bx - 9.5f, by + flap, 2.0f, 255.0f, 125.0f, 15.0f, 0.85f * bfade);
          spr_disc(bx + 5.5f, by + flap * 0.5f, 2.6f, 255.0f, 125.0f, 15.0f, 0.90f * bfade);
          spr_disc(bx + 9.5f, by + flap, 2.0f, 255.0f, 125.0f, 15.0f, 0.85f * bfade);
          record_spr(i, (int)bx - 13, (int)by - 8, (int)bx + 13, (int)by + 8);
        }
      }
      for (size_t g = 0; g < 2; g++) {
        float gx = 160.0f + (float)g * 440.0f + 16.0f * sinf((float)tk * 0.08f + (float)g * 2.0f);
        float gy = 140.0f + 14.0f * cosf((float)tk * 0.14f + (float)g);
        spr_disc(gx, gy - 2.0f, 7.0f, 225.0f, 240.0f, 255.0f, 0.65f);
        spr_disc(gx, gy + 4.0f, 6.5f, 225.0f, 240.0f, 255.0f, 0.55f);
        spr_disc(gx - 2.5f, gy - 3.0f, 1.5f, 20.0f, 10.0f, 30.0f, 0.95f);
        spr_disc(gx + 2.5f, gy - 3.0f, 1.5f, 20.0f, 10.0f, 30.0f, 0.95f);
        record_spr(6 + g, (int)gx - 10, (int)gy - 11, (int)gx + 10, (int)gy + 12);
      }
    } else if (mode == 6) {
      static const float kEggs[] = {42.0f, -100.0f, -16.0f, -56.0f, 8.0f, 22.0f, 34.0f, 104.0f, -25.0f, 134.0f};
      for (size_t e = 0; e < 5; e++) {
        float ex, ey;
        latlon_to_map_xy(kEggs[e * 2], kEggs[e * 2 + 1], ex, ey);
        float ang = (float)tk * 0.22f + (float)e * 1.25f;
        spr_disc(ex + cosf(ang) * 11.5f, ey + sinf(ang) * 11.5f, 2.2f, 255.0f, 230.0f, 65.0f, 0.92f);
        spr_disc(ex - cosf(ang) * 11.5f, ey - sinf(ang) * 11.5f, 1.7f, 255.0f, 255.0f, 200.0f, 0.80f);
        record_spr(e, (int)ex - 15, (int)ey - 15, (int)ex + 15, (int)ey + 15);
      }
    } else if (mode == 7) {
      for (size_t f = 0; f < 4; f++) {
        uint32_t cyc = (tk + f * 9UL) % 36UL;
        uint32_t burst_id = (tk + f * 9UL) / 36UL;
        float fx = 110.0f + (float)((burst_id * 173UL + f * 165UL) % 580UL);
        float peak_y = 58.0f + (float)((burst_id * 97UL + f * 53UL) % 130UL);
        float cr = 255.0f, cg = (f & 1) ? 205.0f : 85.0f, cb = (f % 3 == 0) ? 245.0f : 35.0f;

        if (cyc < 12UL) {
          float p_up = (float)cyc / 11.0f;
          float lfade = smooth01(p_up / 0.25f);
          float ry = 312.0f - (312.0f - peak_y) * (1.0f - (1.0f - p_up) * (1.0f - p_up));
          spr_disc(fx, ry, 2.6f, 255.0f, 235.0f, 140.0f, 0.98f * lfade);
          spr_disc(fx, ry + 5.5f, 2.0f, 255.0f, 145.0f, 30.0f, 0.78f * lfade);
          spr_disc(fx, ry + 11.0f, 1.5f, 255.0f, 80.0f, 20.0f, 0.50f * lfade);
          record_spr(f, (int)fx - 5, (int)ry - 5, (int)fx + 5, (int)ry + 14);
        } else {
          float prog = (float)(cyc - 12UL) / 23.0f;
          float rad = 2.0f + prog * 24.0f;
          float grav = prog * prog * 14.0f;
          float op = (1.0f - prog) * (1.0f - prog) * 0.98f;
          if (op > 0.02f) {
            spr_disc(fx, peak_y + grav * 0.4f, rad * 0.5f, cr, cg, cb, op * 0.24f);
            for (int k = 0; k < 12; k++) {
              float a = (float)k * 0.5236f;
              spr_disc(fx + cosf(a) * rad, peak_y + sinf(a) * rad + grav, 2.2f, cr, cg, cb, op);
              if ((k & 1) == 0) {
                spr_disc(fx + cosf(a + 0.26f) * (rad * 0.55f),
                         peak_y + sinf(a + 0.26f) * (rad * 0.55f) + grav * 0.6f, 1.6f, 255.0f, 240.0f, 160.0f,
                         op * 0.85f);
              }
            }
            record_spr(f, (int)fx - 30, (int)peak_y - 30, (int)fx + 30, (int)(peak_y + 30.0f + grav));
          }
        }
      }
    } else if (mode == 8) {
      constexpr float kTieSpan = 832.0f;
      for (size_t s = 0; s < 2; s++) {
        float prog_t = fmodf((float)tk * (3.2f + (float)s * 0.9f) + (float)s * 416.0f, kTieSpan);
        float sx = -16.0f + prog_t;
        float tfade = smooth01(prog_t / 28.0f) * smooth01((kTieSpan - prog_t) / 28.0f);
        float sy = 75.0f + (float)s * 110.0f + 10.0f * sinf((float)tk * 0.12f + (float)s);
        if (tfade > 0.02f) {
          for (int wy = -6; wy <= 6; wy++) {
            blend_spr_px((int)sx - 6, (int)sy + wy, 225.0f, 235.0f, 250.0f, 0.95f * tfade);
            blend_spr_px((int)sx + 6, (int)sy + wy, 225.0f, 235.0f, 250.0f, 0.95f * tfade);
          }
          spr_disc(sx, sy, 3.4f, 210.0f, 225.0f, 245.0f, tfade);
          record_spr(s, (int)sx - 9, (int)sy - 9, (int)sx + 9, (int)sy + 9);

          float l_prog = fmodf((float)(tk + s * 7UL) / 14.0f, 1.0f);
          float l_alpha = sinf(l_prog * (float)M_PI) * tfade;
          float lx = sx + 12.0f + l_prog * 76.0f;
          if (l_alpha > 0.03f && lx < 790.0f) {
            for (int b = 0; b < 7; b++) {
              blend_spr_px((int)lx + b, (int)sy - 2, 50.0f, 255.0f, 90.0f, 0.95f * l_alpha);
              blend_spr_px((int)lx + b, (int)sy + 2, 50.0f, 255.0f, 90.0f, 0.95f * l_alpha);
            }
            record_spr(2 + s, (int)lx - 2, (int)sy - 4, (int)lx + 9, (int)sy + 4);
          }
        }
      }
    }

    // Subtle Shooting Star / Meteor Shower easter egg (Perseids Aug 11-13, Geminids Dec 13-14, or occasional twilight streak)
    if (p.sleep_phase == 0 && mode == 1) {
      bool shower = p.loc_valid && is_meteor_shower(p.loc_month, p.loc_day);
      uint32_t m_period = shower ? 55UL : 240UL;
      uint32_t m_phase = tk % m_period;
      if (m_phase < 14UL) {
        uint32_t cycle = tk / m_period;
        float start_x = 120.0f + (float)((cycle * 197UL) % 520UL);
        float start_y = 22.0f + (float)((cycle * 73UL) % 90UL);
        float frac = (float)m_phase / 14.0f;
        float mx = start_x + frac * 78.0f;
        float my = start_y + frac * 34.0f;
        float env = sinf(frac * (float)M_PI);
        for (int t = 0; t < 12; t++) {
          float tx = mx - (float)t * 2.4f;
          float ty = my - (float)t * 1.05f;
          float ta = env * (1.0f - (float)t / 12.0f);
          spr_disc(tx, ty, (t == 0) ? 1.8f : 1.0f, 255.0f, 235.0f, 185.0f, ta * 0.92f);
        }
        record_spr(22, (int)mx - 34, (int)my - 18, (int)mx + 5, (int)my + 5);
      }
    }

    // Keep Home Pin layered on top if any sprite passes over it
    float pxf, pyf;
    latlon_to_map_xy(home_lat, home_lon, pxf, pyf);
    pyf += 1.5f;
    int px0 = (int)pxf - 7, px1 = (int)pxf + 7, py0 = (int)pyf - 7, py1 = (int)pyf + 7;
    bool pin_overlapped = false;
    for (size_t i = 0; i < kMaxSpr; i++) {
      if (prev_[i].used && prev_[i].x0 <= px1 && prev_[i].x1 >= px0 && prev_[i].y0 <= py1 && prev_[i].y1 >= py0) {
        pin_overlapped = true;
        break;
      }
    }
    if (pin_overlapped) {
      spr_disc(pxf, pyf, 6.2f, 0.0f, 0.0f, 0.0f, 0.95f);
      spr_disc(pxf, pyf, 5.2f, 255.0f, (mode == 2) ? 215.0f : 246.0f, (mode == 2) ? 40.0f : 234.0f, 1.0f);
      spr_disc(pxf, pyf, 3.6f, 255.0f, 122.0f, 0.0f, 1.0f);
    }

    if (p.map_just_flushed != nullptr && *p.map_just_flushed) {
      *p.map_just_flushed = false;
      lv_obj_invalidate(cv);
    } else {
      for (size_t i = 0; i < kMaxSpr; i++) {
        if (old_prev[i].used) inv_box(old_prev[i].x0, old_prev[i].y0, old_prev[i].x1, old_prev[i].y1);
      }
    }
  }

 private:
  struct DirtyBox {
    int16_t x0, y0, x1, y1;
    bool used;
  };
  DirtyBox prev_[kMaxSpr]{};
  bool inited_{false};
  uint32_t anim_tick_{0};
  int badge_last_x_{-999};
  int badge_last_y_{-999};
  float disp_lat_{0.0f};
  float disp_lon_{0.0f};
  bool disp_init_{false};
  float tr_lat_[5]{0};
  float tr_lon_[5]{0};
  int tr_cnt_{0};
};

inline SpriteEngine &sprite_engine() {
  static SpriteEngine engine;
  return engine;
}

}  // namespace world_clock
