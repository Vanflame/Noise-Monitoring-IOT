#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <HardwareSerial.h>     // ✅ ADDED
#include "types.h"
#include "driver/i2s.h"
#include <math.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_wifi_types.h>
#include "web_ui.h"

// ================= LED PWM =================
// LEDC PWM is used so we can support brightness sliders.
// 8-bit resolution: duty 0-255.
static const int LEDC_FREQ_HZ = 5000;
static const int LEDC_RES_BITS = 8;
static const int LEDC_MAX = 255;

// Noise LEDs (existing discrete LEDs)
static const int CH_NOISE_GREEN = 0;
static const int CH_NOISE_YELLOW = 1;
static const int CH_NOISE_RED = 2;

// Status RGB LED (new)
static const int CH_STATUS_R = 3;
static const int CH_STATUS_G = 4;
static const int CH_STATUS_B = 5;

// Arduino-ESP32 v3 changed LEDC API (removed ledcSetup/ledcAttachPin)
// Keep one codepath by using small wrappers.
static int ledcChannelPinMap[6] = { -1, -1, -1, -1, -1, -1 };

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
#define ESP32_ARDUINO_LEDC_BY_PIN 1
#else
#define ESP32_ARDUINO_LEDC_BY_PIN 0
#endif

static inline void ledcAttachCompat(uint8_t pin, uint8_t channel) {
#if ESP32_ARDUINO_LEDC_BY_PIN
  (void)channel;
  ledcChannelPinMap[channel] = pin;
  ledcAttach(pin, LEDC_FREQ_HZ, LEDC_RES_BITS);
#else
  ledcSetup(channel, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(pin, channel);
#endif
}

static inline void ledcWriteCompat(uint8_t channel, uint32_t duty) {
#if ESP32_ARDUINO_LEDC_BY_PIN
  int pin = ledcChannelPinMap[channel];
  if (pin >= 0) ledcWrite((uint8_t)pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

// ================= WIFI & TIME =================
// const char* defaultSsid = "HUAWEI-2.4G-Y66f";
// const char* defaultPassword = "AuKN4N4w";

String networkSSID = "";
String networkPassword = "";

bool wifiConnecting = false;
bool wifiConnected = false;
unsigned long wifiStartTime = 0;
const unsigned long WIFI_TIMEOUT = 30000;
unsigned long lastWifiRetryMs = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 20000;
String wifiStatusMessage = "Not connected";

uint8_t lastStaDisconnectReason = 0;
bool lastStaDisconnectReasonValid = false;

String lastScanJson = "[]";
bool wifiScanRunning = false;
unsigned long wifiScanStartMs = 0;

Preferences preferences;
WebServer server(80);

bool speakerEnabled = true;

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

void loadDeviceSettings();
void saveDeviceSettings();
void logNetworkInfo(const String &tag);

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

void initLedPwm();
void setNoiseLedPwm(LedState s);
void updateStatusLed(unsigned long now);
void delayWithStatus(unsigned long ms);
void presetToRgb(int preset, int intensity, int &r, int &g, int &b);

void markSupabaseFail();
void markSupabaseOk();
void updateSdAvailability(unsigned long now);

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastStaDisconnectReason = info.wifi_sta_disconnected.reason;
    lastStaDisconnectReasonValid = true;
  } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    lastStaDisconnectReasonValid = false;
  }
}

void handleSetDbLogConfig();
bool appendDbSeriesRecord(uint64_t tsMs, int db10);
bool tryBulkUploadDbSeries(unsigned long now);
uint64_t getEpochMs();

const char* DEVICE_ID = "esp32_noise_01";

const char* SUPABASE_URL = "https://dprqmezzvncftaoksauz.supabase.co";
const char* SUPABASE_API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImRwcnFtZXp6dm5jZnRhb2tzYXV6Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTkzMDkxODIsImV4cCI6MjA3NDg4NTE4Mn0.wzT5ZkCoINaaGSEMXna0fW2Yv04s6DkCKVb5qk5s4G8";

const unsigned long SUPABASE_SYNC_INTERVAL_MS = 3000;
unsigned long lastSupabaseSyncTime = 0;

const char* PENDING_EVENTS_PATH = "/pending_events.txt";

const char* DB_SERIES_PATH = "/db_series.txt";

String lastRecordedWavPath = "";

const unsigned long HTTP_TIMEOUT_MS = 6000;

bool internetOk = false;
unsigned long lastInternetCheckMs = 0;
const unsigned long INTERNET_CHECK_INTERVAL_MS = 10000;

const unsigned long AP_GRACE_MS = 20000;
unsigned long apGraceUntilMs = 0;

unsigned long dbSampleIntervalMs = 100;
int dbChangeThreshold10 = 10;
unsigned long dbHeartbeatMs = 8000;
unsigned long dbBulkUploadIntervalMs = 3600000;

unsigned long lastDbSampleMs = 0;
int lastDbLogged10 = -999999;
unsigned long lastDbRecordMs = 0;
unsigned long lastDbBulkUploadMs = 0;

int mp3Volume = 30;

bool mp3Available = false;
unsigned long lastMp3RxMs = 0;
unsigned long lastMp3OkMs = 0;
uint8_t mp3ConsecutiveFailCount = 0;
bool mp3TfOnline = false;
unsigned long lastMp3ProbeMs = 0;

const int EVENT_LOG_MAX = 40;
String eventLog[EVENT_LOG_MAX];
int eventLogCount = 0;

const int MONITOR_LOG_MAX = 80;
String monitorLog[MONITOR_LOG_MAX];
int monitorLogCount = 0;
unsigned long lastMonitorLogTime = 0;
const unsigned long MONITOR_LOG_INTERVAL_MS = 250;

// Philippines timezone (UTC +8)
#define GMT_OFFSET_SEC   (8 * 3600)
#define DAYLIGHT_OFFSET  0

// ================= MP3 PLAYER =================
HardwareSerial mp3(2);          // ADDED (UART2)

// ================= PINS =================
#define LED_GREEN   14
#define LED_YELLOW  12
#define LED_RED     27

// Status RGB LED (COMMON CATHODE)
#define STATUS_LED_R 21
#define STATUS_LED_G 22
#define STATUS_LED_B 13

#define SD_CS       5

// INMP441 I2S
#define I2S_WS   25
#define I2S_SD   33
#define I2S_SCK  26
#define I2S_PORT I2S_NUM_0

// ================= AUDIO =================
#define BUFFER_LEN 256
#define NOISE_FLOOR 25000
#define SENSITIVITY 0.5

int YELLOW_THRESHOLD = 65;
int RED_THRESHOLD = 70;

// Brightness settings (0-255)
int noiseGreenBrt = 40;
int noiseYellowBrt = 40;
int noiseRedBrt = 80;
int statusLedBrt = 40;

bool noiseLedsEnabled = true;

bool micEnabled = true;
bool serialLoggingEnabled = true;

bool sdAvailable = true;
unsigned long lastSdCheckMs = 0;
unsigned long lastSdFailMs = 0;

unsigned long lastSupabaseFailMs = 0;
unsigned long lastSupabaseOkMs = 0;

unsigned long micZeroStartMs = 0;

bool lastMicErrState = false;
bool lastSupaErrState = false;

int lastSerialDb = -9999;
int lastMonitorDb = -9999;
LedState lastMonitorState = GREEN;

enum StatusColorPreset {
  SCP_OFF = 0,
  SCP_RED = 1,
  SCP_GREEN = 2,
  SCP_BLUE = 3,
  SCP_YELLOW = 4,
  SCP_CYAN = 5,
  SCP_MAGENTA = 6,
  SCP_WHITE = 7
};

int statusColorBoot = SCP_BLUE;
int statusColorAp = SCP_BLUE;
int statusColorWifiOk = SCP_GREEN;
int statusColorNoInternet = SCP_YELLOW;
int statusColorOffline = SCP_RED;

int statusRgbBoot = 0x0000FF;
int statusRgbAp = 0x0000FF;
int statusRgbWifiOk = 0x00FF00;
int statusRgbNoInternet = 0xFFFF00;
int statusRgbOffline = 0xFF0000;

bool statusLedManual = false;
int statusLedManualR = 0;
int statusLedManualG = 0;
int statusLedManualB = 0;

bool setupComplete = false;

#define HYSTERESIS_DB    3
#define SMOOTH_ALPHA    0.1

// ================= TIMING =================
#define FIRST_WARNING_TIME   5000
#define SECOND_WARNING_TIME  30000
#define MAJOR_WARNING_TIME   60000

#define LOG_INTERVAL_MS      2000
#define DB_CHANGE_LOG        2

#define AVG_WINDOW 10

// ================= GLOBALS =================
int32_t samples[BUFFER_LEN];

int rawDB = 0;
double smoothDB = 0;
int lastLoggedDB = -100;

int avgBuffer[AVG_WINDOW];
int avgIndex = 0;
bool avgFilled = false;

unsigned long redStartTime = 0;
bool firstLogged = false;
bool secondLogged = false;
bool majorLogged = false;

unsigned long lastLogTime = 0;
LedState currentState = GREEN;

String ledStateToString(LedState s) {
  switch (s) {
    case GREEN: return "GREEN";
    case YELLOW: return "YELLOW";
    case RED: return "RED";
  }
  return "UNKNOWN";
}

String genUuidV4() {
  uint32_t r[4];
  for (int i = 0; i < 4; i++) r[i] = esp_random();

  uint8_t b[16];
  for (int i = 0; i < 4; i++) {
    b[i * 4 + 0] = (r[i] >> 24) & 0xFF;
    b[i * 4 + 1] = (r[i] >> 16) & 0xFF;
    b[i * 4 + 2] = (r[i] >> 8) & 0xFF;
    b[i * 4 + 3] = r[i] & 0xFF;
  }

  b[6] = (b[6] & 0x0F) | 0x40;
  b[8] = (b[8] & 0x3F) | 0x80;

  char out[37];
  snprintf(
    out,
    sizeof(out),
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    b[0], b[1], b[2], b[3],
    b[4], b[5],
    b[6], b[7],
    b[8], b[9],
    b[10], b[11], b[12], b[13], b[14], b[15]
  );
  return String(out);
}

