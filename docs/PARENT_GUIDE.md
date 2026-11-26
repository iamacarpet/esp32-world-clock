# Parent Guide — Setting Up & Customising Your World & Space Clock

This guide covers every setting on your clock's phone-friendly Web Settings page — **no apps, no accounts, and no technical tools required**.

---

## 1. Finding & Opening Your Clock's Settings

Every clock has its own unique address and password printed right on its boot screen:
1. Unplug the USB-C cable and plug it back in (or use your saved bookmark).
2. On first setup, scan the **Wi-Fi QR Code** on the screen with your phone camera (or join hotspot `world-clock-XXXXXX`, password `ClockSetup123`) to connect the clock to your home Wi-Fi.
3. Once connected, scan the **Settings QR Code** (or type `http://world-clock-XXXXXX.local/` into Safari/Chrome) and log in with:
   - **Username**: `parent`
   - **Password**: `clock-XXXXXX` *(matches the 6 characters on your screen)*

> **Running multiple clocks in one home?** Each clock automatically gets its own unique `world-clock-XXXXXX.local` address and `clock-XXXXXX` password from its hardware ID, so sibling clocks never clash!

---

## 2. Automatic Family & School Calendar Integration (Set Once, Never Touch Again!)

Instead of opening the clock's settings every week, you can link your existing **Apple iCloud**, **Google Calendar**, and/or **School Term Calendar**. The clock checks them silently every 30 minutes in the background and shows calm, predictable daily heads-ups in the header ticker — ideal for busy mornings and neurodivergent / autistic children who love knowing what's coming next!

### What Your Clock Shows Automatically
- **Morning & Daytime**:
  - `"SCHOOL DAY · SWIMMING LESSON (4 PM)"`
  - `"NO SCHOOL · HALF TERM HOLIDAY"`
  - `"WEEKEND · NO SCHOOL"`
  - `"TODAY → PE KIT & TRAINERS"`
- **Afternoon & Evening (from 3 PM & during Wind-Down)**:
  - `"TOMORROW → DENTIST"` *(gentle heads-up so there are no morning surprises!)*
- **Automatic Event Countdowns**:
  - Any event in the next **14 days** automatically shows `"6 SLEEPS TO GRANDMA VISIT!"`.
  - **Special `Countdown:` Tag (up to 90 days ahead!)**: Start any event title with **`Countdown:`** (or add **`[countdown]`**) — e.g. `Countdown: Cornwall Holiday` or `Disney Trip [countdown]`. The clock strips the tag cleanly and prioritizes your big countdown (`"42 SLEEPS TO CORNWALL HOLIDAY!"`) over routine reminders!
- **Automatic School Holiday & Inset Day Detection**:
  - When you add your school's public `.ics` calendar — **or** add **`[no school]`** / **`[holiday]`** to any event in your Family Calendar (e.g., `Inset Day [no school]`) — the clock automatically recognizes keywords like *Holiday, Half Term, Inset, Closed, Training Day, Bank Holiday, No School* and displays **"NO SCHOOL"**!
  - If **Enable Weekend Lie-In** and **Include School Holidays in Lie-In** are turned on, the clock even shifts Morning Quiet Play & Wake-Up later on Inset Days and Half Term automatically!

### Step-by-Step: Getting Your Calendar Link

