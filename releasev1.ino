#include <SPI.h> 
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <HardwareSerial.h>
#include "driver/i2s.h"
#include <math.h> 
#include <WebServer.h>


// ================= WIFI & TIME =================
const char* apSSID = "ESP32_NOISE_MONITOR";
const char* apPassword = "12345678";

// Philippines timezone (UTC +8)
#define GMT_OFFSET_SEC   (8 * 3600)
#define DAYLIGHT_OFFSET  0

WebServer server(80);

// ================= MP3 PLAYER =================
HardwareSerial mp3(2);

// ================= PINS =================
#define LED_GREEN   14
#define LED_YELLOW  12
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

int yellowThreshold = 65;
int redThreshold    = 70;

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

enum LedState { GREEN, YELLOW, RED };
LedState currentState = GREEN;

// Speaker toggle (AP mode)
bool speakerEnabled = true;

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
void playMP3(uint8_t track) {
  uint8_t selectTF[] = {0x7E, 0x03, 0x35, 0x01, 0xEF};
  uint8_t setVol[]   = {0x7E, 0x03, 0x31, 0x1E, 0xEF};
  uint8_t playCmd[]  = {0x7E, 0x04, 0x42, 0x01, track, 0xEF};

  mp3.write(selectTF, sizeof(selectTF));
  delay(200);
  mp3.write(setVol, sizeof(setVol));
  delay(200);
  if(speakerEnabled) mp3.write(playCmd, sizeof(playCmd));
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
  const uint32_t durationMs = 5000;
  const uint32_t totalSamples = (sampleRate * durationMs) / 1000;
  const uint32_t targetDataBytes = totalSamples * channels * (bitsPerSample / 8);

  if (!SD.begin(SD_CS, SPI, 1000000)) {
    Serial.println("SD not available for recording");
    return false;
  }

  String filename = makeRecordingFilename();
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

// ================= WEB PAGE =================
String generateHTML() {
  String html = "<!DOCTYPE html><html><head><title>Noise Monitor</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>";
  html += "<h2>Noise Monitor Settings</h2>";
  
  html += "<form action='/update' method='POST'>";
  html += "Yellow Threshold: <input type='range' name='yellow' min='0' max='120' value='" + String(yellowThreshold) + "' oninput='this.nextElementSibling.value = this.value'><output>" + String(yellowThreshold) + "</output><br>";
  html += "Red Threshold: <input type='range' name='red' min='0' max='120' value='" + String(redThreshold) + "' oninput='this.nextElementSibling.value = this.value'><output>" + String(redThreshold) + "</output><br>";
  html += "Speaker: <input type='checkbox' name='speaker' " + String(speakerEnabled ? "checked" : "") + "><br><br>";
  html += "<input type='submit' value='Update'>";
  html += "</form></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", generateHTML());
}

void handleUpdate() {
  if (server.hasArg("yellow")) yellowThreshold = server.arg("yellow").toInt();
  if (server.hasArg("red")) redThreshold = server.arg("red").toInt();
  speakerEnabled = server.hasArg("speaker");
  server.sendHeader("Location", "/");
  server.send(303);
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

  // ===== AP MODE =====
  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP Mode started. Connect to WiFi: " + String(apSSID));
  Serial.println("IP: " + WiFi.softAPIP().toString());

  server.on("/", handleRoot);
  server.on("/update", handleUpdate);
  server.begin();

  // ===== MP3 INIT =====
  delay(8000);
  mp3.begin(9600, SERIAL_8N1, 16, 17);

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
  server.handleClient();

  unsigned long now = millis();

  rawDB = readMicDB();
  smoothDB = smoothDB + SMOOTH_ALPHA * (rawDB - smoothDB);
  int avgDB = getMovingAverage((int)smoothDB);

  Serial.print("Raw: ");
  Serial.print(rawDB);
  Serial.print(" | Smooth: ");
  Serial.println((int)smoothDB);

  updateLEDState((int)smoothDB);
  handleRedWarnings((int)smoothDB, now);

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
      if (value > yellowThreshold + HYSTERESIS_DB)
        currentState = YELLOW;
      break;

    case YELLOW:
      if (value > redThreshold + HYSTERESIS_DB)
        currentState = RED;
      else if (value < yellowThreshold - HYSTERESIS_DB)
        currentState = GREEN;
      break;

    case RED:
      if (value < redThreshold - HYSTERESIS_DB)
        currentState = YELLOW;
      break;
  }

  digitalWrite(LED_GREEN, currentState == GREEN);
  digitalWrite(LED_YELLOW, currentState == YELLOW);
  digitalWrite(LED_RED, currentState == RED);
}

// ================= WARNING LOGIC =================
void handleRedWarnings(int value, unsigned long now) {
  if (value >= redThreshold) {
    if (redStartTime == 0) {
      redStartTime = now;
      firstLogged = secondLogged = majorLogged = false;
    }

    unsigned long d = now - redStartTime;
    if (d >= FIRST_WARNING_TIME && !firstLogged) {
      logEvent("FIRST WARNING (RED 5s)");
      playMP3(0x01);
      firstLogged = true;
    }
    if (d >= SECOND_WARNING_TIME && !secondLogged) {
      logEvent("SECOND WARNING (RED 30s)");
      playMP3(0x02);
      secondLogged = true;
    }
    if (d >= MAJOR_WARNING_TIME && !majorLogged) {
      logEvent("MAJOR WARNING (RED 60s)");
      recordINMP441Wav5s();
      playMP3(0x03);
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