bool supabaseConfigured() {
  return (strlen(SUPABASE_URL) > 0) && (strlen(SUPABASE_API_KEY) > 0);
}

bool enqueuePendingEvent(const String &line) {
  File f = SD.open(PENDING_EVENTS_PATH, FILE_APPEND);
  if (!f) return false;
  f.println(line);
  f.close();
  return true;
}

bool sdReady() {
  if (!SD.begin(SD_CS, SPI, 1000000)) return false;
  return SD.cardType() != CARD_NONE;
}

uint64_t getEpochMs() {
  time_t sec = time(NULL);
  if (sec <= 1000) return 0;
  return ((uint64_t)sec * 1000ULL) + (uint64_t)(millis() % 1000);
}

bool appendDbSeriesRecord(uint64_t tsMs, int db10) {
  if (!sdReady()) return false;
  File f = SD.open(DB_SERIES_PATH, FILE_APPEND);
  if (!f) return false;
  f.print(String((unsigned long long)tsMs));
  f.print('|');
  f.println(db10);
  f.close();
  return true;
}

bool tryBulkUploadDbSeries(unsigned long now) {
  (void)now;
  if (!wifiConnected) {
    logSupabaseStatus(getTimeString() + " | DB series upload skipped: offline");
    return false;
  }
  if (!internetOk) {
    logSupabaseStatus(getTimeString() + " | DB series upload skipped: no internet");
    return false;
  }
  if (!supabaseConfigured()) {
    logSupabaseStatus(getTimeString() + " | DB series upload skipped: supabase not configured");
    return false;
  }
  if (!sdReady()) {
    logSupabaseStatus(getTimeString() + " | DB series upload skipped: SD not available");
    return false;
  }

  File in = SD.open(DB_SERIES_PATH, FILE_READ);
  if (!in) return false;

  File out = SD.open("/db_series_tmp.txt", FILE_WRITE);
  if (!out) {
    in.close();
    return false;
  }

  unsigned long startMs = millis();
  const unsigned long maxWorkMs = 800;
  const int maxBatch = 120;

  bool didUploadAny = false;

  while (in.available()) {
    server.handleClient();
    yield();

    if (millis() - startMs > maxWorkMs) {
      while (in.available()) {
        String rest = in.readStringUntil('\n');
        rest.trim();
        if (rest.length() > 0) out.println(rest);
      }
      break;
    }

    String lines[maxBatch];
    int n = 0;
    while (in.available() && n < maxBatch) {
      String line = in.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      lines[n++] = line;
    }

    if (n == 0) continue;

    logSupabaseStatus(getTimeString() + " | DB series upload: batch=" + String(n));

    String body = "[";
    for (int i = 0; i < n; i++) {
      int p = lines[i].indexOf('|');
      if (p < 0) {
        // malformed; keep it
        out.println(lines[i]);
        continue;
      }
      String ts = lines[i].substring(0, p);
      String db10s = lines[i].substring(p + 1);
      if (body.length() > 1) body += ",";
      body += "{";
      body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
      body += "\"ts_ms\":" + ts + ",";
      body += "\"db10\":" + db10s;
      body += "}";
    }
    body += "]";

    int postCode = 0;
    String resp;
    String url = String(SUPABASE_URL) + "/rest/v1/noise_db_series";
    bool ok = supabasePostJson(url, body, postCode, resp);
    if (!ok) {
      logSupabaseStatus(getTimeString() + " | DB series upload FAIL | HTTP " + String(postCode) + " | " + truncateForLog(resp, 180));
      for (int i = 0; i < n; i++) out.println(lines[i]);
      while (in.available()) {
        String rest = in.readStringUntil('\n');
        rest.trim();
        if (rest.length() > 0) out.println(rest);
      }
      break;
    }

    logSupabaseStatus(getTimeString() + " | DB series upload OK | HTTP " + String(postCode));

    didUploadAny = true;
  }

  in.close();
  out.close();

  if (didUploadAny) {
    SD.remove(DB_SERIES_PATH);
    SD.rename("/db_series_tmp.txt", DB_SERIES_PATH);
  } else {
    SD.remove("/db_series_tmp.txt");
  }
  return didUploadAny;
}

String truncateForLog(const String &s, int maxLen) {
  if (s.length() <= (unsigned)maxLen) return s;
  return s.substring(0, maxLen) + "...";
}

void logSupabaseStatus(const String &line) {
  appendEventLog(line);
  Serial.println(line);
}

int countPendingEventsOnSD() {
  if (!sdReady()) return -1;
  File f = SD.open(PENDING_EVENTS_PATH, FILE_READ);
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) count++;
  }
  f.close();
  return count;
}

bool supabasePostJson(const String &url, const String &jsonBody, int &httpCodeOut, String &responseOut) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    httpCodeOut = -1;
    responseOut = "begin_failed";
    markSupabaseFail();
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Prefer", "return=representation,resolution=merge-duplicates");

  httpCodeOut = http.POST((uint8_t*)jsonBody.c_str(), jsonBody.length());
  responseOut = http.getString();
  http.end();
  bool ok = (httpCodeOut >= 200 && httpCodeOut < 300);
  if (ok) markSupabaseOk();
  else markSupabaseFail();
  return ok;
}

bool supabaseUploadFileToRecordsBucket(const String &objectPath, const String &localFilePath, int &httpCodeOut, String &responseOut) {
  File f = SD.open(localFilePath.c_str(), FILE_READ);
  if (!f) {
    httpCodeOut = -1;
    responseOut = "file_open_failed";
    markSupabaseFail();
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/storage/v1/object/recordings/" + objectPath;
  if (!http.begin(client, url)) {
    httpCodeOut = -1;
    responseOut = "begin_failed";
    markSupabaseFail();
    f.close();
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);

  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("x-upsert", "true");

  httpCodeOut = http.sendRequest("PUT", &f, f.size());
  responseOut = http.getString();
  http.end();
  f.close();
  return (httpCodeOut >= 200 && httpCodeOut < 300);
}

String makeStorageObjectPath(const String &eventId) {
  return String(DEVICE_ID) + "/" + eventId + ".wav";
}

String makePublicStorageUrl(const String &objectPath) {
  return String(SUPABASE_URL) + "/storage/v1/object/public/records/" + objectPath;
}

bool sendNoiseEventToSupabase(
  const String &eventId,
  const String &warningLevel,
  int durationSeconds,
  int decibel,
  bool buzzerTriggered,
  bool audioRecorded,
  const String &audioLocalPath
) {
  if (!supabaseConfigured()) return false;

  String audioUrl = "";
  if (audioRecorded && audioLocalPath.length() > 0) {
    String objPath = makeStorageObjectPath(eventId);
    int upCode = 0;
    String upResp;
    if (!supabaseUploadFileToRecordsBucket(objPath, audioLocalPath, upCode, upResp)) {
      logSupabaseStatus(getTimeString() + " | Supabase upload FAIL | " + eventId + " | HTTP " + String(upCode) + " | " + truncateForLog(upResp, 180));
      return false;
    }
    logSupabaseStatus(getTimeString() + " | Supabase upload OK | " + eventId + " | HTTP " + String(upCode));
    audioUrl = makePublicStorageUrl(objPath);
  }

  String url = String(SUPABASE_URL) + "/rest/v1/noise_events?on_conflict=id";
  String body;
  body.reserve(512);
  body += "{";
  body += "\"id\":\"" + eventId + "\",";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"warning_level\":\"" + warningLevel + "\",";
  body += "\"warning_color\":\"RED\",";
  body += "\"duration_seconds\":" + String(durationSeconds) + ",";
  body += "\"decibel\":" + String(decibel) + ",";
  if (audioUrl.length() > 0) {
    body += "\"audio_url\":\"" + audioUrl + "\",";
  }
  body += "\"buzzer_triggered\":" + String(buzzerTriggered ? "true" : "false") + ",";
  body += "\"audio_recorded\":" + String(audioRecorded ? "true" : "false");
  body += "}";

  int postCode = 0;
  String response;
  if (!supabasePostJson(url, body, postCode, response)) {
    if (postCode == 409) {
      logSupabaseStatus(getTimeString() + " | Supabase insert noise_events DUPLICATE (ok) | " + eventId + " | HTTP " + String(postCode) + " | " + truncateForLog(response, 180));
    } else {
      logSupabaseStatus(getTimeString() + " | Supabase insert noise_events FAIL | " + eventId + " | HTTP " + String(postCode) + " | " + truncateForLog(response, 180));
      return false;
    }
  }
  if (postCode >= 200 && postCode < 300) {
    logSupabaseStatus(getTimeString() + " | Supabase insert noise_events OK | " + eventId + " | HTTP " + String(postCode));
  }

  if (audioUrl.length() > 0) {
    String url2 = String(SUPABASE_URL) + "/rest/v1/noise_event_audio?on_conflict=noise_event_id";
    String body2;
    body2.reserve(256);
    body2 += "{";
    body2 += "\"noise_event_id\":\"" + eventId + "\",";
    body2 += "\"audio_url\":\"" + audioUrl + "\",";
    body2 += "\"audio_seconds\":5";
    body2 += "}";

    int post2Code = 0;
    String response2;
    if (!supabasePostJson(url2, body2, post2Code, response2)) {
      if (post2Code == 409) {
        logSupabaseStatus(getTimeString() + " | Supabase insert noise_event_audio DUPLICATE (ok) | " + eventId + " | HTTP " + String(post2Code) + " | " + truncateForLog(response2, 180));
      } else {
        logSupabaseStatus(getTimeString() + " | Supabase insert noise_event_audio FAIL | " + eventId + " | HTTP " + String(post2Code) + " | " + truncateForLog(response2, 180));
        return false;
      }
    }
    if (post2Code >= 200 && post2Code < 300) {
      logSupabaseStatus(getTimeString() + " | Supabase insert noise_event_audio OK | " + eventId + " | HTTP " + String(post2Code));
    }
  }

  return true;
}

void queueRedWarningEvent(const String &warningLevel, int durationSeconds, int decibel, bool audioRecorded, const String &audioLocalPath) {
  String eventId = genUuidV4();
  String line;
  line.reserve(256);
  line += eventId;
  line += "|";
  line += warningLevel;
  line += "|";
  line += String(durationSeconds);
  line += "|";
  line += String(decibel);
  line += "|";
  line += (speakerEnabled ? "1" : "0");
  line += "|";
  line += (audioRecorded ? "1" : "0");
  line += "|";
  line += audioLocalPath;

  if (!sdReady()) {
    logSupabaseStatus(getTimeString() + " | Queue FAIL (SD not available) | " + eventId);
    sdAvailable = false;
    lastSdFailMs = millis();
    return;
  }

  if (!enqueuePendingEvent(line)) {
    logSupabaseStatus(getTimeString() + " | Queue FAIL (write error) | " + eventId);
    sdAvailable = false;
    lastSdFailMs = millis();
    return;
  }

  String msg = getTimeString() + " | Queued RED event | " + eventId + " | level=" + warningLevel;
  if (!wifiConnected) {
    msg += " | offline (wifi not connected)";
  }
  if (!supabaseConfigured()) {
    msg += " | supabase not configured";
  }
  logSupabaseStatus(msg);

  if (wifiConnected && supabaseConfigured()) {
    logSupabaseStatus(getTimeString() + " | Attempting immediate sync | " + eventId);
    trySyncPendingEvents();
  }
}

void handleScanNetworks() {
  int n = WiFi.scanComplete();
  if (n == -1) {
    if (wifiScanRunning && (millis() - wifiScanStartMs > 20000)) {
      WiFi.scanDelete();
      wifiScanRunning = false;
      lastScanJson = "[]";
    }
    server.send(200, "application/json", lastScanJson);
    return;
  }
  if (n == -2) {
    WiFi.scanDelete();
    wifiScanRunning = false;
    lastScanJson = "[]";
  }

  if (n < 0) {
    if (!wifiScanRunning) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
      wifiScanRunning = true;
      wifiScanStartMs = millis();
    }
    server.send(200, "application/json", lastScanJson);
    return;
  }

  int idx[60];
  int m = 0;
  for (int i = 0; i < n && m < 60; i++) idx[m++] = i;

  for (int i = 0; i < m - 1; i++) {
    for (int j = i + 1; j < m; j++) {
      if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i])) {
        int t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
      }
    }
  }

  String out;
  out.reserve(2048);
  out += "[";
  for (int k = 0; k < m; k++) {
    int i = idx[k];
    String ssid = WiFi.SSID(i);
    ssid.trim();
    if (ssid.length() == 0) continue;
    bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    if (out.length() > 1) out += ",";
    out += "{";
    out += "\"ssid\":\"" + jsonEscape(ssid) + "\",";
    out += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    out += "\"secure\":" + String(secure ? "true" : "false");
    out += "}";
  }
  out += "]";

  WiFi.scanDelete();
  wifiScanRunning = false;
  lastScanJson = out;
  server.send(200, "application/json", lastScanJson);
}

