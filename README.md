# ESP32 Noise Monitor (releasev1)

ESP32-based classroom noise monitoring firmware with:

- Traffic-light noise LEDs (Green/Yellow/Red) with brightness control
- Status RGB LED for network/system state
- INMP441 I2S microphone dB estimation with smoothing + moving average
- Configurable multi-stage RED escalation (FIRST → SECOND → MAJOR)
- Continuous-noise MAJOR repeat alerts while noise stays above RED
- Silence window reset logic (requires quiet time below RED before resetting escalation)
- Optional MP3 warning playback (UART MP3 module)
- Optional short WAV recordings on MAJOR events (SD card)
- Offline-safe event queue on SD and sync to Supabase when online
- Bulk upload for pending (non-audio) events to reduce HTTP overhead
- Built-in admin web UI served from ESP32 (`/`) to configure device

This folder is an **Arduino sketch**:

- `releasev1.ino` — main firmware
- `web_ui.h` — single-page admin UI served by the ESP32
- `types.h` — shared types (`LedState`)

---

## Hardware

### Required

- **ESP32** (Arduino-ESP32 core)
- **INMP441** digital microphone (I2S)
- **Traffic-light LEDs** (3x discrete LED)
- **SD card module** (SPI)

### Optional

- **UART MP3 player module** (DFPlayer-style) + speaker
- **Status RGB LED** (common cathode)

### Pin mapping (from `releasev1.ino`)

Noise LEDs:

- `LED_GREEN` = GPIO **14**
- `LED_YELLOW` = GPIO **12**
- `LED_RED` = GPIO **27**

Status RGB (common cathode):

- `STATUS_LED_R` = GPIO **21**
- `STATUS_LED_G` = GPIO **22**
- `STATUS_LED_B` = GPIO **13**

SD card:

- `SD_CS` = GPIO **5**
- SPI bus initialized as `SPI.begin(18, 19, 23, SD_CS)`

INMP441 I2S:

- `I2S_WS` = GPIO **25**
- `I2S_SD` = GPIO **33**
- `I2S_SCK` = GPIO **26**

MP3 UART (HardwareSerial2):

- RX = GPIO **16**
- TX = GPIO **17**

---

## Firmware overview

### Main loop responsibilities

`loop()` continuously:

- Handles HTTP requests (`server.handleClient()`)
- Maintains MP3 availability probing
- Maintains Wi-Fi connection and retry behavior
- Checks internet reachability periodically
- Syncs queued events to Supabase periodically (and also on-demand after queueing)
- Reads microphone samples via I2S and computes smoothed dB
- Updates noise LEDs (`updateLEDState()`)
- Runs RED escalation state machine (`handleRedWarnings()`)
- Logs a dB time-series to SD (change-based + heartbeat) and bulk uploads it

---

## Noise measurement pipeline

### `readMicDB()`

- Reads `samples[]` via `i2s_read()`
- Computes RMS of the samples
- Subtracts `NOISE_FLOOR`
- Converts to a rough dB-like metric using:

```text
20 * log10(rms + 1) * SENSITIVITY
```

Key constants:

- `NOISE_FLOOR = 25000`
- `SENSITIVITY = 0.5`

### Smoothing + average

- Exponential smoothing:

```cpp
smoothDB = smoothDB + SMOOTH_ALPHA * (rawDB - smoothDB);
```

- Moving average window (`AVG_WINDOW = 10`) via `getMovingAverage()`

Note: LEDs and warnings use `smoothDB` (cast to `int`).

---

## LED behavior

### Noise LEDs (`updateLEDState()`)

- Switches to **RED immediately** when `value >= RED_THRESHOLD`
- Switches to **YELLOW immediately** when `value >= YELLOW_THRESHOLD`
- Uses hysteresis only on the **falling edge** to reduce flicker:

- RED → YELLOW only if `value < RED_THRESHOLD - HYSTERESIS_DB`
- YELLOW → GREEN only if `value < YELLOW_THRESHOLD - HYSTERESIS_DB`

`HYSTERESIS_DB = 3`.

### Status RGB LED (`updateStatusLed()`)

Shows a color (and sometimes blinking) based on:

- Booting vs setup-complete
- AP/STA mode and connection
- Internet OK vs no internet
- Any error state:
  - SD not available
  - MIC stuck at 0
  - Supabase request failures
  - MP3 not detected

Admin can configure:

