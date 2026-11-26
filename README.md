# ESP32-S3 World & Space Clock (Kids Sleep Trainer + Adult Office Desk)

A high-craft, dual-core **ESPHome + LVGL 9** 800×480 digital clock for the **Guition JC8048W550N** (ESP32-S3, 16MB Flash, 8MB Octal PSRAM, 16-bit parallel RGB LCD).

Available in **three plug-and-play flavours** from one modular codebase:
1. **Kids Standalone** (`examples/leo-clock-standalone.yaml`) — Zero Home Assistant needed! Core 0 background worker polls Open-Meteo & live ISS feeds, Google Public NTP, Captive Portal Wi-Fi setup, and a full iPhone-friendly local Web UI (`http://leo-clock.local/`).
2. **Kids Home Assistant** (`examples/leo-clock-ha.yaml`) — Native encrypted ESPHome API with HA weather, ISS, time, and 4-phase circadian dimming.
3. **Adult / Office Desk** (`examples/office-clock-energy-ha.yaml`) — Same live world map, solar terminator, night-side city lights & ISS tracker, with a **pluggable Band 3 dock** swapped out for live Home Assistant Solar / Load / Battery / Grid & Tariff telemetry (or any custom HA dock you build!).

---

## Key Features

- **4-Phase Circadian Sleep Trainer** (each phase independently toggleable in Standalone Web UI or YAML):
  1. **Daytime**: Bright warm-white/orange UI, live weather + clothing tips + 3-day forecast, 10fps map animations.
  2. **Evening Wind-Down (Amber)**: Cozy sunset + crescent/stars card ("Get ready for bed!"). On Christmas Eve, optionally keeps Santa flying across Europe during wind-down!
  3. **Deep Sleep (Red)**: Night-vision dim red palette (8%) with a **real lunar phase** moon matching tonight's actual moon.
  4. **Morning Quiet Play (Amber)**: Soft rising-sun card ("OK to get up & play quietly").
- **Live World Map & Space Tracker**:
  - 72×33 anti-aliased continent dot matrix with real-time solar day/night terminator.
  - **Night-side city lights** glowing softly across 26 major world hubs on the dark half of Earth.
  - **Live ISS tracker** with smooth 10fps dead-reckoning, trailing orbital breadcrumbs, **forward orbit projection**, and 900 km overhead flyover highlight.
  - Automatic seasonal takeovers: Birthday + countdown, Christmas Advent progress bar, 30-city NORAD-style Christmas Eve Santa tracker, Christmas Day snow, Halloween, exact-Gregorian Easter Egg Hunt, Bonfire/NYE Fireworks, and Star Wars Day.
- **Zero-Blinding Midnight Recovery**: Boots gently at 15% brightness (`ALWAYS_OFF` restore mode) so a 2 AM power blip never blasts daytime brightness into a sleeping bedroom.
- **Dynamic `{token}` Wording**: Custom messages support `{name}`, `{winddown}`, `{bedtime}`, `{quiet}`, and `{wakeup}` so changing a schedule time automatically updates all screen text.

---

## Architecture

```text
├── components/world_clock/       # Reusable C++ headers (auto-loaded locally OR via GitHub!)
│   ├── clock_math.h              # 4-phase schedule, exact Easter, lunar phase, UTF-8, JSON helpers
│   ├── world_map.h               # Continent grid, terminator, night lights, aurora, PSRAM flush
│   ├── weather_dock.h            # Anti-aliased weather icons (incl. fog), lunar moon, holiday cards
│   ├── sprite_engine.h           # 10fps dirty-rect sprite engine (ISS + forward arc, Santa, etc.)
│   ├── net_worker.h              # Core 0 FreeRTOS HTTPS worker (Open-Meteo + ISS failover)
│   └── sim_time.h / time.py      # Deterministic controllable clock for headless host emulation
├── packages/
│   ├── hardware_jc8048w550n.yaml # Board pins, PSRAM, 16-bit RGB LCD, 19.5kHz PWM backlight, OTA
│   ├── fonts.yaml                # Fira Code v6.2.0 + Material Design Icons
│   ├── ui_shell.yaml             # Band 1 (Header), Band 2 (Map), Top-Layer Boot/OTA Splash
│   ├── dock_weather.yaml         # Pluggable Band 3: Weather + 3-Day Forecast + Sleep/Holiday Cards
│   ├── dock_energy_ha.yaml       # Pluggable Band 3: 4-Tile Home Energy & Tariff Strip (HA)
│   ├── ha_weather_sensors.yaml   # HA weather entity bindings (pair with dock_weather.yaml)
│   ├── clock_core.yaml           # Shared globals, OTA splash, preview auto-revert, 10fps loop
│   ├── backend_homeassistant.yaml# HA API, time, ISS & 1Hz schedule/header engine
│   └── backend_standalone.yaml   # Core 0 NetWorker, Google NTP, Captive Portal & Web UI v3
├── examples/                     # Public, personal-info-free starter & universal release configs
├── sim/                          # Headless ESPHome host + SnapshotDisplay emulators (zero X11/SDL!)
├── scripts/run_emulator.py       # Compiles/runs emulators, builds PNG gallery & GIF/MP4 reels
├── .github/workflows/            # CI (tests + firmware builds + emulator media) & Release builds
└── tests/test_clock_math.cpp     # Fast g++ unit tests for core C++ math
```