void trySyncPendingEvents() {
  if (!wifiConnected) return;
  if (!sdReady()) {
    logSupabaseStatus(getTimeString() + " | Supabase sync skipped: SD not available");
    sdAvailable = false;
    lastSdFailMs = millis();
    return;
  }
  if (!supabaseConfigured()) return;

  unsigned long startMs = millis();
  const unsigned long maxWorkMs = 250;
  int okCount = 0;

  File in = SD.open(PENDING_EVENTS_PATH, FILE_READ);
  if (!in) return;

  File out = SD.open("/pending_events_tmp.txt", FILE_WRITE);
  if (!out) {
    in.close();
    return;
  }

  while (in.available()) {
    server.handleClient();
    yield();

    if (millis() - startMs > maxWorkMs) {
      while (in.available()) {
        String rest = in.readStringUntil('\n');
        rest.trim();
        if (rest.length() > 0) out.println(rest);
      }
      break;
    }

    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    int p4 = line.indexOf('|', p3 + 1);
    int p5 = line.indexOf('|', p4 + 1);
    int p6 = line.indexOf('|', p5 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0 || p6 < 0) {
      out.println(line);
      continue;
    }

    String eventId = line.substring(0, p1);
    String warningLevel = line.substring(p1 + 1, p2);
    int durationSeconds = line.substring(p2 + 1, p3).toInt();
    int decibel = line.substring(p3 + 1, p4).toInt();
    bool buzzerTriggered = (line.substring(p4 + 1, p5) == "1");
    bool audioRecorded = (line.substring(p5 + 1, p6) == "1");
    String audioLocalPath = line.substring(p6 + 1);

    bool ok = sendNoiseEventToSupabase(eventId, warningLevel, durationSeconds, decibel, buzzerTriggered, audioRecorded, audioLocalPath);
    if (!ok) {
      logSupabaseStatus(getTimeString() + " | Supabase sync FAIL (kept pending) | " + eventId);
      markSupabaseFail();
      out.println(line);
    } else {
      markSupabaseOk();
      okCount++;
    }
  }

  in.close();
  out.close();

  SD.remove(PENDING_EVENTS_PATH);
  SD.rename("/pending_events_tmp.txt", PENDING_EVENTS_PATH);
}

void appendEventLog(const String &line) {
  if (eventLogCount < EVENT_LOG_MAX) {
    eventLog[eventLogCount++] = line;
    return;
  }

  for (int i = 1; i < EVENT_LOG_MAX; i++) {
    eventLog[i - 1] = eventLog[i];
  }
  eventLog[EVENT_LOG_MAX - 1] = line;
}

bool checkInternetNow() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin("http://clients3.google.com/generate_204")) {
    http.end();
    return false;
  }
  int code = http.GET();
  http.end();
  return (code == 204);
}

void appendMonitorLog(const String &line) {
  if (monitorLogCount < MONITOR_LOG_MAX) {
    monitorLog[monitorLogCount++] = line;
    return;
  }

  for (int i = 1; i < MONITOR_LOG_MAX; i++) {
    monitorLog[i - 1] = monitorLog[i];
  }
  monitorLog[MONITOR_LOG_MAX - 1] = line;
}

void loadDeviceSettings() {
  preferences.begin("settings", true);
  YELLOW_THRESHOLD = preferences.getInt("yellow", YELLOW_THRESHOLD);
  RED_THRESHOLD = preferences.getInt("red", RED_THRESHOLD);
  speakerEnabled = preferences.getBool("speaker", speakerEnabled);
  mp3Volume = preferences.getInt("mp3vol", mp3Volume);

  noiseGreenBrt = preferences.getInt("ngbrt", noiseGreenBrt);
  noiseYellowBrt = preferences.getInt("nybrt", noiseYellowBrt);
  noiseRedBrt = preferences.getInt("nrbrt", noiseRedBrt);
  statusLedBrt = preferences.getInt("stbrt", statusLedBrt);
  noiseLedsEnabled = preferences.getBool("nleden", noiseLedsEnabled);

  micEnabled = preferences.getBool("micen", micEnabled);
  serialLoggingEnabled = preferences.getBool("serlog", serialLoggingEnabled);

  statusRgbBoot = preferences.getInt("sr_boot", statusRgbBoot);
  statusRgbAp = preferences.getInt("sr_ap", statusRgbAp);
  statusRgbWifiOk = preferences.getInt("sr_wifi", statusRgbWifiOk);
  statusRgbNoInternet = preferences.getInt("sr_noi", statusRgbNoInternet);
  statusRgbOffline = preferences.getInt("sr_off", statusRgbOffline);

  dbSampleIntervalMs = (unsigned long)preferences.getInt("db_samp", (int)dbSampleIntervalMs);
  dbChangeThreshold10 = preferences.getInt("db_thr10", dbChangeThreshold10);
  dbHeartbeatMs = (unsigned long)preferences.getInt("db_hb", (int)dbHeartbeatMs);
  dbBulkUploadIntervalMs = (unsigned long)preferences.getInt("db_up", (int)dbBulkUploadIntervalMs);

  noiseGreenBrt = constrain(noiseGreenBrt, 0, LEDC_MAX);
  noiseYellowBrt = constrain(noiseYellowBrt, 0, LEDC_MAX);
  noiseRedBrt = constrain(noiseRedBrt, 0, LEDC_MAX);
  statusLedBrt = constrain(statusLedBrt, 0, LEDC_MAX);

  statusRgbBoot = constrain(statusRgbBoot, 0, 0xFFFFFF);
  statusRgbAp = constrain(statusRgbAp, 0, 0xFFFFFF);
  statusRgbWifiOk = constrain(statusRgbWifiOk, 0, 0xFFFFFF);
  statusRgbNoInternet = constrain(statusRgbNoInternet, 0, 0xFFFFFF);
  statusRgbOffline = constrain(statusRgbOffline, 0, 0xFFFFFF);

  dbSampleIntervalMs = constrain(dbSampleIntervalMs, (unsigned long)50, (unsigned long)5000);
  dbChangeThreshold10 = constrain(dbChangeThreshold10, 1, 200);
  dbHeartbeatMs = constrain(dbHeartbeatMs, (unsigned long)1000, (unsigned long)600000);
  dbBulkUploadIntervalMs = constrain(dbBulkUploadIntervalMs, (unsigned long)60000, (unsigned long)86400000);
  preferences.end();
}

