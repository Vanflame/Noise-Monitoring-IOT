# IoT-Based Smart Classroom Noise Monitoring Using Traffic Light Indicator System (ESP32)

## Abstract
This project implements an **IoT-based classroom noise monitoring system** using an **ESP32** and an **INMP441 digital microphone** (I2S). The device continuously samples audio, computes an estimated sound level (dB), and provides immediate visual feedback using a **traffic-light LED indicator (Green/Yellow/Red)**. When excessive noise persists, the system escalates alerts using a **speaker/MP3 warning module** and (for major violations) captures a short **5‑second audio clip** as evidence. Events are stored locally on an **SD card** and synchronized to a **Supabase (PostgreSQL + Storage)** backend when internet connectivity is available.

The firmware includes a built-in **web-based admin interface** hosted directly on the ESP32 for Wi‑Fi provisioning, live monitoring, device settings, and diagnostics.

## Project Objectives
- Monitor classroom noise levels in near real-time.
- Provide clear visual feedback through a traffic-light indicator.
- Reduce false positives using smoothing, hysteresis, and time-based escalation.
- Log noise incidents with timestamps and severity levels.
- Support offline-first operation with SD buffering and later cloud synchronization.
- Enforce privacy by recording **only event-triggered short clips** (no continuous recording / no live listening).

## Scope and Limitations
- **Noise level** is computed from microphone RMS and is an **estimated dB-like value**. It is not a calibrated Class‑1/2 SPL meter unless you perform calibration.
- Audio recording is **triggered only for MAJOR events** and records **exactly 5 seconds**.
- The firmware syncs directly to Supabase using REST endpoints and uploads audio to a Supabase Storage bucket.
- Role-based admin access is handled via Supabase Auth + a `profiles` table (see backend requirements).

## Key Features
- **Traffic light indicator**
  - Green: normal
  - Yellow: moderate noise
  - Red: excessive noise
- **Escalation logic while Red persists**
  - First warning at configurable time
  - Second warning at configurable time
  - Major warning at configurable time (records 5s audio)
  - Major repeat warning at configurable interval
- **Offline-first**
  - Events are queued to SD (`/pending_events.txt`) and uploaded later.
  - Time-series dB values are stored to SD (`/db_series.txt`) and bulk uploaded.
- **Web-based admin UI hosted on ESP32**
  - Wi‑Fi scanning and configuration
  - Thresholds and timers configuration
  - LED brightness controls
  - Speaker/MP3 test controls + volume
  - Live monitor and logs
  - SD and RTC diagnostics

## Repository Contents
- `releasev1.ino`
  - Main ESP32 firmware
- `web_ui.h`
  - Embedded single-page admin UI served at `/`
- `types.h`
  - Shared enums/types
- `plan.txt`
  - System design notes and capstone planning

## System Architecture
### Layer 1 — Edge / Classroom Device (ESP32)
Responsibilities:
- Sample audio via INMP441 (I2S)
- Compute smoothed dB estimate
- Apply thresholds + hysteresis
- Drive traffic-light LEDs
- Escalate alerts via MP3 module
- Record short 5s WAV for major events
- Store logs and pending events on SD
- Sync events and audio to Supabase

### Layer 2 — Backend / Cloud (Supabase)
Responsibilities:
- Store event metadata and time-series records
- Store event-triggered audio clips (Supabase Storage)
- Provide authentication (Supabase Auth)
- Provide role-based access via `profiles` table

### Layer 3 — Web Application / Dashboard
This repository contains the **device-side admin UI** hosted on the ESP32.
A separate teacher/admin dashboard can be built on top of the Supabase database.

## Hardware Requirements (Bill of Materials)
- ESP32 development board
- INMP441 digital microphone module (I2S)
- 3 LEDs (Green/Yellow/Red) + resistors
- Status RGB LED (common cathode) + resistors
- MicroSD card module + microSD card
- MP3 module (DFPlayer Mini or compatible UART MP3 module) + speaker
- (Optional) DS3231 RTC module
- Wires, breadboard/PCB, power supply

## Wiring / Pin Map (as configured in firmware)
### Traffic-light LEDs
- Green LED: GPIO `14`
- Yellow LED: GPIO `12`
- Red LED: GPIO `27`

### Status RGB LED (Common Cathode)
- R: GPIO `21`
- G: GPIO `22`
- B: GPIO `13`

### SD Card (SPI)
- CS: GPIO `5`
- SPI bus initialized as:
  - SCK `18`
  - MISO `19`
  - MOSI `23`

### INMP441 (I2S)
- WS/LRCL: GPIO `25`
- SD/DOUT: GPIO `33`
- SCK/BCLK: GPIO `26`