#### Apple iPhone / iCloud Calendar
1. Open the **Calendar** app on your iPhone/iPad.
2. Tap **Calendars** at the bottom. *(Tip: create a dedicated "Charlie" or "Family Kids" calendar so work meetings don't appear on the clock!)*
3. Tap the **`(i)`** button next to that calendar.
4. Toggle **Public Calendar** ON, tap **Share Link...**, and tap **Copy**.
5. Open your clock's Web Settings → **5. Family & School Calendar (.ics)** → paste the link directly into **Family Calendar URL** *(you can paste `webcal://...` directly — the clock handles it automatically!)*.

#### Google Calendar
1. Open [calendar.google.com](https://calendar.google.com) on a browser (or desktop mode on phone).
2. Click the **3 dots** next to your family calendar → **Settings and sharing**.
3. Scroll to **Integrate calendar** and copy **Secret address in iCal format** (`https://calendar.google.com/.../basic.ics`).
4. Paste it into **Family Calendar URL** on your clock.

#### School Term Dates Calendar
1. Check your school's website calendar page for a **"Subscribe"**, **"iCal"**, or **"Add to Calendar"** link (`webcal://...` or `https://...ics`).
2. Copy that link and paste it into **School Term & Holiday Calendar URL** on your clock!
3. Tap **Sync Weather, ISS & Calendars Now** in Quick Controls to test immediately.

---

## 3. All Web Settings Explained

### 1. Quick Controls & Overrides
- **Occasion & Preview Mode**: Test any holiday or sleep mode right away (automatically reverts to `Auto` after 10 minutes so you can't forget!).
- **Screen Backlight**: Manual override slider if you want to brighten/dim temporarily.
- **Message of the Day (Auto-Clears at Midnight)**: Type a one-off reminder like `"PE kit today!"` or `"Grandma picking up!"` — shows right at the top until midnight or cleared.
- **Tooth Fairy Tonight!**: One-tap toggle for when a tooth falls out! Turns Evening Wind-Down into a magical twinkling tooth card (`"SHHH... TOOTH FAIRY TONIGHT!"`) with floating golden fairy dust on the map, and turns itself back off at noon the next day.
- **Nap Timer Duration + Start Nap Mode Now**: Instantly puts the clock into soft Red Nap Mode with fading countdown stars for 15–180 minutes, then returns to normal automatically.
- **Trigger Wind-Down Now / Cancel Override**: Start Amber Wind-Down early on tired evenings without editing your regular bedtime.
- **Shop & Fayre Demo Mode**: Cycles through every operational mode every 10 seconds for live demonstrations (always resets to OFF on reboot).

### 2. Child Profile, Location & Units
- **Child Name & Birth Date**: Personalizes morning greetings, birthday age & countdowns.
- **Location Pill Label + Latitude & Longitude**: Sets the home pin on the world map and fetches hyper-local weather & 3-day forecasts. *(Tip: find coordinates on Google Maps by long-pressing your town.)*
- **Timezone**: Pick from 12 worldwide timezones (automatic daylight saving!).
- **24-Hour Clock / Fahrenheit (`°F`) / Miles**: Instant switch for UK, European, or US families.

### 3. Sleep Schedule & Brightness
Every phase can be turned **ON or OFF** independently:
1. **Evening Wind-Down (Amber)** — *"Get ready for bed!"* (sunset + crescent icon)
2. **Deep Sleep (Red)** — Night-vision dim red with tonight's **real lunar phase** moon
3. **Morning Quiet Play (Amber)** — Rising-sun icon (*"OK to play quietly in your room"*)
4. **Morning Wake-Up (Daytime)** — Full bright World Map, Weather & ISS Tracker
- **5 Sleep Countdown Stars**: Toggleable row of 5 glowing stars during Wind-Down, Sleep, and Naps that gently fade out one by one so younger children can see how long is left without reading digital numbers.
- **Weekend & School-Holiday Lie-In**: Optional +5 to +90 minute shift for Saturday/Sunday mornings (and optionally School Holidays / Inset Days!).
- **4 Brightness Sliders**: Independent brightness for Day, Wind-Down, Quiet Play, and Deep Sleep.

### 4. Custom Screen Wording (`{tokens}`)
Personalize any ticker or card message! Use these dynamic placeholders so changing a schedule time automatically updates every message:
- `{name}` → Child's name (e.g. `LEO`)
- `{winddown}` → Wind-down time (e.g. `7 PM`)
- `{bedtime}` → Bedtime (e.g. `7:30 PM`)
- `{quiet}` → Effective quiet-play time (automatically reflects Weekend Lie-In!)
- `{wakeup}` → Effective wake-up time

### 5. Family & School Calendar (.ics) & Custom Countdown
- **Family Calendar URL** & **School Calendar URL** (see Section 2 above)
- **Custom Countdown Name & Date**: Quick built-in countdown without needing a calendar (e.g., `CORNWALL` / `2026-08-01` → `"14 SLEEPS TO CORNWALL!"`).

### 6. Occasions & Visual Features
- Toggle **Night-Side City Lights**, **Live ISS Space Station Tracker** (with overhead *"WAVE AT ASTRONAUTS!"* alerts & occasional shooting stars / Perseids & Geminids meteor showers), **Seasonal Holidays** (Birthday, Christmas Advent, Santa Tracker, Christmas Day, Halloween, Easter Egg Hunt, Bonfire/NYE Fireworks, Star Wars Day, Pancake Day, Mother's Day, Father's Day, World Book Day, Solstices), **Keep Santa Flying During Xmas Eve Wind-Down**, and **Special Nights Bedtime Delay** (0–120 min shift on Christmas Eve, NYE & Bonfire Night).

---

## 4. Wireless Firmware Updates (No Computer Needed!)

1. Download the latest **`world-clock-standalone-vX.Y.Z-ota.bin`** file from the GitHub Releases page onto your phone or laptop.
2. Open your clock's Web Settings (`http://world-clock-XXXXXX.local/`).
3. Scroll to **OTA Update**, tap **Choose File**, select the `-ota.bin` file, and tap **Update**.
4. Keep power connected for ~30 seconds — your Wi-Fi, child's profile, calendars, and sleep schedule are all preserved automatically!

---

## 5. Home Assistant Calendar Setup (For HA Users)

If you run a Home Assistant clock (`packages/backend_homeassistant.yaml`), include `packages/ha_calendar_sensors.yaml` and add this snippet to Home Assistant's `configuration.yaml` (pointing at your HA `calendar.family` and `calendar.school` entities):

```yaml
template:
  - trigger:
      - platform: time_pattern
        minutes: "/15"
      - platform: homeassistant
        event: start
    action:
      - action: calendar.get_events
        target:
          entity_id:
            - calendar.family
            - calendar.school
        data:
          duration:
            hours: 48
        response_variable: cal
    sensor:
      - name: "Clock Calendar Today"
        state: >-
          {% set today = now().strftime('%Y-%m-%d') %}
          {% set evs = cal['calendar.family'].events | selectattr('start', 'search', '^' ~ today) | list %}
          {{ evs[0].summary if evs else 'none' }}
      - name: "Clock Calendar Tomorrow"
        state: >-
          {% set tmrw = (now() + timedelta(days=1)).strftime('%Y-%m-%d') %}
          {% set evs = cal['calendar.family'].events | selectattr('start', 'search', '^' ~ tmrw) | list %}
          {{ evs[0].summary if evs else 'none' }}
      - name: "Clock School Note"
        state: >-
          {% set today = now().strftime('%Y-%m-%d') %}
          {% set evs = cal['calendar.school'].events | selectattr('start', 'search', '^' ~ today) | list %}
          {{ evs[0].summary if evs else 'none' }}
    binary_sensor:
      - name: "Clock School Holiday"
        state: >-
          {% set today = now().strftime('%Y-%m-%d') %}
          {% set evs = cal['calendar.school'].events | selectattr('start', 'search', '^' ~ today) | map(attribute='summary') | join(' ') | lower %}
          {{ 'holiday' in evs or 'half term' in evs or 'inset' in evs or 'closed' in evs or 'no school' in evs }}
```