void saveDeviceSettings() {
  preferences.begin("settings", false);
  preferences.putInt("yellow", YELLOW_THRESHOLD);
  preferences.putInt("red", RED_THRESHOLD);
  preferences.putBool("speaker", speakerEnabled);
  preferences.putInt("mp3vol", mp3Volume);

  preferences.putInt("ngbrt", noiseGreenBrt);
  preferences.putInt("nybrt", noiseYellowBrt);
  preferences.putInt("nrbrt", noiseRedBrt);
  preferences.putInt("stbrt", statusLedBrt);
  preferences.putBool("nleden", noiseLedsEnabled);

  preferences.putBool("micen", micEnabled);
  preferences.putBool("serlog", serialLoggingEnabled);

  preferences.putInt("sr_boot", statusRgbBoot);
  preferences.putInt("sr_ap", statusRgbAp);
  preferences.putInt("sr_wifi", statusRgbWifiOk);
  preferences.putInt("sr_noi", statusRgbNoInternet);
  preferences.putInt("sr_off", statusRgbOffline);

  preferences.putInt("db_samp", (int)dbSampleIntervalMs);
  preferences.putInt("db_thr10", dbChangeThreshold10);
  preferences.putInt("db_hb", (int)dbHeartbeatMs);
  preferences.putInt("db_up", (int)dbBulkUploadIntervalMs);
  preferences.end();
}

void markSupabaseFail() {
  bool wasErr = (lastSupabaseFailMs != 0) && (millis() - lastSupabaseFailMs <= 60000);
  lastSupabaseFailMs = millis();
  bool isErr = true;
  if (!wasErr && isErr) {
    appendEventLog(getTimeString() + " | Supabase error detected");
  }
}

void markSupabaseOk() {
  bool wasErr = (lastSupabaseFailMs != 0) && (millis() - lastSupabaseFailMs <= 60000);
  lastSupabaseOkMs = millis();
  if (wasErr) {
    appendEventLog(getTimeString() + " | Supabase OK");
  }
}

void updateSdAvailability(unsigned long now) {
  const unsigned long SD_CHECK_MS = 10000;
  if (now - lastSdCheckMs < SD_CHECK_MS) return;
  lastSdCheckMs = now;
  bool ok = sdReady();
  bool prev = sdAvailable;
  sdAvailable = ok;
  if (!ok) lastSdFailMs = now;
  if (prev != ok) {
    appendEventLog(getTimeString() + String(ok ? " | SD card OK" : " | SD card NOT available"));
  }
}

void logNetworkInfo(const String &tag) {
  String line = getTimeString() + " | " + tag;
  line += " | AP=" + WiFi.softAPIP().toString();
  if (WiFi.status() == WL_CONNECTED) {
    line += " | STA=" + WiFi.localIP().toString();
    line += " | GW=" + WiFi.gatewayIP().toString();
  } else {
    line += " | STA=not_connected";
  }
  appendEventLog(line);
  Serial.println(line);
}

void flickerActiveLed() {
  bool g = (currentState == GREEN);
  bool y = (currentState == YELLOW);
  bool r = (currentState == RED);

  for (int i = 0; i < 3; i++) {
    ledcWriteCompat(CH_NOISE_GREEN, 0);
    ledcWriteCompat(CH_NOISE_YELLOW, 0);
    ledcWriteCompat(CH_NOISE_RED, 0);
    delay(80);

    ledcWriteCompat(CH_NOISE_GREEN, g ? constrain(noiseGreenBrt, 0, LEDC_MAX) : 0);
    ledcWriteCompat(CH_NOISE_YELLOW, y ? constrain(noiseYellowBrt, 0, LEDC_MAX) : 0);
    ledcWriteCompat(CH_NOISE_RED, r ? constrain(noiseRedBrt, 0, LEDC_MAX) : 0);
    delay(80);
  }
}

void initLedPwm() {
  ledcAttachCompat(LED_GREEN, CH_NOISE_GREEN);
  ledcAttachCompat(LED_YELLOW, CH_NOISE_YELLOW);
  ledcAttachCompat(LED_RED, CH_NOISE_RED);

  ledcAttachCompat(STATUS_LED_R, CH_STATUS_R);
  ledcAttachCompat(STATUS_LED_G, CH_STATUS_G);
  ledcAttachCompat(STATUS_LED_B, CH_STATUS_B);

  ledcWriteCompat(CH_NOISE_GREEN, 0);
  ledcWriteCompat(CH_NOISE_YELLOW, 0);
  ledcWriteCompat(CH_NOISE_RED, 0);
  ledcWriteCompat(CH_STATUS_R, 0);
  ledcWriteCompat(CH_STATUS_G, 0);
  ledcWriteCompat(CH_STATUS_B, 0);
}

void setNoiseLedPwm(LedState s) {
  if (!noiseLedsEnabled) {
    ledcWriteCompat(CH_NOISE_GREEN, 0);
    ledcWriteCompat(CH_NOISE_YELLOW, 0);
    ledcWriteCompat(CH_NOISE_RED, 0);
    return;
  }
  const int g = (s == GREEN) ? constrain(noiseGreenBrt, 0, LEDC_MAX) : 0;
  const int y = (s == YELLOW) ? constrain(noiseYellowBrt, 0, LEDC_MAX) : 0;
  const int r = (s == RED) ? constrain(noiseRedBrt, 0, LEDC_MAX) : 0;
  ledcWriteCompat(CH_NOISE_GREEN, g);
  ledcWriteCompat(CH_NOISE_YELLOW, y);
  ledcWriteCompat(CH_NOISE_RED, r);
}

void updateStatusLed(unsigned long now) {
  if (statusLedManual) {
    ledcWriteCompat(CH_STATUS_R, statusLedManualR);
    ledcWriteCompat(CH_STATUS_G, statusLedManualG);
    ledcWriteCompat(CH_STATUS_B, statusLedManualB);
    return;
  }

  updateSdAvailability(now);
  const bool micError = (micZeroStartMs != 0) && (now - micZeroStartMs >= 3000);
  const bool sdError = !sdAvailable;
  const bool supaError = (lastSupabaseFailMs != 0) && (now - lastSupabaseFailMs <= 60000);
  const bool mp3Error = !mp3Available;
  const bool anyError = sdError || supaError || micError || mp3Error;

  const bool isBooting = !setupComplete;
  const bool apEnabled = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  const bool staConnected = (WiFi.status() == WL_CONNECTED);
  const bool apGrace = (apGraceUntilMs > now);

  bool blink = false;
  unsigned long periodMs = 0;
  int r = 0, g = 0, b = 0;
  int rgb = 0;

  const int brt = constrain(statusLedBrt, 0, LEDC_MAX);

  if (anyError) {
    rgb = statusRgbOffline;
    r = (((rgb >> 16) & 0xFF) * brt) / 255;
    g = (((rgb >> 8) & 0xFF) * brt) / 255;
    b = (((rgb) & 0xFF) * brt) / 255;
    blink = true;
    periodMs = 1200;
  } else if (isBooting) {
    rgb = statusRgbBoot;
    r = (((rgb >> 16) & 0xFF) * brt) / 255;
    g = (((rgb >> 8) & 0xFF) * brt) / 255;
    b = (((rgb) & 0xFF) * brt) / 255;
    blink = true;
    periodMs = 1200;
  } else if (apEnabled && !staConnected && apGrace) {
    rgb = statusRgbAp;
    r = (((rgb >> 16) & 0xFF) * brt) / 255;
    g = (((rgb >> 8) & 0xFF) * brt) / 255;
    b = (((rgb) & 0xFF) * brt) / 255;
    blink = true;
    periodMs = 300;
  } else if (staConnected) {
    if (internetOk) {
      rgb = statusRgbWifiOk;
      r = (((rgb >> 16) & 0xFF) * brt) / 255;
      g = (((rgb >> 8) & 0xFF) * brt) / 255;
      b = (((rgb) & 0xFF) * brt) / 255;
      blink = false;
    } else {
      rgb = statusRgbNoInternet;
      r = (((rgb >> 16) & 0xFF) * brt) / 255;
      g = (((rgb >> 8) & 0xFF) * brt) / 255;
      b = (((rgb) & 0xFF) * brt) / 255;
      blink = false;
    }
  } else {
    rgb = statusRgbOffline;
    r = (((rgb >> 16) & 0xFF) * brt) / 255;
    g = (((rgb >> 8) & 0xFF) * brt) / 255;
    b = (((rgb) & 0xFF) * brt) / 255;
    blink = true;
    periodMs = 1200;
  }

  bool on = true;
  if (blink && periodMs > 0) {
    on = ((now / (periodMs / 2)) % 2) == 0;
  }

  ledcWriteCompat(CH_STATUS_R, on ? r : 0);
  ledcWriteCompat(CH_STATUS_G, on ? g : 0);
  ledcWriteCompat(CH_STATUS_B, on ? b : 0);
}

void presetToRgb(int preset, int intensity, int &r, int &g, int &b) {
  intensity = constrain(intensity, 0, LEDC_MAX);
  r = g = b = 0;
  switch (preset) {
    case SCP_OFF: r = 0; g = 0; b = 0; break;
    case SCP_RED: r = intensity; break;
    case SCP_GREEN: g = intensity; break;
    case SCP_BLUE: b = intensity; break;
    case SCP_YELLOW: r = intensity; g = intensity; break;
    case SCP_CYAN: g = intensity; b = intensity; break;
    case SCP_MAGENTA: r = intensity; b = intensity; break;
    case SCP_WHITE: r = intensity; g = intensity; b = intensity; break;
    default: r = 0; g = 0; b = 0; break;
  }
}