---

## Pre-Built Standalone Firmware (Zero ESPHome Install Required!)

Every tagged GitHub Release (`v*`) automatically builds and attaches universal pre-built firmware binaries from [`examples/world-clock-standalone-release.yaml`](examples/world-clock-standalone-release.yaml):

- **`world-clock-standalone-vX.Y.Z-factory.bin`** — First-time USB flash at offset `0x0` ([ESP Web Tools](https://web.esphome.io/) or `esptool.py`).
- **`world-clock-standalone-vX.Y.Z-ota.bin`** — One-click wireless update via the clock's Web UI (**OTA Update**). Preserves Wi-Fi, child's name, birth date, coordinates, sleep schedule, brightness, and custom wording automatically!

### Multi-Device Ready (Hardware MAC Auto-Identity)
Multiple clocks in the same household can run the exact same `.bin` without colliding:
- **Unique mDNS Hostname**: `http://world-clock-<mac6>.local/`
- **Unique Setup Wi-Fi Hotspot**: `world-clock-<mac6>` (password: `ClockSetup123`)
- **Unique Web Settings Login**: username `parent`, password `clock-<mac6>` (derived from the board's last 6 hardware MAC hex characters)
- **Zero Setup Card Needed**: Unplug & plug in any clock to see its exact URL, IP address, and `parent / clock-<mac6>` login displayed on the 5-inch boot screen.

---

## Deploying to Your ESPHome Server (Zero Manual File Copying!)

Once you push this repository to GitHub (e.g. `https://github.com/YOUR_USER/esp32-world-clock`), your ESPHome Device Builder (`http://<your-esphome-ip>:6052/`) can pull `packages/` and `components/world_clock/` **directly from GitHub** — you only paste a ~55-line device YAML into ESPHome!

In any device YAML on your ESPHome server, replace the local `external_components:` and `packages:` blocks with:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USER/esp32-world-clock
      ref: master
      path: components
    components: [world_clock]
    refresh: 1d

packages:
  world_clock:
    url: https://github.com/YOUR_USER/esp32-world-clock
    ref: master
    refresh: 1d
    files:
      - packages/hardware_jc8048w550n.yaml
      - packages/fonts.yaml
      - packages/ui_shell.yaml
      - packages/dock_weather.yaml        # Swap for packages/dock_energy_ha.yaml for Office Desk!
      - packages/ha_weather_sensors.yaml  # Only needed for HA + dock_weather.yaml
      - packages/clock_core.yaml
      - packages/backend_homeassistant.yaml  # Or packages/backend_standalone.yaml
```

*(Make sure your ESPHome server's `secrets.yaml` defines `wifi_ssid`, `wifi_password`, `ap_password`, and `api_key` or `web_auth_user`/`web_auth_pass`.)*

---

## Creating Your Own Custom Band 3 Dock

Any Band 3 dock (y = 400..480) only needs to:
1. Add its LVGL widgets under `lvgl: pages: - id: !extend main_page: widgets:`
2. Implement 3 scripts:
   - `render_dock` (full redraw on palette/mode change & every 5 min)
   - `apply_dock_palette` (update widget colors for `ui_palette` `0`=Day, `1`=Red, `2`=Amber)
   - `update_dock_1hz` (fast 1-second sensor/label updates)

See [packages/dock_energy_ha.yaml](packages/dock_energy_ha.yaml) for a complete, ready-to-copy example.

---

## Headless Emulator, Screenshots, Videos & CI

This repo includes a native **ESPHome `host:` + `SnapshotDisplay` headless emulator** (`sim/`) that runs the real LVGL 9.5 UI, fonts, world map, weather dock, energy dock, and 10fps sprite engine directly on Linux with zero display server or SDL2 required:

```bash
# Run C++ unit tests (4-phase schedule, Easter boundaries, lunar phase, UTF-8, JSON)
g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/world_clock tests/test_clock_math.cpp -o /tmp/test_clock_math && /tmp/test_clock_math

# Run the headless emulator locally to capture all 17 mode PNGs, contact sheet & animated GIFs/MP4s
python3 scripts/run_emulator.py --out sim_output
```

### GitHub Actions Workflows
- **CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml))** — Runs on every push to `master`/`main` and every PR:
  1. C++ unit tests (`-Wall -Wextra -Werror`)
  2. Real ESP32-S3 firmware compilation across all 4 example/release configs (`leo-clock-standalone`, `world-clock-standalone-release`, `leo-clock-ha`, `office-clock-energy-ha`)
  3. Headless emulator run uploading all 17 PNGs, `overview_gallery.png`, 4 animated GIFs, 4 H.264 MP4s, and `demo_reel.mp4` as workflow artifacts
- **Release ([`.github/workflows/release.yml`](.github/workflows/release.yml))** — Triggered on `git tag v1.0.0 && git push origin v1.0.0`: compiles the universal multi-device standalone firmware and attaches `*-factory.bin`, `*-ota.bin`, `SHA256SUMS.txt`, `overview_gallery.png`, and `demo_reel.mp4` straight to the GitHub Release.
