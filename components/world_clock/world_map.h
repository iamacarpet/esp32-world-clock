#pragma once

#include "clock_math.h"
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "esphome/core/application.h"

namespace world_clock {

constexpr size_t kMapWidth = 800;
constexpr size_t kMapHeight = 322;
constexpr size_t kBgBytes = kMapWidth * kMapHeight * 3UL;
constexpr size_t kScratchBytes = 24576UL;

struct WorldMapParams {
  float home_lat{51.5074f};
  float home_lon{-0.1278f};
  int doy{172};
  float utc_hr{12.0f};
  int loc_month{1};
  int loc_day{1};
  bool loc_valid{false};
  uint8_t pal{0};          // 0=Day, 1=Sleep Red, 2=Amber (Morning Quiet / Evening Wind-Down)
  uint8_t mode{1};         // 0=Classic, 1=ISS, 2=Bday, 3=Advent, 4=Santa, 5=Halloween, 6=Easter, 7=Fireworks, 8=StarWars, 9=XmasDay
  uint8_t sleep_phase{0};  // 0=Day, 1=Sleep, 2=Morning Quiet, 3=Evening Wind-Down
  int santa_wp_idx{-1};
  bool show_night_lights{true};
  bool keep_santa_winddown{true};
};

inline uint8_t *ensure_bg_cache(uint8_t *&bg_cache) {
  if (bg_cache == nullptr) {
    bg_cache = (uint8_t *)heap_caps_malloc(kBgBytes + kScratchBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return bg_cache;
}

inline void render_world_map_canvas(lv_obj_t *cv, uint8_t *&bg_cache, const WorldMapParams &p,
                                    bool &sprites_dirty, bool &map_just_flushed) {
  if (cv == nullptr) return;
  esphome::App.feed_wdt();

  uint8_t *bg = ensure_bg_cache(bg_cache);
  if (bg == nullptr) return;
  memset(bg, 0, kBgBytes);
  float *wave_lut = (float *)(bg + kBgBytes);

  auto set_mem_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= (int)kMapWidth || y < 0 || y >= (int)kMapHeight) return;
    size_t idx = ((size_t)y * kMapWidth + (size_t)x) * 3UL;
    bg[idx] = r;
    bg[idx + 1] = g;
    bg[idx + 2] = b;
  };

  auto blend_mem_px = [&](int x, int y, float cr, float cg, float cb, float a) {
    if (x < 0 || x >= (int)kMapWidth || y < 0 || y >= (int)kMapHeight || a <= 0.01f) return;
    a = std::clamp(a, 0.0f, 1.0f);
    size_t idx = ((size_t)y * kMapWidth + (size_t)x) * 3UL;
    bg[idx] = (uint8_t)((float)bg[idx] * (1.0f - a) + cr * a);
    bg[idx + 1] = (uint8_t)((float)bg[idx + 1] * (1.0f - a) + cg * a);
    bg[idx + 2] = (uint8_t)((float)bg[idx + 2] * (1.0f - a) + cb * a);
  };

  auto blend_disc = [&](float cx_f, float cy_f, float rad, float cr, float cg, float cb, float op) {
    int x0 = std::max(0, (int)floorf(cx_f - rad - 1.0f));
    int x1 = std::min((int)kMapWidth - 1, (int)ceilf(cx_f + rad + 1.0f));
    int y0 = std::max(0, (int)floorf(cy_f - rad - 1.0f));
    int y1 = std::min((int)kMapHeight - 1, (int)ceilf(cy_f + rad + 1.0f));
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        float d = sqrtf(((float)x - cx_f) * ((float)x - cx_f) + ((float)y - cy_f) * ((float)y - cy_f));
        float a = std::clamp(rad + 0.5f - d, 0.0f, 1.0f) * op;
        if (a > 0.01f) blend_mem_px(x, y, cr, cg, cb, a);
      }
    }
  };

  struct MapRow {
    uint64_t lo;
    uint8_t hi;
  };
  static const MapRow kMap[] = {
      {0x03C000007FC00000ULL, 0x00U}, {0x0FF000007F83F800ULL, 0x00U}, {0xFFFF01007F8FFF80ULL, 0x07U},
      {0xFFFFF3C01F1FFFFFULL, 0xFFU}, {0xFFFFFFE0CF3FFFFFULL, 0xFFU}, {0xFFFFFFF0073FFFFFULL, 0x3FU},
      {0xFFFFFF60007FFFC6ULL, 0x0BU}, {0xFFFFFF88007FFF00ULL, 0x08U}, {0xFFFFFFF6007FFE00ULL, 0x00U},
      {0xFFFFFFF8007FFE00ULL, 0x00U}, {0xBFFFF1CC001FFE00ULL, 0x01U}, {0xBFFFFF86000FFC00ULL, 0x00U},
      {0xCFFFFC1C0007FC00ULL, 0x00U}, {0x2FFFF83E0007F800ULL, 0x00U}, {0x0FFFFFFF00047800ULL, 0x00U},
      {0x07FE7BFF000C6000ULL, 0x00U}, {0x01CC3FFF8001C000ULL, 0x00U}, {0x11840FFF80030000ULL, 0x00U},
      {0x10881FFF007C0000ULL, 0x00U}, {0x04801FE000F80000ULL, 0x00U}, {0x4D800FE001FC0000ULL, 0x00U},
      {0x860007E00FFC0000ULL, 0x01U}, {0x000007C007F80000ULL, 0x00U}, {0x600017E007F00000ULL, 0x01U},
      {0xF80013C007F00000ULL, 0x01U}, {0xFC001BC003F00000ULL, 0x03U}, {0xFC0001C001F00000ULL, 0x03U},
      {0xD800008000F00000ULL, 0x03U}, {0x0000000000700000ULL, 0xC3U}, {0x0000000000300000ULL, 0x41U},
      {0x0000000000180000ULL, 0x60U}, {0x0000000000100000ULL, 0x00U}, {0x0000000000100000ULL, 0x00U},
  };

  static const float kRowLat[] = {
      75.50f,  73.05f,  70.46f,  67.72f,  64.83f,  61.78f,  58.57f,  55.20f,  51.67f,  47.98f,  44.13f,
      40.12f,  35.98f,  31.69f,  27.29f,  22.77f,  18.15f,  13.46f,   8.71f,   3.91f,  -0.89f,  -5.70f,
     -10.48f, -15.21f, -19.88f, -24.46f, -28.94f, -33.30f, -37.54f, -41.63f, -45.58f, -49.37f, -53.00f,
  };

  auto is_land = [&](int r, int c) -> bool {
    if (r < 0 || r >= 33 || c < 0 || c >= 72) return false;
    return (c < 64) ? (((kMap[r].lo >> c) & 1ULL) != 0ULL) : (((kMap[r].hi >> (c - 64)) & 1U) != 0U);
  };

  const float home_lat = std::clamp(p.home_lat, -53.0f, 75.5f);
  const float home_lon = std::clamp(p.home_lon, -167.0f, 178.0f);
  constexpr float kDeg2Rad = (float)M_PI / 180.0f;

  const uint8_t pal = p.pal;
  const uint8_t mode = p.mode;
  const bool santa_winddown = (p.sleep_phase == 3 && mode == 4 && p.keep_santa_winddown);
  const bool animations_active = (p.sleep_phase == 0 && mode != 0) || santa_winddown;
  const bool sleeping = (p.sleep_phase != 0);

  float gamma = 2.0f * (float)M_PI * ((float)(p.doy - 1) + (p.utc_hr - 12.0f) / 24.0f) / 365.0f;
  float dec = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma) - 0.006758f * cosf(2.0f * gamma) +
              0.000907f * sinf(2.0f * gamma);
  float eqt = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma) -
                         0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));
  float sub_lon = (12.0f - (p.utc_hr + eqt / 60.0f)) * 15.0f;
  while (sub_lon > 180.0f) sub_lon -= 360.0f;
  while (sub_lon < -180.0f) sub_lon += 360.0f;

  float sin_dec = sinf(dec), cos_dec = cosf(dec), tan_dec = tanf(dec);
  if (fabsf(tan_dec) < 1e-4f) tan_dec = (tan_dec >= 0.0f) ? 1e-4f : -1e-4f;

  // 1. Two-Tone Polar Aurora Curtain (Christmas Advent / Eve / Day)
  if (!sleeping && pal == 0 && (mode == 3 || mode == 9 || mode == 4)) {
    float phase_shift = p.utc_hr * 2.61799f;
    for (int x = 10; x < 790; x++) {
      wave_lut[x] = 0.5f + 0.5f * sinf((float)x * 0.028f + phase_shift) * cosf((float)x * 0.011f);
    }
    for (int y = 0; y < 56; y++) {
      float vy = 1.0f - (float)y / 56.0f;
      float env = vy * vy * 0.54f;
      float violet_mix = std::clamp(1.0f - (float)y / 26.0f, 0.0f, 1.0f);
      for (int x = 10; x < 790; x++) {
        float curtain = 6.0f * sinf((float)x * 0.019f - phase_shift);
        float band = expf(-0.008f * ((float)y - (18.0f + curtain)) * ((float)y - (18.0f + curtain)));
        float a = (wave_lut[x] * 0.65f + band * 0.55f) * env;
        if (a > 0.02f) {
          a = std::min(a, 0.62f);
          float ar_c = 28.0f * (1.0f - violet_mix) + 165.0f * violet_mix;
          float ag_c = 242.0f * (1.0f - violet_mix) + 75.0f * violet_mix;
          float ab_c = 168.0f * (1.0f - violet_mix) + 245.0f * violet_mix;
          set_mem_px(x, y, (uint8_t)(ar_c * a), (uint8_t)(ag_c * a), (uint8_t)(ab_c * a));
        }
      }
    }
  }

  // 2. Continent Dots
  float nr = (pal == 1) ? 34.0f : ((pal == 2) ? 40.0f : ((mode == 5) ? 46.0f : 30.0f));
  float ng = (pal == 1) ? 2.0f : ((pal == 2) ? 14.0f : ((mode == 5) ? 14.0f : 31.0f));
  float nb = (pal == 1) ? 4.0f : ((pal == 2) ? 2.0f : ((mode == 5) ? 62.0f : 34.0f));
  float dr = (pal == 1) ? 220.0f : ((pal == 2) ? 245.0f : 255.0f);
  float dg = (pal == 1) ? 18.0f : ((pal == 2) ? 115.0f : ((mode == 5) ? 132.0f : 245.0f));
  float db = (pal == 1) ? 24.0f : ((pal == 2) ? 12.0f : ((mode == 5) ? 18.0f : 232.0f));

  for (int r = 0; r < 33; r++) {
    if ((r & 7) == 0) esphome::App.feed_wdt();
    int cy = 11 + r * 9;
    float lat_r = kRowLat[r] * kDeg2Rad;
    for (int c = 0; c < 72; c++) {
      if (!is_land(r, c)) continue;
      int cx = 10 + c * 11;
      float lon = -167.0f + (float)c * (345.0f / 71.0f);
      float sin_alt =
          std::clamp(sinf(lat_r) * sin_dec + cosf(lat_r) * cos_dec * cosf((lon - sub_lon) * kDeg2Rad), -1.0f, 1.0f);
      float t = std::clamp((asinf(sin_alt) / kDeg2Rad + 3.5f) / 7.0f, 0.0f, 1.0f);
      float cr = nr + (dr - nr) * t, cg = ng + (dg - ng) * t, cb = nb + (db - nb) * t;
      for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
          float cov = std::clamp(3.65f - sqrtf((float)(dx * dx + dy * dy)), 0.0f, 1.0f);
          if (cov > 0.0f) blend_mem_px(cx + dx, cy + dy, cr, cg, cb, cov);
        }
      }
    }
  }

  esphome::App.feed_wdt();

  // 2b. Subtle Night-Side City Lights (Daytime & Evening Wind-Down only — warm gold glow on the night half!)
  if (p.show_night_lights && pal != 1 && p.sleep_phase != 2) {
    static const float kNightCities[] = {
        51.51f,  -0.13f,   // London
        48.86f,   2.35f,   // Paris
        40.42f,  -3.70f,   // Madrid
        52.52f,  13.40f,   // Berlin
        41.90f,  12.50f,   // Rome
        55.76f,  37.62f,   // Moscow
        30.04f,  31.24f,   // Cairo
        25.20f,  55.27f,   // Dubai
        19.08f,  72.88f,   // Mumbai
        28.61f,  77.21f,   // Delhi
        13.76f, 100.50f,   // Bangkok
         1.35f, 103.82f,   // Singapore
        22.32f, 114.17f,   // Hong Kong
        31.23f, 121.47f,   // Shanghai
        39.90f, 116.41f,   // Beijing
        37.57f, 126.98f,   // Seoul
        35.68f, 139.69f,   // Tokyo
       -33.87f, 151.21f,   // Sydney
       -36.85f, 174.76f,   // Auckland
       -26.20f,  28.05f,   // Johannesburg
        40.71f, -74.01f,   // New York
        41.88f, -87.63f,   // Chicago
        34.05f,-118.24f,   // Los Angeles
        19.43f, -99.13f,   // Mexico City
       -23.55f, -46.63f,   // Sao Paulo
       -34.60f, -58.38f,   // Buenos Aires
    };
    const int n_cities = (int)(sizeof(kNightCities) / sizeof(kNightCities[0])) / 2;
    for (int i = 0; i < n_cities; i++) {
      float clat = kNightCities[i * 2];
      float clon = kNightCities[i * 2 + 1];
      float lat_r = clat * kDeg2Rad;
      float sin_alt =
          std::clamp(sinf(lat_r) * sin_dec + cosf(lat_r) * cos_dec * cosf((clon - sub_lon) * kDeg2Rad), -1.0f, 1.0f);
      float alt_deg = asinf(sin_alt) / kDeg2Rad;
      if (alt_deg < -4.0f) {
        float night_fade = std::clamp((-alt_deg - 4.0f) / 10.0f, 0.0f, 1.0f);
        float cx_c, cy_c;
        latlon_to_map_xy(clat, clon, cx_c, cy_c);
        float glow_op = (pal == 2) ? 0.24f : 0.32f;
        float core_op = (pal == 2) ? 0.65f : 0.85f;
        blend_disc(cx_c, cy_c, 3.6f, 255.0f, 185.0f, 60.0f, glow_op * night_fade);
        blend_disc(cx_c, cy_c, 1.5f, 255.0f, 220.0f, 125.0f, core_op * night_fade);
      }
    }
  }

  // 3. Day/Night Solar Terminator Wave
  float lr = (pal == 1) ? 215.0f : ((pal == 2) ? 235.0f : ((mode == 5) ? 185.0f : 255.0f));
  float lg = (pal == 1) ? 18.0f : ((pal == 2) ? 105.0f : ((mode == 5) ? 70.0f : 158.0f));
  float lb = (pal == 1) ? 24.0f : ((pal == 2) ? 12.0f : ((mode == 5) ? 245.0f : 45.0f));

  for (int x = 6; x <= 794; x++) {
    if ((x & 127) == 0) esphome::App.feed_wdt();
    auto ty = [&](float xf) {
      float lon = -167.0f + (xf - 10.0f) * (345.0f / 781.0f);
      float lat_d = atanf(-cosf((lon - sub_lon) * kDeg2Rad) / tan_dec) / kDeg2Rad;
      float dummy_x, oy;
      latlon_to_map_xy(lat_d, lon, dummy_x, oy);
      return oy;
    };
    float y0 = ty((float)x - 0.5f), y1 = ty((float)x + 0.5f);
    if ((y0 < -4.0f && y1 < -4.0f) || (y0 > 323.0f && y1 > 323.0f)) continue;
    int ya = std::max(0, (int)floorf(std::min(y0, y1) - 2.6f));
    int yb = std::min((int)kMapHeight - 1, (int)ceilf(std::max(y0, y1) + 2.6f));
    float dy_s = y1 - y0, seg_l2 = 1.0f + dy_s * dy_s;
    float x_edge = (x < 30) ? std::clamp((float)(x - 6) / 24.0f, 0.0f, 1.0f)
                            : ((x > 770) ? std::clamp((float)(794 - x) / 24.0f, 0.0f, 1.0f) : 1.0f);

    for (int y = ya; y <= yb; y++) {
      float t = std::clamp((0.5f + ((float)y - y0) * dy_s) / seg_l2, 0.0f, 1.0f);
      float dist =
          sqrtf((-0.5f + t) * (-0.5f + t) + ((float)y - (y0 + t * dy_s)) * ((float)y - (y0 + t * dy_s)));
      float y_fade = 1.0f;
      if (y > 276) {
        float u = std::clamp(((float)y - 276.0f) / 44.0f, 0.0f, 1.0f);
        y_fade = (1.0f - u) * (1.0f - u);
      } else if (y < 12) {
        float u = std::clamp((12.0f - (float)y) / 10.0f, 0.0f, 1.0f);
        y_fade = (1.0f - u) * (1.0f - u);
      }
      float a = std::clamp(1.65f - dist, 0.0f, 1.0f) * 0.80f * x_edge * y_fade;
      blend_mem_px(x, y, lr, lg, lb, a);
    }
  }

  // 4. Static Holiday Map Landmarks (Daytime, plus Christmas Eve Wind-Down Santa breadcrumbs!)
  if (!sleeping || santa_winddown) {
    if (mode == 3 || mode == 9 || mode == 4) {
      float npx, npy;
      latlon_to_map_xy(74.8f, 0.0f, npx, npy);
      blend_disc(npx, npy, 12.5f, 34.0f, 215.0f, 110.0f, 0.34f);
      blend_disc(npx, npy, 6.2f, 255.0f, 255.0f, 255.0f, 0.96f);
      blend_disc(npx, npy, 3.8f, 239.0f, 48.0f, 56.0f, 1.0f);

      if (mode == 3 && !sleeping) {
        const float kAdvCities[48] = {
            66.50f,  25.73f, -36.85f, 174.76f, -33.87f, 151.21f,  35.68f, 139.69f,
            37.57f, 126.98f,  39.90f, 116.41f,   1.35f, 103.82f,  13.76f, 100.50f,
            19.08f,  72.88f,  25.20f,  55.27f,  55.76f,  37.62f,  -1.29f,  36.82f,
            30.04f,  31.24f, -33.92f,  18.42f,  37.98f,  23.73f,  52.52f,  13.40f,
            41.90f,  12.50f,  48.86f,   2.35f,  64.15f, -21.94f, -22.91f, -43.17f,
            40.71f, -74.01f,  43.65f, -79.38f,  34.05f,-118.24f, home_lat, home_lon,
        };
        int adv_days = (p.loc_valid && p.loc_month == 12) ? std::clamp(p.loc_day, 1, 24) : 16;
        for (int ci = 0; ci < adv_days; ci++) {
          float cx_a, cy_a;
          latlon_to_map_xy(kAdvCities[ci * 2], kAdvCities[ci * 2 + 1], cx_a, cy_a);
          float lr_c = (ci % 3 == 0) ? 255.0f : ((ci % 3 == 1) ? 255.0f : 48.0f);
          float lg_c = (ci % 3 == 0) ? 205.0f : ((ci % 3 == 1) ? 58.0f : 235.0f);
          float lb_c = (ci % 3 == 0) ? 45.0f : ((ci % 3 == 1) ? 75.0f : 125.0f);
          blend_disc(cx_a, cy_a, 6.2f, lr_c, lg_c, lb_c, 0.30f);
          blend_disc(cx_a, cy_a, 3.0f, 0.0f, 0.0f, 0.0f, 0.85f);
          blend_disc(cx_a, cy_a, 2.2f, lr_c, lg_c, lb_c, 0.96f);
        }
      } else if (mode == 4 && p.santa_wp_idx > 0) {
        const float kSantaCoords[60] = {
            -36.85f, 174.76f, -27.47f, 153.03f, -33.87f, 151.21f,  35.68f, 139.69f,  37.57f, 126.98f,
            -31.95f, 115.86f,  39.90f, 116.41f,  22.32f, 114.17f,   1.35f, 103.82f,  13.76f, 100.50f,
             28.61f,  77.21f,  19.08f,  72.88f,  25.20f,  55.27f,  55.76f,  37.62f,  -1.29f,  36.82f,
            -33.92f,  18.42f,  30.04f,  31.24f,  37.98f,  23.73f,  52.23f,  21.01f,  52.52f,  13.40f,
             41.90f,  12.50f,  48.86f,   2.35f,  51.51f,  -0.13f, home_lat, home_lon,  64.15f, -21.94f,
            -22.91f, -43.17f,  40.71f, -74.01f,  41.88f, -87.63f,  34.05f,-118.24f,  21.31f,-157.86f,
        };
        int max_v = std::min(p.santa_wp_idx, 30);
        for (int sv = 0; sv < max_v; sv++) {
          float vx, vy_s;
          latlon_to_map_xy(kSantaCoords[sv * 2], kSantaCoords[sv * 2 + 1], vx, vy_s);
          blend_disc(vx, vy_s, 5.2f, 255.0f, 200.0f, 45.0f, 0.28f);
          blend_disc(vx, vy_s, 2.3f, 255.0f, 225.0f, 80.0f, 0.95f);
        }
      }
    } else if (!sleeping && mode == 6) {
      static const float kEggs[] = {42.0f, -100.0f, -16.0f, -56.0f, 8.0f, 22.0f, 34.0f, 104.0f, -25.0f, 134.0f};
      for (int e = 0; e < 5; e++) {
        float ex, ey;
        latlon_to_map_xy(kEggs[e * 2], kEggs[e * 2 + 1], ex, ey);
        float er = (e & 1) ? 255.0f : 168.0f;
        float eg = (e % 3 == 0) ? 215.0f : 115.0f;
        float eb = (e & 1) ? 70.0f : 255.0f;
        blend_disc(ex, ey, 11.5f, er, eg, eb, 0.24f);
        blend_disc(ex, ey, 7.2f, 0.0f, 0.0f, 0.0f, 0.90f);
        blend_disc(ex, ey - 2.0f, 4.6f, er, eg, eb, 1.0f);
        blend_disc(ex, ey + 1.8f, 5.8f, er, eg, eb, 1.0f);
        for (int sx = -5; sx <= 5; sx++) blend_mem_px((int)ex + sx, (int)ey, 255.0f, 250.0f, 220.0f, 0.95f);
      }
    } else if (!sleeping && mode == 8) {
      blend_disc(94.0f, 168.0f, 22.0f, 255.0f, 170.0f, 40.0f, 0.22f);
      blend_disc(94.0f, 168.0f, 10.5f, 255.0f, 210.0f, 75.0f, 0.98f);
      blend_disc(120.0f, 190.0f, 15.0f, 255.0f, 80.0f, 20.0f, 0.24f);
      blend_disc(120.0f, 190.0f, 7.5f, 255.0f, 105.0f, 30.0f, 0.98f);
    }
  }

  // 5. Home Pin
  float pin_xf, pin_yf;
  latlon_to_map_xy(home_lat, home_lon, pin_xf, pin_yf);
  pin_yf += 1.5f;
  blend_disc(pin_xf, pin_yf, 6.2f, 0.0f, 0.0f, 0.0f, 0.95f);
  if (pal == 1) {
    blend_disc(pin_xf, pin_yf, 4.4f, 245.0f, 24.0f, 30.0f, 1.0f);
  } else if (pal == 2) {
    blend_disc(pin_xf, pin_yf, 4.6f, 255.0f, 138.0f, 18.0f, 1.0f);
  } else {
    blend_disc(pin_xf, pin_yf, 5.2f, 255.0f, (mode == 2) ? 215.0f : 246.0f, (mode == 2) ? 40.0f : 234.0f, 1.0f);
    blend_disc(pin_xf, pin_yf, 3.6f, 255.0f, 122.0f, 0.0f, 1.0f);
  }

  // 6. Direct Draw-Buffer Flush