void delayWithStatus(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateStatusLed(millis());
    delay(10);
  }
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "TIME_NOT_SET";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void setMP3Volume(uint8_t vol) {
  vol = constrain(vol, 0, 30);
  uint8_t selectTF[] = {0x7E, 0x03, 0x35, 0x01, 0xEF};
  uint8_t setVol[]   = {0x7E, 0x03, 0x31, vol, 0xEF};
  mp3.write(selectTF, sizeof(selectTF));
  delay(80);
  mp3.write(setVol, sizeof(setVol));
}

void playMP3(uint8_t track) {
  if (!speakerEnabled) return;
  uint8_t selectTF[] = {0x7E, 0x03, 0x35, 0x01, 0xEF};
  uint8_t setVol[]   = {0x7E, 0x03, 0x31, (uint8_t)constrain(mp3Volume, 0, 30), 0xEF};
  uint8_t playCmd[]  = {0x7E, 0x04, 0x42, 0x01, track, 0xEF};

  mp3.write(selectTF, sizeof(selectTF));
  delay(200);
  mp3.write(setVol, sizeof(setVol));
  delay(200);
  mp3.write(playCmd, sizeof(playCmd));
}

void stopMP3() {
  uint8_t selectTF[] = {0x7E, 0x03, 0x35, 0x01, 0xEF};
  uint8_t stopCmd[]  = {0x7E, 0x02, 0x16, 0xEF};
  uint8_t pauseCmd[] = {0x7E, 0x02, 0x0E, 0xEF};
  mp3.write(selectTF, sizeof(selectTF));
  delay(80);
  mp3.write(stopCmd, sizeof(stopCmd));
  delay(80);
  mp3.write(pauseCmd, sizeof(pauseCmd));
}

static inline void mp3WriteCmd(uint8_t cmd, const uint8_t *data, size_t dataLen) {
  uint8_t len = (uint8_t)(2 + dataLen);
  mp3.write((uint8_t)0x7E);
  mp3.write(len);
  mp3.write(cmd);
  for (size_t i = 0; i < dataLen; i++) mp3.write(data[i]);
  mp3.write((uint8_t)0xEF);
}

static bool mp3ReadFrame(uint8_t &cmdOut, uint8_t *dataOut, size_t dataOutCap, size_t &dataLenOut, unsigned long timeoutMs) {
  unsigned long start = millis();
  bool started = false;
  uint8_t len = 0;
  uint8_t payload[16];
  size_t payloadLen = 0;

  while (millis() - start < timeoutMs) {
    while (mp3.available()) {
      uint8_t b = (uint8_t)mp3.read();
      lastMp3RxMs = millis();

      if (!started) {
        if (b == 0x7E) {
          started = true;
          payloadLen = 0;
          len = 0;
        }
        continue;
      }

      if (len == 0) {
        len = b;
        if (len < 2) {
          started = false;
          len = 0;
        }
        continue;
      }

      size_t needPayload = (size_t)(len - 1);
      if (payloadLen < needPayload) {
        if (payloadLen < sizeof(payload)) payload[payloadLen] = b;
        payloadLen++;
        continue;
      }

      if (b != 0xEF) {
        started = false;
        len = 0;
        payloadLen = 0;
        continue;
      }

      if (payloadLen < 1) {
        started = false;
        len = 0;
        payloadLen = 0;
        continue;
      }

      cmdOut = payload[0];
      size_t dlen = (payloadLen >= 1) ? (payloadLen - 1) : 0;
      dataLenOut = (dlen > dataOutCap) ? dataOutCap : dlen;
      for (size_t i = 0; i < dataLenOut; i++) dataOut[i] = payload[i + 1];
      return true;
    }
    delay(2);
  }
  return false;
}

static bool mp3Query1(uint8_t cmd, uint8_t &valOut) {
  while (mp3.available()) mp3.read();
  mp3WriteCmd(cmd, nullptr, 0);
  uint8_t rcmd = 0;
  uint8_t data[4];
  size_t dlen = 0;
  if (!mp3ReadFrame(rcmd, data, sizeof(data), dlen, 180)) return false;
  if (rcmd != cmd) return false;
  if (dlen < 1) return false;
  valOut = data[0];
  return true;
}

static void probeMp3() {
  uint8_t online = 0;
  bool ok = mp3Query1(0x18, online);
  const unsigned long now = millis();
  if (ok) {
    mp3ConsecutiveFailCount = 0;
    mp3Available = true;
    lastMp3OkMs = now;
    mp3TfOnline = (online == 2);
    return;
  }

  // Don't immediately mark missing on a single failed probe; require sustained failures.
  if (mp3ConsecutiveFailCount < 255) mp3ConsecutiveFailCount++;
  const bool graceExpired = (lastMp3OkMs != 0) && (now - lastMp3OkMs >= 12000);
  if (graceExpired && mp3ConsecutiveFailCount >= 2) {
    mp3Available = false;
    mp3TfOnline = false;
  }
}

void handleRoot() {
  String html = FPSTR(INDEX_HTML);
  html.replace("__SUPABASE_URL__", String(SUPABASE_URL));
  html.replace("__SUPABASE_ANON_KEY__", String(SUPABASE_API_KEY));
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html", html);
}