### MP3 Module (UART2)
- ESP32 RX2: GPIO `16`  (connect to MP3 TX)
- ESP32 TX2: GPIO `17`  (connect to MP3 RX)

### DS3231 RTC (Optional, I2C)
- SDA: GPIO `32`
- SCL: GPIO `4`
- I2C address: `0x68`

## Firmware Behavior
### Noise thresholds
Default thresholds in code (can be changed in UI and saved to NVS):
- `YELLOW_THRESHOLD`: `65`
- `RED_THRESHOLD`: `70`

### Smoothing and stability controls
- Exponential smoothing factor: `SMOOTH_ALPHA = 0.1`
- Hysteresis: `HYSTERESIS_DB = 3`
- Moving average window: `AVG_WINDOW = 10`

### Escalation timers (defaults)
- First warning time: `5s`
- Second warning time: `30s`
- Major warning time: `60s`
- Major repeat interval: `180000ms` (3 minutes)
- Silence reset window: `15000ms` (15 seconds below red threshold before resetting the “red session”)

### What gets recorded and uploaded
- **Events**: saved to SD and synced to Supabase.
- **Audio**: only for **MAJOR** events (and repeat majors), records **5 seconds** to SD, then uploads to Supabase Storage.
- **dB time series**: stored as `db10` (dB * 10) and uploaded in batches.

## Device Web UI (ESP32 hosted)
### Access
On boot, the ESP32 starts in `WIFI_AP_STA` mode and creates a setup access point:
- SSID: `ESP32_NOISE_Setup`
- Password: `12345678`

Connect to the AP and open:
- `http://192.168.4.1/`

If the ESP32 later connects to your router Wi‑Fi, the UI will show the assigned STA IP. You can then open:
- `http://<device_sta_ip>/`

### Main endpoints (HTTP)
- `GET /`
  - Admin UI
- `GET /scan`
  - Wi‑Fi scan results (JSON)
- `POST/GET /save?ssid=...&password=...`
  - Save Wi‑Fi credentials (stored in NVS) and attempt connection
- `GET /status`
  - Device status (JSON)
- `GET /events`
  - Device event log (text)
- `GET /monitor`
  - Live monitor stream (text)

Admin controls (requires admin login via Supabase in the UI):
- `POST /setThresholds`
- `POST /setAlertConfig`
- `POST /setLedBrightness`
- `POST /setNoiseLedsEnabled`
- `POST /setMicEnabled`
- `POST /setSerialLogging`
- `POST /setStatusRgb`
- `POST /setDbLogConfig`
- `POST /setSpeaker`
- `POST /setMp3Volume`
- `GET /playTest001`, `/playTest002`, `/playTest003`
- `POST /stopMp3`

Diagnostics:
- `GET /sdinfo`
- `GET /sdreinit`
- `GET /rtcinfo`
- `GET /rtcsync`

## Data Storage (SD Card)
Files used by firmware:
- `/noise_log.txt`
  - Human-readable log
- `/pending_events.txt`
  - Queue of unsent events to be synced to Supabase
- `/db_series.txt`
  - Time-series dB logs for bulk upload
- `/rec_YYYYMMDD_HHMMSS.wav` (or `/rec_<millis>.wav`)
  - 5-second WAV clips for major events

## Supabase / Backend Requirements
The firmware uses **Supabase REST + Storage**. It expects:
- A Supabase project URL
- An API key (currently embedded in firmware)
- Database tables for:
  - `noise_events`
  - `noise_event_audio`
  - `noise_db_series`
  - `profiles` (to check `role` for admin)
- A Storage bucket named: `recordings`

### Tables used (fields used by firmware)
#### `noise_events`
The firmware upserts (`on_conflict=id`) with payload including:
- `id` (uuid)
- `event_group_id` (uuid)
- `device_id` (string)
- `warning_level` (FIRST/SECOND/MAJOR)
- `warning_color` (currently always `RED`)
- `duration_seconds` (int)
- `decibel` (int)
- `event_ts_ms` (bigint, optional if time is set)
- `audio_url` (string, only for MAJOR)
- `buzzer_triggered` (bool)
- `audio_recorded` (bool)

#### `noise_event_audio`
Upserted (`on_conflict=noise_event_id`) with:
- `noise_event_id` (uuid)
- `audio_url` (string)
- `audio_seconds` (int, firmware uses 5)

#### `noise_db_series`
Bulk-inserted with:
- `device_id` (string)
- `ts_ms` (bigint)
- `db10` (int)  // dB * 10