- “preset” colors (`/setStatusColors`) and exact RGB values (`/setStatusRgb`)
- manual override (`/statusLedManual`)

---

## RED escalation + continuous noise logic

All RED escalation happens in `handleRedWarnings(value, now)`.

### Violation start / grouping

When `value >= RED_THRESHOLD` and `redStartTime == 0`, a new violation starts:

- `redStartTime = now`
- flags reset (`firstLogged/secondLogged/majorLogged = false`)
- `currentViolationGroupId = genUuidV4()`

All events during that violation share the same `event_group_id`.

### Configurable warning timings

Three escalation steps are configurable (via web UI + Preferences):

- `firstWarningTimeMs`
- `secondWarningTimeMs`
- `majorWarningTimeMs`

Defaults (compile-time):

- FIRST: 5s (`FIRST_WARNING_TIME`)
- SECOND: 30s (`SECOND_WARNING_TIME`)
- MAJOR: 60s (`MAJOR_WARNING_TIME`)

Ordering is enforced:

- `first < second < major`

### Continuous MAJOR repeat

If MAJOR already triggered and noise stays above RED, the device repeats MAJOR:

- Every `majorRepeatIntervalMs`
- Logs a repeat
- Records audio again (WAV)
- Queues a new MAJOR event

### Silence reset window

If `value < RED_THRESHOLD`, escalation does **not** reset immediately.

It resets only if noise stays below RED continuously for:

- `silenceResetWindowMs`

After reset:

- next time it goes RED again, it starts from FIRST.

---

## SD card files + formats

### 1) Pending events queue

Path:

- `/pending_events.txt`

Purpose:

- Offline-safe queue of warning events to be uploaded to Supabase.

Current line format (newest):

```text
eventId|groupId|warningLevel|durationSeconds|decibel|buzzerTriggered|audioRecorded|audioLocalPath|eventTsMs
```

Examples:

- `warningLevel` is one of: `FIRST`, `SECOND`, `MAJOR`
- `durationSeconds` is:
  - configured seconds for FIRST/SECOND/MAJOR
  - elapsed seconds since violation start for MAJOR repeats
- `buzzerTriggered` is `1` if speaker was enabled at the time
- `audioRecorded` is `1` if a WAV was recorded
- `audioLocalPath` is a filename like `/rec_YYYYMMDD_HHMMSS.wav` (or empty)
- `eventTsMs` is epoch milliseconds (`getEpochMs()`), stored as an integer

Backward compatibility:

- Old formats without `groupId` and/or `eventTsMs` are still parsed and uploaded.

### 2) dB time-series log

Path:

- `/db_series.txt`

Format:

```text
ts_ms|db10
```

Where:

- `ts_ms` = epoch milliseconds
- `db10` = dB * 10 (integer)

Upload:

- batched to Supabase table `noise_db_series` as a JSON array.

### 3) Rolling noise log

Path:

- `/noise_log.txt`

Used for debug logging and event lines.

---

## Supabase integration

### Supabase settings

In `releasev1.ino`:

- `SUPABASE_URL`
- `SUPABASE_API_KEY`

These are compiled into the firmware.

### Tables / storage expected

1) `noise_events` (PostgREST endpoint: `/rest/v1/noise_events`)

Firmware inserts JSON with at least:

- `id` (uuid string)
- `device_id` (string)
- `event_group_id` (uuid string)
- `warning_level` (string)
- `warning_color` (`RED`)
- `duration_seconds` (int)
- `decibel` (int)
- `buzzer_triggered` (bool)
- `audio_recorded` (bool)
- optional: `audio_url` (string)
- optional: `event_ts_ms` (bigint) — **actual occurrence time** (important for offline sync)

2) Storage bucket: `recordings`

- WAVs are uploaded to:

```text
/storage/v1/object/recordings/<DEVICE_ID>/<eventId>.wav
```

- Public URL format:

```text
/storage/v1/object/public/recordings/<DEVICE_ID>/<eventId>.wav
```

3) `noise_event_audio`

Inserted when audio exists:

- `noise_event_id`
- `audio_url`
- `audio_seconds` (currently fixed to 5)

4) `noise_db_series`

Bulk inserts `{ device_id, ts_ms, db10 }`.

### Bulk upload behavior (pending events)

`trySyncPendingEvents()`:

- Batches up to 10 **non-audio** pending events into a single POST (JSON array)
- Uploads **audio events** individually (because it must upload the WAV first)
- Processes up to 12 events per run and uses a time budget to avoid blocking too long