void handleStatus() {
  String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  String out;
  out.reserve(256);
  out += "{";
  out += "\"connected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
  out += "\"internet\":" + String(internetOk ? "true" : "false") + ",";
  out += "\"apGrace\":" + String((apGraceUntilMs > millis()) ? "true" : "false") + ",";
  out += "\"setupSsid\":\"ESP32_NOISE_Setup\",";
  out += "\"ssid\":\"" + jsonEscape(ssid) + "\",";
  out += "\"rssi\":" + String(rssi) + ",";
  out += "\"ip\":\"" + ((WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("")) + "\",";
  out += "\"gw\":\"" + ((WiFi.status() == WL_CONNECTED) ? WiFi.gatewayIP().toString() : String("")) + "\",";
  out += "\"apip\":\"" + WiFi.softAPIP().toString() + "\",";
  out += "\"yellow\":" + String(YELLOW_THRESHOLD) + ",";
  out += "\"red\":" + String(RED_THRESHOLD) + ",";
  out += "\"ngbrt\":" + String(noiseGreenBrt) + ",";
  out += "\"nybrt\":" + String(noiseYellowBrt) + ",";
  out += "\"nrbrt\":" + String(noiseRedBrt) + ",";
  out += "\"stbrt\":" + String(statusLedBrt) + ",";
  out += "\"nleden\":" + String(noiseLedsEnabled ? "true" : "false") + ",";
  out += "\"micen\":" + String(micEnabled ? "true" : "false") + ",";
  out += "\"serlog\":" + String(serialLoggingEnabled ? "true" : "false") + ",";
  out += "\"sderr\":" + String((!sdAvailable) ? "true" : "false") + ",";
  out += "\"supaerr\":" + String(((lastSupabaseFailMs != 0) && (millis() - lastSupabaseFailMs <= 60000)) ? "true" : "false") + ",";
  out += "\"micerr\":" + String(((micZeroStartMs != 0) && (millis() - micZeroStartMs >= 3000)) ? "true" : "false") + ",";
  out += "\"sc_boot\":" + String(statusColorBoot) + ",";
  out += "\"sc_ap\":" + String(statusColorAp) + ",";
  out += "\"sc_wifi\":" + String(statusColorWifiOk) + ",";
  out += "\"sc_noi\":" + String(statusColorNoInternet) + ",";
  out += "\"sc_off\":" + String(statusColorOffline) + ",";
  out += "\"sr_boot\":" + String(statusRgbBoot) + ",";
  out += "\"sr_ap\":" + String(statusRgbAp) + ",";
  out += "\"sr_wifi\":" + String(statusRgbWifiOk) + ",";
  out += "\"sr_noi\":" + String(statusRgbNoInternet) + ",";
  out += "\"sr_off\":" + String(statusRgbOffline) + ",";
  out += "\"db_samp\":" + String(dbSampleIntervalMs) + ",";
  out += "\"db_thr10\":" + String(dbChangeThreshold10) + ",";
  out += "\"db_hb\":" + String(dbHeartbeatMs) + ",";
  out += "\"db_up\":" + String(dbBulkUploadIntervalMs) + ",";
  out += "\"mp3vol\":" + String(mp3Volume) + ",";
  out += "\"speaker\":" + String(speakerEnabled ? "true" : "false") + ",";
  out += "\"mp3err\":" + String((!mp3Available) ? "true" : "false") + ",";
  out += "\"mp3tferr\":" + String((mp3Available && !mp3TfOnline) ? "true" : "false");
  out += "}";
  server.send(200, "application/json", out);
}

void handleSetLedBrightness() {
  int prevNg = noiseGreenBrt;
  int prevNy = noiseYellowBrt;
  int prevNr = noiseRedBrt;
  int prevSt = statusLedBrt;
  if (server.hasArg("ng")) noiseGreenBrt = constrain(server.arg("ng").toInt(), 0, LEDC_MAX);
  if (server.hasArg("ny")) noiseYellowBrt = constrain(server.arg("ny").toInt(), 0, LEDC_MAX);
  if (server.hasArg("nr")) noiseRedBrt = constrain(server.arg("nr").toInt(), 0, LEDC_MAX);
  if (server.hasArg("st")) statusLedBrt = constrain(server.arg("st").toInt(), 0, LEDC_MAX);

  saveDeviceSettings();
  setNoiseLedPwm(currentState);
  if (noiseGreenBrt != prevNg) appendEventLog(getTimeString() + " | Noise LED Green brightness=" + String(noiseGreenBrt));
  if (noiseYellowBrt != prevNy) appendEventLog(getTimeString() + " | Noise LED Yellow brightness=" + String(noiseYellowBrt));
  if (noiseRedBrt != prevNr) appendEventLog(getTimeString() + " | Noise LED Red brightness=" + String(noiseRedBrt));
  if (statusLedBrt != prevSt) appendEventLog(getTimeString() + " | Status RGB brightness=" + String(statusLedBrt));
  server.send(204);
}

static int parseRgbHex(const String &s) {
  String t = s;
  t.trim();
  if (t.startsWith("#")) t = t.substring(1);
  if (t.length() != 6) return -1;
  for (int i = 0; i < 6; i++) {
    char c = t[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!ok) return -1;
  }
  return (int)strtol(t.c_str(), NULL, 16);
}

void handleSetStatusRgb() {
  int prevBoot = statusRgbBoot;
  int prevAp = statusRgbAp;
  int prevWifi = statusRgbWifiOk;
  int prevNoi = statusRgbNoInternet;
  int prevOff = statusRgbOffline;
  if (server.hasArg("boot")) {
    int v = parseRgbHex(server.arg("boot"));
    if (v >= 0) statusRgbBoot = v;
  }
  if (server.hasArg("ap")) {
    int v = parseRgbHex(server.arg("ap"));
    if (v >= 0) statusRgbAp = v;
  }
  if (server.hasArg("wifi")) {
    int v = parseRgbHex(server.arg("wifi"));
    if (v >= 0) statusRgbWifiOk = v;
  }
  if (server.hasArg("noi")) {
    int v = parseRgbHex(server.arg("noi"));
    if (v >= 0) statusRgbNoInternet = v;
  }
  if (server.hasArg("off")) {
    int v = parseRgbHex(server.arg("off"));
    if (v >= 0) statusRgbOffline = v;
  }
  statusLedManual = false;
  saveDeviceSettings();
  if (statusRgbBoot != prevBoot) appendEventLog(getTimeString() + " | Status RGB Boot set");
  if (statusRgbAp != prevAp) appendEventLog(getTimeString() + " | Status RGB AP set");
  if (statusRgbWifiOk != prevWifi) appendEventLog(getTimeString() + " | Status RGB WiFi OK set");
  if (statusRgbNoInternet != prevNoi) appendEventLog(getTimeString() + " | Status RGB No Internet set");
  if (statusRgbOffline != prevOff) appendEventLog(getTimeString() + " | Status RGB Offline set");
  server.send(204);
}

void handleSetDbLogConfig() {
  unsigned long prevSamp = dbSampleIntervalMs;
  int prevThr10 = dbChangeThreshold10;
  unsigned long prevHb = dbHeartbeatMs;
  unsigned long prevUp = dbBulkUploadIntervalMs;
  if (server.hasArg("samp")) dbSampleIntervalMs = (unsigned long)server.arg("samp").toInt();
  if (server.hasArg("thr10")) dbChangeThreshold10 = server.arg("thr10").toInt();
  if (server.hasArg("hb")) dbHeartbeatMs = (unsigned long)server.arg("hb").toInt();
  if (server.hasArg("up")) dbBulkUploadIntervalMs = (unsigned long)server.arg("up").toInt();

  dbSampleIntervalMs = constrain(dbSampleIntervalMs, (unsigned long)50, (unsigned long)5000);
  dbChangeThreshold10 = constrain(dbChangeThreshold10, 1, 200);
  dbHeartbeatMs = constrain(dbHeartbeatMs, (unsigned long)1000, (unsigned long)600000);
  dbBulkUploadIntervalMs = constrain(dbBulkUploadIntervalMs, (unsigned long)60000, (unsigned long)86400000);

  saveDeviceSettings();
  if (dbSampleIntervalMs != prevSamp) appendEventLog(getTimeString() + " | DB series sample_ms=" + String(dbSampleIntervalMs));
  if (dbChangeThreshold10 != prevThr10) appendEventLog(getTimeString() + " | DB series change_db=" + String(dbChangeThreshold10 / 10.0f, 1));
  if (dbHeartbeatMs != prevHb) appendEventLog(getTimeString() + " | DB series heartbeat_ms=" + String(dbHeartbeatMs));
  if (dbBulkUploadIntervalMs != prevUp) appendEventLog(getTimeString() + " | DB series upload_ms=" + String(dbBulkUploadIntervalMs));
  server.send(204);
}

void handleSetMicEnabled() {
  if (server.hasArg("enabled")) {
    bool prev = micEnabled;
    micEnabled = (server.arg("enabled") == "1");
    saveDeviceSettings();
    if (!micEnabled) {
      micZeroStartMs = 0;
    }
    if (micEnabled != prev) appendEventLog(getTimeString() + " | MIC set to " + String(micEnabled ? "ON" : "OFF"));
  }
  server.send(204);
}

void handleSetSerialLoggingEnabled() {
  if (server.hasArg("enabled")) {
    bool prev = serialLoggingEnabled;
    serialLoggingEnabled = (server.arg("enabled") == "1");
    saveDeviceSettings();
    if (serialLoggingEnabled != prev) appendEventLog(getTimeString() + " | Serial log set to " + String(serialLoggingEnabled ? "ON" : "OFF"));
  }
  server.send(204);
}

void handleSetNoiseLedsEnabled() {
  if (server.hasArg("enabled")) {
    bool prev = noiseLedsEnabled;
    noiseLedsEnabled = (server.arg("enabled") == "1");
    saveDeviceSettings();
    setNoiseLedPwm(currentState);
    if (noiseLedsEnabled != prev) appendEventLog(getTimeString() + " | Noise LEDs set to " + String(noiseLedsEnabled ? "ON" : "OFF"));
  }
  server.send(204);
}

void handleSetStatusColors() {
  int prevBoot = statusColorBoot;
  int prevAp = statusColorAp;
  int prevWifi = statusColorWifiOk;
  int prevNoi = statusColorNoInternet;
  int prevOff = statusColorOffline;
  if (server.hasArg("boot")) statusColorBoot = constrain(server.arg("boot").toInt(), SCP_OFF, SCP_WHITE);
  if (server.hasArg("ap")) statusColorAp = constrain(server.arg("ap").toInt(), SCP_OFF, SCP_WHITE);
  if (server.hasArg("wifi")) statusColorWifiOk = constrain(server.arg("wifi").toInt(), SCP_OFF, SCP_WHITE);
  if (server.hasArg("noi")) statusColorNoInternet = constrain(server.arg("noi").toInt(), SCP_OFF, SCP_WHITE);
  if (server.hasArg("off")) statusColorOffline = constrain(server.arg("off").toInt(), SCP_OFF, SCP_WHITE);
  statusLedManual = false;
  saveDeviceSettings();
  if (statusColorBoot != prevBoot) appendEventLog(getTimeString() + " | Status preset Boot changed");
  if (statusColorAp != prevAp) appendEventLog(getTimeString() + " | Status preset AP changed");
  if (statusColorWifiOk != prevWifi) appendEventLog(getTimeString() + " | Status preset WiFi OK changed");
  if (statusColorNoInternet != prevNoi) appendEventLog(getTimeString() + " | Status preset No Internet changed");
  if (statusColorOffline != prevOff) appendEventLog(getTimeString() + " | Status preset Offline changed");
  server.send(204);
}

void handleStatusLedManual() {
  bool prevOn = statusLedManual;
  int prevR = statusLedManualR;
  int prevG = statusLedManualG;
  int prevB = statusLedManualB;
  if (server.hasArg("on")) {
    statusLedManual = (server.arg("on") == "1");
  }
  if (server.hasArg("r")) statusLedManualR = constrain(server.arg("r").toInt(), 0, LEDC_MAX);
  if (server.hasArg("g")) statusLedManualG = constrain(server.arg("g").toInt(), 0, LEDC_MAX);
  if (server.hasArg("b")) statusLedManualB = constrain(server.arg("b").toInt(), 0, LEDC_MAX);
  if (statusLedManual != prevOn) appendEventLog(getTimeString() + " | Status LED manual=" + String(statusLedManual ? "ON" : "OFF"));
  if (statusLedManualR != prevR || statusLedManualG != prevG || statusLedManualB != prevB) {
    appendEventLog(getTimeString() + " | Status LED manual RGB updated");
  }
  server.send(204);
}

void handleSetMp3Volume() {
  if (server.hasArg("vol")) {
    mp3Volume = constrain(server.arg("vol").toInt(), 0, 30);
    setMP3Volume((uint8_t)mp3Volume);
    saveDeviceSettings();
    appendEventLog(getTimeString() + " | MP3 volume set to " + String(mp3Volume));
  }
  server.send(204);
}

void handleSetThresholds() {
  int prevY = YELLOW_THRESHOLD;
  int prevR = RED_THRESHOLD;
  if (server.hasArg("yellow"))
    YELLOW_THRESHOLD = constrain(server.arg("yellow").toInt(), 0, 100);
  if (server.hasArg("red"))
    RED_THRESHOLD = constrain(server.arg("red").toInt(), 0, 100);

  saveDeviceSettings();

  if (YELLOW_THRESHOLD != prevY) appendEventLog(getTimeString() + " | Yellow threshold=" + String(YELLOW_THRESHOLD));
  if (RED_THRESHOLD != prevR) appendEventLog(getTimeString() + " | Red threshold=" + String(RED_THRESHOLD));

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleToggleSpeaker() {
  speakerEnabled = !speakerEnabled;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetSpeaker() {
  if (server.hasArg("enabled")) {
    speakerEnabled = (server.arg("enabled") == "1");
    appendEventLog(getTimeString() + " | Speaker set to " + String(speakerEnabled ? "ON" : "OFF"));
    saveDeviceSettings();
  }
  server.send(204);
}

void handleDisconnect() {
  preferences.begin("wifi", false);
  preferences.remove("ssid");
  preferences.remove("password");
  preferences.end();
  networkSSID = "";
  networkPassword = "";

  WiFi.disconnect(false, true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_NOISE_Setup", "12345678");
  wifiConnecting = false;
  wifiConnected = false;
  wifiStatusMessage = "Disconnected";
  appendEventLog(getTimeString() + " | WiFi disconnected");
  logNetworkInfo("WiFi disconnected");

  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePlayTest001() {
  appendEventLog(getTimeString() + " | Speaker test: play 001");
  playMP3(0x01);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePlayTest002() {
  appendEventLog(getTimeString() + " | Speaker test: play 002");
  playMP3(0x02);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePlayTest003() {
  appendEventLog(getTimeString() + " | Speaker test: play 003");
  playMP3(0x03);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStopMp3() {
  appendEventLog(getTimeString() + " | Speaker: stop");
  stopMP3();
  server.send(204);
}

void handleEvents() {
  String out;
  for (int i = 0; i < eventLogCount; i++) {
    out += eventLog[i];
    out += "\n";
  }
  server.send(200, "text/plain", out);
}

void handleMonitor() {
  String out;
  for (int i = 0; i < monitorLogCount; i++) {
    out += monitorLog[i];
    out += "\n";
  }
  server.send(200, "text/plain", out);
}

void handleNetworkConnection() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    networkSSID = server.arg("ssid");
    networkPassword = server.arg("password");

    preferences.begin("wifi", false);
    preferences.putString("ssid", networkSSID);
    preferences.putString("password", networkPassword);
    preferences.end();

    WiFi.disconnect(true);
    delay(500);
    Serial.println(String("WiFi begin (manual save) SSID=") + networkSSID);
    WiFi.begin(networkSSID.c_str(), networkPassword.c_str());

    wifiConnecting = true;
    wifiConnected = false;
    wifiStartTime = millis();
    wifiStatusMessage = "Connecting...";

    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void connectToWiFi() {
  preferences.begin("wifi", true);
  networkSSID = preferences.getString("ssid", "");
  networkPassword = preferences.getString("password", "");
  preferences.end();

  if (networkSSID.length() == 0) {
    wifiConnecting = false;
    wifiConnected = false;
    wifiStatusMessage = "Not configured";
    appendEventLog(getTimeString() + " | WiFi not configured");
    logNetworkInfo("WiFi not configured");
    return;
  }

  if (networkSSID.length() > 0) {
    lastWifiRetryMs = millis();
    Serial.println(String("WiFi begin (boot) SSID=") + networkSSID);
    WiFi.begin(networkSSID.c_str(), networkPassword.c_str());
    wifiConnecting = true;
    wifiConnected = false;
    wifiStartTime = millis();
    wifiStatusMessage = "Connecting...";
  }
}

// ================= WAV RECORDING =================
bool writeWavHeader(File &f, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize) {
  if (!f) return false;

  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  uint32_t chunkSize = 36 + dataSize;

  f.seek(0);
  f.write((const uint8_t*)"RIFF", 4);
  f.write((uint8_t*)&chunkSize, 4);
  f.write((const uint8_t*)"WAVE", 4);
  f.write((const uint8_t*)"fmt ", 4);

  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1;
  f.write((uint8_t*)&subchunk1Size, 4);
  f.write((uint8_t*)&audioFormat, 2);
  f.write((uint8_t*)&channels, 2);
  f.write((uint8_t*)&sampleRate, 4);
  f.write((uint8_t*)&byteRate, 4);
  f.write((uint8_t*)&blockAlign, 2);
  f.write((uint8_t*)&bitsPerSample, 2);

  f.write((const uint8_t*)"data", 4);
  f.write((uint8_t*)&dataSize, 4);
  return true;
}

String makeRecordingFilename() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[32];
    strftime(buf, sizeof(buf), "/rec_%Y%m%d_%H%M%S.wav", &timeinfo);
    return String(buf);
  }
  return String("/rec_") + String(millis()) + String(".wav");
}

bool recordINMP441Wav5s() {
  const uint32_t sampleRate = 16000;
  const uint16_t bitsPerSample = 16;
  const uint16_t channels = 1;
  const uint32_t durationMs = 50000;
  const uint32_t totalSamples = (sampleRate * durationMs) / 1000;
  const uint32_t targetDataBytes = totalSamples * channels * (bitsPerSample / 8);

  if (!SD.begin(SD_CS, SPI, 1000000)) {
    Serial.println("SD not available for recording");
    return false;
  }

  String filename = makeRecordingFilename();
  lastRecordedWavPath = filename;
  File f = SD.open(filename.c_str(), FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open WAV file");
    return false;
  }

  if (!writeWavHeader(f, sampleRate, bitsPerSample, channels, 0)) {
    f.close();
    return false;
  }

  size_t bytesWritten = 0;
  int32_t i2sBuf[BUFFER_LEN];
  int16_t pcmBuf[BUFFER_LEN];

  while (bytesWritten < targetDataBytes) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, (void*)i2sBuf, sizeof(i2sBuf), &bytesRead, portMAX_DELAY);
    if (err != ESP_OK || bytesRead == 0) {
      Serial.println("I2S read failed");
      break;
    }

    size_t nSamples = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < nSamples; i++) {
      int32_t s = i2sBuf[i] >> 14;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      pcmBuf[i] = (int16_t)s;
    }

    size_t bytesToWrite = nSamples * sizeof(int16_t);
    if (bytesWritten + bytesToWrite > targetDataBytes) {
      bytesToWrite = targetDataBytes - bytesWritten;
    }

    size_t w = f.write((uint8_t*)pcmBuf, bytesToWrite);
    bytesWritten += w;
    if (w != bytesToWrite) {
      Serial.println("SD write short");
      break;
    }
  }

  writeWavHeader(f, sampleRate, bitsPerSample, channels, (uint32_t)bytesWritten);
  f.close();

  Serial.print("Recorded WAV: ");
  Serial.print(filename);
  Serial.print(" bytes: ");
  Serial.println(bytesWritten);
  return bytesWritten > 0;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  pinMode(STATUS_LED_R, OUTPUT);
  pinMode(STATUS_LED_G, OUTPUT);
  pinMode(STATUS_LED_B, OUTPUT);

  SPI.begin(18, 19, 23, SD_CS);
  SD.begin(SD_CS, SPI, 1000000);

  loadDeviceSettings();

  initLedPwm();
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.onEvent(onWiFiEvent);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.softAP("ESP32_NOISE_Setup", "12345678");
  logNetworkInfo("Boot");
  connectToWiFi();

  server.on("/", handleRoot);
  server.on("/save", handleNetworkConnection);
  server.on("/scan", handleScanNetworks);
  server.on("/status", handleStatus);
  server.on("/setThresholds", handleSetThresholds);
  server.on("/toggleSpeaker", handleToggleSpeaker);
  server.on("/setSpeaker", handleSetSpeaker);
  server.on("/disconnect", handleDisconnect);
  server.on("/playTest001", handlePlayTest001);
  server.on("/playTest002", handlePlayTest002);
  server.on("/playTest003", handlePlayTest003);
  server.on("/stopMp3", handleStopMp3);
  server.on("/setMp3Volume", handleSetMp3Volume);
  server.on("/setLedBrightness", handleSetLedBrightness);
  server.on("/setNoiseLedsEnabled", handleSetNoiseLedsEnabled);
  server.on("/setMicEnabled", handleSetMicEnabled);
  server.on("/setSerialLogging", handleSetSerialLoggingEnabled);
  server.on("/setStatusColors", handleSetStatusColors);
  server.on("/setStatusRgb", handleSetStatusRgb);
  server.on("/setDbLogConfig", handleSetDbLogConfig);
  server.on("/statusLedManual", handleStatusLedManual);
  server.on("/events", handleEvents);
  server.on("/monitor", handleMonitor);
  server.begin();

  // ===== TIME SYNC =====
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, "pool.ntp.org", "time.nist.gov");
  Serial.println("Time synchronized");

  // ===== MP3 INIT (BOOT WAIT) =====  
  delayWithStatus(8000);                              // MP3 boot time
  mp3.begin(9600, SERIAL_8N1, 16, 17);      // RX, TX

  probeMp3();
  if (mp3Available) {
    setMP3Volume((uint8_t)constrain(mp3Volume, 0, 30));
  }

  // ===== I2S SETUP =====
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);

  Serial.println("=== Stable Noise Monitoring System ===");
  setupComplete = true;
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  while (mp3.available()) {
    (void)mp3.read();
    lastMp3RxMs = now;
    mp3Available = true;
    lastMp3OkMs = now;
    mp3ConsecutiveFailCount = 0;
  }

  if (now - lastMp3ProbeMs >= 5000) {
    lastMp3ProbeMs = now;
    probeMp3();
  }

  server.handleClient();

  // Error transition audit logs (avoid spamming; log only on change)
  const bool micErrNow = (micZeroStartMs != 0) && (now - micZeroStartMs >= 3000);
  if (micErrNow != lastMicErrState) {
    appendEventLog(getTimeString() + String(micErrNow ? " | MIC error detected" : " | MIC OK"));
    lastMicErrState = micErrNow;
  }
  const bool supaErrNow = (lastSupabaseFailMs != 0) && (now - lastSupabaseFailMs <= 60000);
  if (supaErrNow != lastSupaErrState) {
    appendEventLog(getTimeString() + String(supaErrNow ? " | Supabase error detected" : " | Supabase OK"));
    lastSupaErrState = supaErrNow;
  }

  // Auto turn off Setup AP after grace period once STA is connected
  if (wifiConnected && (WiFi.status() == WL_CONNECTED) && (apGraceUntilMs > 0) && (now > apGraceUntilMs)) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apGraceUntilMs = 0;
    logNetworkInfo("AP turned off");
  }

  updateStatusLed(now);

  // Keep cached flags in sync with real interface status
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected && (now - lastInternetCheckMs >= INTERNET_CHECK_INTERVAL_MS)) {
    internetOk = checkInternetNow();
    lastInternetCheckMs = now;
  }

  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      wifiConnecting = false;
      wifiStatusMessage = "Connected";
      appendEventLog(getTimeString() + " | WiFi connected | IP: " + WiFi.localIP().toString());

      apGraceUntilMs = now + AP_GRACE_MS;
      WiFi.mode(WIFI_AP_STA);
      logNetworkInfo("WiFi connected");
      internetOk = checkInternetNow();
      lastInternetCheckMs = now;
    } else if (millis() - wifiStartTime > WIFI_TIMEOUT) {
      wifiConnecting = false;
      wifiConnected = false;
      wifiStatusMessage = "Connection Failed";
      appendEventLog(getTimeString() + " | WiFi connection failed");

      Serial.print("WiFi connect timeout. status=");
      Serial.print(WiFi.status());
      Serial.print(" reason=");
      if (lastStaDisconnectReasonValid) {
        Serial.print((int)lastStaDisconnectReason);
        Serial.print(" ");
        Serial.println(WiFi.disconnectReasonName((wifi_err_reason_t)lastStaDisconnectReason));
      } else {
        Serial.println("unknown");
      }

      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("ESP32_NOISE_Setup1", "12345678");
      logNetworkInfo("WiFi connect failed");
    }
  }

  if (!wifiConnected && !wifiConnecting && (networkSSID.length() > 0) && (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS)) {
    lastWifiRetryMs = now;
    WiFi.disconnect(true);
    delay(200);
    Serial.println(String("WiFi begin (retry) SSID=") + networkSSID);
    WiFi.begin(networkSSID.c_str(), networkPassword.c_str());
    wifiConnecting = true;
    wifiStartTime = millis();
    wifiStatusMessage = "Reconnecting...";
    appendEventLog(getTimeString() + " | WiFi reconnect attempt");
    logNetworkInfo("WiFi reconnect attempt");
  }

  if (now - lastSupabaseSyncTime >= SUPABASE_SYNC_INTERVAL_MS) {
    if (!wifiConnected) {
      int pending = countPendingEventsOnSD();
      if (pending > 0) {
        logSupabaseStatus(getTimeString() + " | Supabase sync skipped: offline | pending=" + String(pending));
      }
    } else if (!supabaseConfigured()) {
      int pending = countPendingEventsOnSD();
      if (pending > 0) {
        logSupabaseStatus(getTimeString() + " | Supabase sync skipped: not configured | pending=" + String(pending));
      }
    } else {
      int pending = countPendingEventsOnSD();
      if (pending > 0) {
        logSupabaseStatus(getTimeString() + " | Supabase sync tick | pending=" + String(pending));
      }
      trySyncPendingEvents();
    }
    lastSupabaseSyncTime = now;
  }

  if (!micEnabled) {
    delay(50);
    return;
  }

  rawDB = readMicDB();
  smoothDB = smoothDB + SMOOTH_ALPHA * (rawDB - smoothDB);
  int avgDB = getMovingAverage((int)smoothDB);

  int smoothInt = (int)smoothDB;
  if (serialLoggingEnabled && smoothInt != lastSerialDb) {
    Serial.print("Raw: ");
    Serial.print(rawDB);
    Serial.print(" | Smooth: ");
    Serial.println(smoothInt);
    lastSerialDb = smoothInt;
  }

  if (rawDB == 0) {
    if (micZeroStartMs == 0) micZeroStartMs = now;
  } else {
    micZeroStartMs = 0;
  }

  updateLEDState((int)smoothDB);
  handleRedWarnings((int)smoothDB, now);

  // ===== CHANGE-BASED DB SERIES LOGGING (NO AUDIO STORED) =====
  if (now - lastDbSampleMs >= dbSampleIntervalMs) {
    lastDbSampleMs = now;

    int db10 = (int)lroundf(smoothDB * 10.0f);
    bool changed = (lastDbLogged10 == -999999) || (abs(db10 - lastDbLogged10) >= dbChangeThreshold10);
    bool heartbeatDue = (lastDbRecordMs == 0) || (now - lastDbRecordMs >= dbHeartbeatMs);
    if (changed || heartbeatDue) {
      uint64_t tsMs = getEpochMs();
      if (tsMs != 0) {
        if (appendDbSeriesRecord(tsMs, db10)) {
          lastDbLogged10 = db10;
          lastDbRecordMs = now;
        } else {
          sdAvailable = false;
          lastSdFailMs = now;
        }
      }
    }
  }

  if (now - lastDbBulkUploadMs >= dbBulkUploadIntervalMs) {
    if (tryBulkUploadDbSeries(now)) {
      lastDbBulkUploadMs = now;
    } else {
      // Avoid tight retry loops
      lastDbBulkUploadMs = now;
    }
  }

  if (now - lastMonitorLogTime >= MONITOR_LOG_INTERVAL_MS) {
    bool changed = (smoothInt != lastMonitorDb) || (currentState != lastMonitorState);
    if (changed) {
      String line = getTimeString();
      line += " | dB: ";
      line += String(smoothInt);
      line += " | LED: ";
      line += ledStateToString(currentState);
      appendMonitorLog(line);
      lastMonitorDb = smoothInt;
      lastMonitorState = currentState;
    }
    lastMonitorLogTime = now;
  }

  if ((now - lastLogTime >= LOG_INTERVAL_MS) &&
      abs((int)smoothDB - lastLoggedDB) >= DB_CHANGE_LOG) {

    logNoise((int)smoothDB);
    lastLoggedDB = (int)smoothDB;
    lastLogTime = now;
  }

  delay(50);
}