#### `profiles`
Device UI checks role via:
- `GET /rest/v1/profiles?select=role&id=eq.<user_id>&limit=1`
Expected field:
- `role` (e.g., `admin`)

### Authentication used by the device UI
The device UI performs a password grant login:
- `POST /auth/v1/token?grant_type=password`

It stores the session token in browser local storage and uses it to check `profiles.role`.

## Configuration Notes
### Wi‑Fi credentials
- Stored in ESP32 NVS (`Preferences`) namespace `wifi` keys:
  - `ssid`
  - `password`

### Device settings
Stored in NVS (`Preferences`) namespace `settings` including:
- Thresholds (`yellow`, `red`)
- Timers (`fw_ms`, `sw_ms`, `mw_ms`, `maj_int`, `sil_win`)
- LED brightness (`ngbrt`, `nybrt`, `nrbrt`, `stbrt`)
- Status LED colors (`sr_boot`, `sr_ap`, `sr_wifi`, `sr_noi`, `sr_off`)
- MP3 volume (`mp3vol`) + speaker enable (`speaker`)
- MIC enable (`micen`) + serial logging (`serlog`)
- DB logging config (`db_samp`, `db_thr10`, `db_hb`, `db_up`)

## How to Build and Upload (Arduino IDE)
### Prerequisites
- Arduino IDE
- ESP32 board support installed (Arduino-ESP32)
- USB driver for your ESP32 board (if needed)

### Steps
1. Open `releasev1.ino` in Arduino IDE.
2. Select board:
   - Tools -> Board -> ESP32 Arduino -> (your ESP32 board)
3. Select port:
   - Tools -> Port -> (your COM port)
4. Upload.
5. Open Serial Monitor at `115200` baud for logs.

## How to Use (End-to-End)
1. Power the device.
2. Connect your phone/laptop to `ESP32_NOISE_Setup`.
3. Open `http://192.168.4.1/`.
4. Scan Wi‑Fi, choose the school network, and save credentials.
5. Once connected, open the device using the shown STA IP (e.g., `http://192.168.1.50/`).
6. Log in using an **admin account** (Supabase Auth). Admin role is validated via `profiles.role`.
7. Configure thresholds, warning timers, and brightness as needed.
8. Observe live monitor and logs.

## Privacy, Ethics, and Safeguards
- **No continuous recording**: audio is recorded only for confirmed MAJOR violations.
- **No live listening**: the device does not provide streaming audio.
- **Short duration**: audio clips are limited to **5 seconds**.
- **Access control**: admin UI requires authentication; backend dashboard should enforce RBAC.
- Recommended institutional controls:
  - Notice/consent policy
  - Retention policy (e.g., auto delete within 7–14 days)
  - Audit logging of access to recordings

## Troubleshooting
### Cannot find the AP / cannot open setup page
- Ensure the device is powered and ESP32 is running.
- Connect to SSID `ESP32_NOISE_Setup` with password `12345678`.
- Open `http://192.168.4.1/`.

### Wi‑Fi connects but no internet
- The status LED may indicate “Connected (No Internet)”.
- Check firewall/captive portal requirements in the network.
- The firmware checks internet via `http://clients3.google.com/generate_204`.

### SD card not detected
- Confirm wiring and correct SD module voltage level.
- Use UI diagnostic `GET /sdinfo`.
- Re-init from UI (`/sdreinit`).

### MP3 module not detected
- Verify UART wiring (ESP32 RX2 GPIO16 to MP3 TX, ESP32 TX2 GPIO17 to MP3 RX).
- Verify power and speaker connection.
- Use UI MP3 test buttons.

### MIC reads 0 / “MIC error detected”
- Check INMP441 wiring and correct pin mapping.
- Ensure common ground.
- Check I2S pins: WS=25, SCK=26, SD=33.

### Supabase sync errors
- Ensure Wi‑Fi internet access.
- Verify Supabase URL, API key, table policies, and Storage bucket.
- Check `/events` log for HTTP errors.

## Security Notes (Important)
- **Do not ship hardcoded secrets**.
- This firmware currently contains `SUPABASE_URL` and `SUPABASE_API_KEY` in source.
  - For production/capstone demonstrations, prefer:
    - using a restricted key,
    - applying strict RLS policies,
    - rotating keys after demo,
    - or moving secrets to a provisioning flow.

## Future Improvements
- SPL calibration workflow and per-device calibration constants
- Per-room device configuration (room/building metadata)
- Separate teacher/admin dashboards
- Automated retention/deletion of recordings
- Better offline sync conflict handling and metrics

## Authors / Acknowledgements
(Insert your group members, adviser, and institution here.)

## License
(Add your license here if required by your capstone/institution.)