#if LVGL_VERSION_MAJOR >= 9
  lv_draw_buf_t *db_cv = lv_canvas_get_draw_buf(cv);
  if (db_cv != nullptr && db_cv->data != nullptr && db_cv->header.cf == LV_COLOR_FORMAT_RGB565) {
    uint8_t *dst_base = db_cv->data;
    uint32_t stride = db_cv->header.stride;
    for (int y = 0; y < (int)kMapHeight; y++) {
      uint16_t *row16 = (uint16_t *)(dst_base + (size_t)y * stride);
      size_t src_off = (size_t)y * kMapWidth * 3UL;
      for (int x = 0; x < (int)kMapWidth; x++) {
        size_t idx = src_off + (size_t)x * 3UL;
        row16[x] = ((uint16_t)(bg[idx] & 0xF8U) << 8) | ((uint16_t)(bg[idx + 1] & 0xFCU) << 3) |
                   ((uint16_t)bg[idx + 2] >> 3);
      }
    }
  } else {
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
    for (int y = 0; y < (int)kMapHeight; y++) {
      if ((y & 63) == 0) esphome::App.feed_wdt();
      size_t src_off = (size_t)y * kMapWidth * 3UL;
      for (int x = 0; x < (int)kMapWidth; x++) {
        size_t idx = src_off + (size_t)x * 3UL;
        if ((bg[idx] | bg[idx + 1] | bg[idx + 2]) > 1U) {
          lv_canvas_set_px(cv, x, y, lv_color_make(bg[idx], bg[idx + 1], bg[idx + 2]), LV_OPA_COVER);
        }
      }
    }
  }