---

## Web UI

The ESP32 serves a single-page admin interface from PROGMEM:

- `GET /` → HTML/JS from `web_ui.h`

The UI:

- Performs Supabase login (email/password) client-side
- Checks the user’s role via `profiles` table (`role === 'admin'`)
- If admin, enables the controls panel

### Key endpoints used by the UI

- `GET /status` → JSON status/config snapshot
- `GET /scan` → Wi-Fi scan results
- `GET /save?ssid=...&password=...` → save Wi-Fi credentials
- `GET /disconnect` → clear Wi-Fi and re-enable setup AP

Admin controls:

- `GET /setThresholds?yellow=..&red=..`
- `GET /setAlertConfig?maj_min=..&sil_sec=..&first_sec=..&second_sec=..&major_sec=..`
- `GET /setSpeaker?enabled=0|1`
- `GET /setMp3Volume?vol=0..30`
- `GET /setLedBrightness?ng=..&ny=..&nr=..&st=..`
- `GET /setNoiseLedsEnabled?enabled=0|1`
- `GET /setMicEnabled?enabled=0|1`
- `GET /setSerialLogging?enabled=0|1`
- `GET /setStatusColors?boot=..&ap=..&wifi=..&noi=..&off=..`
- `GET /setStatusRgb?boot=#RRGGBB&ap=#RRGGBB&wifi=#RRGGBB&noi=#RRGGBB&off=#RRGGBB`
- `GET /setDbLogConfig?samp=..&thr10=..&hb=..&up=..`
- `GET /statusLedManual?on=0|1&r=..&g=..&b=..`
- `GET /events` → device event logs
- `GET /monitor` → dB/LED monitor logs

---

## Configuration storage

Settings are persisted in ESP32 NVS using `Preferences`:

Namespace `settings`:

- Thresholds: `yellow`, `red`
- Alert timers: `fw_ms`, `sw_ms`, `mw_ms`, `maj_int`, `sil_win`
- LED brightness: `ngbrt`, `nybrt`, `nrbrt`, `stbrt`
- Toggles: `nleden`, `micen`, `serlog`, `speaker`
- MP3 volume: `mp3vol`
- Status colors: `sr_boot`, `sr_ap`, `sr_wifi`, `sr_noi`, `sr_off`
- DB series logging: `db_samp`, `db_thr10`, `db_hb`, `db_up`

Namespace `wifi`:

- `ssid`, `password`

---

## Build & flash

### Requirements

- Arduino IDE or PlatformIO
- ESP32 board package (Arduino-ESP32)
- Libraries used are standard in Arduino-ESP32:
  - `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `Preferences`, `SPI`, `SD`

### Steps (Arduino IDE)

1. Open `releasev1.ino`
2. Select your ESP32 board + correct COM port
3. Compile and Upload
4. Open Serial Monitor at **115200**

---

## First-time setup / provisioning

1. Power on the ESP32
2. Connect to the setup AP:

- SSID: `ESP32_NOISE_Setup`
- Password: `12345678`

3. Open in browser:

- `http://192.168.4.1/`

4. Scan Wi-Fi, connect, and save credentials
5. Once STA connects, the device shows its router IP in the UI

The AP automatically turns off after a short grace period.

---

## Troubleshooting

### Device feels “paused” during sync

- Supabase HTTP calls are synchronous.
- This firmware reduces overhead by:
  - bulk inserting non-audio events
  - processing a limited batch per run with a time budget

If you need absolutely non-blocking behavior, the next step would be moving sync into a separate FreeRTOS task.

### Pending events never clear

Common causes:

- SD card not mounted / failing
- Supabase insert rejected (schema mismatch)

Check:

- `/events` and Serial logs for HTTP status codes
- Make sure Supabase tables contain required columns:
  - `event_group_id`
  - `event_ts_ms` (bigint)

### Time not set / timestamps are 0

`getEpochMs()` returns `0` until NTP time is valid.

- Ensure the device has internet access
- Wait a few seconds after boot

### LED color looks inconsistent with thresholds

LED transitions:

- RED turns on at `>= RED_THRESHOLD`
- Falling hysteresis uses `HYSTERESIS_DB = 3`

---

## Security notes

- Supabase URL/key are currently compiled into firmware.
- The web UI performs login from the browser and uses the Supabase anon key.
- Do not publish keys in public repos.

---

## License

Add your license here.