// ================= MIC =================
int readMicDB() {
  size_t bytes_read = 0;
  i2s_read(I2S_PORT, samples, sizeof(samples), &bytes_read, 100);
  if (bytes_read == 0) return rawDB;

  int count = bytes_read / 4;
  double sum = 0;
  for (int i = 0; i < count; i++) {
    double s = samples[i];
    sum += s * s;
  }

  double rms_raw = sqrt(sum / count);
  double rms = max(0.0, rms_raw - NOISE_FLOOR);
  return (int)(20.0 * log10(rms + 1) * SENSITIVITY);
}

// ================= MOVING AVERAGE =================
int getMovingAverage(int value) {
  avgBuffer[avgIndex++] = value;
  if (avgIndex >= AVG_WINDOW) {
    avgIndex = 0;
    avgFilled = true;
  }

  int sum = 0;
  int count = avgFilled ? AVG_WINDOW : avgIndex;
  for (int i = 0; i < count; i++) sum += avgBuffer[i];
  return sum / count;
}

// ================= LED =================
void updateLEDState(int value) {
  switch (currentState) {
    case GREEN:
      if (value > YELLOW_THRESHOLD + HYSTERESIS_DB)
        currentState = YELLOW;
      break;

    case YELLOW:
      if (value > RED_THRESHOLD + HYSTERESIS_DB)
        currentState = RED;
      else if (value < YELLOW_THRESHOLD - HYSTERESIS_DB)
        currentState = GREEN;
      break;

    case RED:
      if (value < RED_THRESHOLD - HYSTERESIS_DB)
        currentState = YELLOW;
      break;
  }

  setNoiseLedPwm(currentState);
}

