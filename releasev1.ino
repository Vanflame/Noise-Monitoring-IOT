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
#include "web_ui.h"

// ================= WIFI & TIME =================
const char* defaultSsid = "HUAWEI-2.4G-Y66f";
const char* defaultPassword = "AuKN4N4w";

String networkSSID = "";
String networkPassword = "";

bool wifiConnecting = false;
bool wifiConnected = false;
unsigned long wifiStartTime = 0;
const unsigned long WIFI_TIMEOUT = 15000;
String wifiStatusMessage = "Not connected";

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

const char* DEVICE_ID = "esp32_noise_01";

const char* SUPABASE_URL = "https://dprqmezzvncftaoksauz.supabase.co";
const char* SUPABASE_API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImRwcnFtZXp6dm5jZnRhb2tzYXV6Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTkzMDkxODIsImV4cCI6MjA3NDg4NTE4Mn0.wzT5ZkCoINaaGSEMXna0fW2Yv04s6DkCKVb5qk5s4G8";

const unsigned long SUPABASE_SYNC_INTERVAL_MS = 3000;
unsigned long lastSupabaseSyncTime = 0;

const char* PENDING_EVENTS_PATH = "/pending_events.txt";

String lastRecordedWavPath = "";

const unsigned long HTTP_TIMEOUT_MS = 6000;

bool internetOk = false;
unsigned long lastInternetCheckMs = 0;
const unsigned long INTERNET_CHECK_INTERVAL_MS = 10000;

const unsigned long AP_GRACE_MS = 20000;
unsigned long apGraceUntilMs = 0;

int mp3Volume = 30;

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
HardwareSerial mp3(2);          // ✅ ADDED (UART2)

// ================= PINS =================
#define LED_GREEN   12
#define LED_YELLOW  14
#define LED_RED     27
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
  return (httpCodeOut >= 200 && httpCodeOut < 300);
}

bool supabaseUploadFileToRecordsBucket(const String &objectPath, const String &localFilePath, int &httpCodeOut, String &responseOut) {
  File f = SD.open(localFilePath.c_str(), FILE_READ);
  if (!f) {
    httpCodeOut = -1;
    responseOut = "file_open_failed";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/storage/v1/object/recordings/" + objectPath;
  if (!http.begin(client, url)) {
    httpCodeOut = -1;
    responseOut = "begin_failed";
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
    return;
  }

  if (!enqueuePendingEvent(line)) {
    logSupabaseStatus(getTimeString() + " | Queue FAIL (write error) | " + eventId);
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
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    server.send(500, "application/json", "[]");
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
  server.send(200, "application/json", out);
}

void trySyncPendingEvents() {
  if (!wifiConnected) return;
  if (!sdReady()) {
    logSupabaseStatus(getTimeString() + " | Supabase sync skipped: SD not available");
    return;
  }
  if (!supabaseConfigured()) return;

  unsigned long startMs = millis();
  const unsigned long maxWorkMs = 250;

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
      out.println(line);
    } else {
      logSupabaseStatus(getTimeString() + " | Supabase sync OK (removed pending) | " + eventId);
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
  return (code == 204 || (code >= 200 && code < 300));
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
  preferences.begin("cfg", true);
  YELLOW_THRESHOLD = preferences.getInt("yellow", YELLOW_THRESHOLD);
  RED_THRESHOLD = preferences.getInt("red", RED_THRESHOLD);
  speakerEnabled = preferences.getBool("speaker", speakerEnabled);
  mp3Volume = preferences.getInt("mp3vol", mp3Volume);
  mp3Volume = constrain(mp3Volume, 0, 30);
  preferences.end();
}

void saveDeviceSettings() {
  preferences.begin("cfg", false);
  preferences.putInt("yellow", YELLOW_THRESHOLD);
  preferences.putInt("red", RED_THRESHOLD);
  preferences.putBool("speaker", speakerEnabled);
  preferences.putInt("mp3vol", mp3Volume);
  preferences.end();
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
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    delay(80);

    digitalWrite(LED_GREEN, g);
    digitalWrite(LED_YELLOW, y);
    digitalWrite(LED_RED, r);
    delay(80);
  }
}

// ================= TIME STRING =================
String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "TIME_NOT_SET";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

// ================= MP3 PLAY FUNCTION =================
// ADDED — does NOT touch existing logic
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

void handleRoot() {
  String html = FPSTR(INDEX_HTML);
  html.replace("__SUPABASE_URL__", String(SUPABASE_URL));
  html.replace("__SUPABASE_ANON_KEY__", String(SUPABASE_API_KEY));
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
  out += "\"mp3vol\":" + String(mp3Volume) + ",";
  out += "\"speaker\":" + String(speakerEnabled ? "true" : "false");
  out += "}";
  server.send(200, "application/json", out);
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
  if (server.hasArg("yellow"))
    YELLOW_THRESHOLD = constrain(server.arg("yellow").toInt(), 0, 100);
  if (server.hasArg("red"))
    RED_THRESHOLD = constrain(server.arg("red").toInt(), 0, 100);

  saveDeviceSettings();

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
    networkSSID = defaultSsid;
    networkPassword = defaultPassword;
  }

  if (networkSSID.length() > 0) {
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
  delay(2000);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  SPI.begin(18, 19, 23, SD_CS);
  SD.begin(SD_CS, SPI, 1000000);

  loadDeviceSettings();

  WiFi.mode(WIFI_AP_STA);
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
  server.on("/events", handleEvents);
  server.on("/monitor", handleMonitor);
  server.begin();

  // ===== TIME SYNC =====
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, "pool.ntp.org", "time.nist.gov");
  Serial.println("Time synchronized");

  // ===== MP3 INIT (BOOT WAIT) =====  ADDED
  delay(8000);                              // MP3 boot time
  mp3.begin(9600, SERIAL_8N1, 16, 17);      // RX, TX

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
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  server.handleClient();

  if (wifiConnected && (now - lastInternetCheckMs >= INTERNET_CHECK_INTERVAL_MS)) {
    internetOk = checkInternetNow();
    lastInternetCheckMs = now;
  }

  if (wifiConnected && apGraceUntilMs > 0 && now >= apGraceUntilMs) {
    apGraceUntilMs = 0;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    logNetworkInfo("AP disabled (grace ended)");
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

      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("ESP32_NOISE_Setup", "12345678");
      logNetworkInfo("WiFi connect failed");
    }
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

  rawDB = readMicDB();
  smoothDB = smoothDB + SMOOTH_ALPHA * (rawDB - smoothDB);
  int avgDB = getMovingAverage((int)smoothDB);

  Serial.print("Raw: ");
  Serial.print(rawDB);
  Serial.print(" | Smooth: ");
  Serial.println((int)smoothDB);

  updateLEDState((int)smoothDB);
  handleRedWarnings((int)smoothDB, now);

  if (now - lastMonitorLogTime >= MONITOR_LOG_INTERVAL_MS) {
    String line = getTimeString();
    line += " | dB: ";
    line += String((int)smoothDB);
    line += " | LED: ";
    line += ledStateToString(currentState);
    appendMonitorLog(line);
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

  digitalWrite(LED_GREEN, currentState == GREEN);
  digitalWrite(LED_YELLOW, currentState == YELLOW);
  digitalWrite(LED_RED, currentState == RED);
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