#else
  lv_img_dsc_t *dsc_cv = lv_canvas_get_img(cv);
  if (dsc_cv != nullptr && dsc_cv->data != nullptr && dsc_cv->header.cf == LV_IMG_CF_TRUE_COLOR) {
    lv_color_t *dst = (lv_color_t *)const_cast<uint8_t *>(dsc_cv->data);
    for (int y = 0; y < (int)kMapHeight; y++) {
      lv_color_t *row = dst + (size_t)y * kMapWidth;
      size_t src_off = (size_t)y * kMapWidth * 3UL;
      for (int x = 0; x < (int)kMapWidth; x++) {
        size_t idx = src_off + (size_t)x * 3UL;
        row[x] = lv_color_make(bg[idx], bg[idx + 1], bg[idx + 2]);
      }
    }
  } else {
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
    for (int y = 0; y < (int)kMapHeight; y++) {
      if ((y & 63) == 0) esphome::App.feed_wdt();
      size_t src_off = (size_t)y * kMapWidth * 3UL;
      for (int x = 0; x < (int)kMapWidth; x++) {
        size_t idx = src_off + (size_t)x * 3UL;
        if ((bg[idx] | bg[idx + 1] | bg[idx + 2]) > 1U) {
          lv_canvas_set_px_color(cv, x, y, lv_color_make(bg[idx], bg[idx + 1], bg[idx + 2]));
        }
      }
    }
  }
#endif

  sprites_dirty = true;
  if (animations_active) {
    map_just_flushed = true;
  } else {
    lv_obj_invalidate(cv);
  }
}

}  // namespace world_clock