// ================= WARNING LOGIC =================
void handleRedWarnings(int value, unsigned long now) {
  if (value >= RED_THRESHOLD) {
    if (redStartTime == 0) {
      redStartTime = now;
      firstLogged = secondLogged = majorLogged = false;
    }

    unsigned long d = now - redStartTime;
    if (d >= FIRST_WARNING_TIME && !firstLogged) {
      logEvent("FIRST WARNING (RED 5s)");
      flickerActiveLed();
      playMP3(0x01);     // 001.mp3
      queueRedWarningEvent("FIRST", 5, value, false, "");
      firstLogged = true;
    }
    if (d >= SECOND_WARNING_TIME && !secondLogged) {
      logEvent("SECOND WARNING (RED 30s)");
      flickerActiveLed();
      playMP3(0x02);     // 002.mp3
      queueRedWarningEvent("SECOND", 30, value, false, "");
      secondLogged = true;
    }
    if (d >= MAJOR_WARNING_TIME && !majorLogged) {
      logEvent("MAJOR WARNING (RED 60s)");
      flickerActiveLed();
      recordINMP441Wav5s();
      playMP3(0x03);     // 003.mp3
      queueRedWarningEvent("MAJOR", 60, value, true, lastRecordedWavPath);
      majorLogged = true;
    }
  } else {
    redStartTime = 0;
    firstLogged = secondLogged = majorLogged = false;
  }
}

// ================= SD LOGGING =================
void logNoise(int value) {
  File f = SD.open("/noise_log.txt", FILE_APPEND);
  if (f) {
    f.print("Time(ms): ");
    f.print(millis());
    f.print(" | dB: ");
    f.println(value);
    f.close();
  }
}

void logEvent(const char* msg) {
  String timeStr = getTimeString();
  int dbValue = (int)smoothDB;

  appendEventLog(timeStr + " | " + String(msg) + " | dB: " + String(dbValue));

  Serial.print("⚠️ ");
  Serial.print(timeStr);
  Serial.print(" | ");
  Serial.print(msg);
  Serial.print(" | dB: ");
  Serial.println(dbValue);

  File f = SD.open("/noise_log.txt", FILE_APPEND);
  if (f) {
    f.print(timeStr);
    f.print(" | ");
    f.print(msg);
    f.print(" | dB: ");
    f.println(dbValue);
    f.close();
  }
}
