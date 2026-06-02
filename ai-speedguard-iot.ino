#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ===== WiFi Access Point =====
const char *AP_SSID = "ESP32";
const char *AP_PASSWORD = "12345678";

// ===== ESP32 pins =====
const uint8_t LCD_SDA_PIN = 21;
const uint8_t LCD_SCL_PIN = 22;
const uint8_t SENSOR_1_PIN = 32;
const uint8_t SENSOR_2_PIN = 33;
const uint8_t VOICE_PIN = 26;
const uint8_t BUZZER_PIN = 27;

// ===== LCD I2C =====
const uint8_t LCD_ADDRESS = 0x27;
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

// ===== Speed sensor settings =====
const bool SENSOR_ACTIVE_LOW = true;
const unsigned long DEBOUNCE_MS = 60;
const float GAP_CM = 10.0;
const float SPEED_CALIBRATION_FACTOR = 40.0;
const unsigned long SPEED_TIMEOUT_US = 5000000UL;

// ===== Buzzer settings =====
const unsigned long BUZZER_TOTAL_MS = 2000;
const unsigned long BEEP_ON_MS = 150;
const unsigned long BEEP_OFF_MS = 150;

// ===== ISD1820 settings =====
const unsigned long VOICE_TRIGGER_MS = 300;
const unsigned long VOICE_COOLDOWN_MS = 3000;

// ===== Required global state =====
float lastSpeedKmh = 0.0;
float speedLimitKmh = 60.0;
unsigned long violationCount = 0;
float lastViolationSpeed = 0.0;
String currentStatus = "READY";
float maxSpeedKmh = 0.0;
unsigned long totalMeasurements = 0;

WebServer server(80);
bool littleFsReady = false;

// ===== Violation log =====
const uint8_t MAX_VIOLATION_LOGS = 50;

struct ViolationLog {
  unsigned long id;
  unsigned long uptimeMs;
  unsigned long epochSec;
  float speedKmh;
  float limitKmh;
};

ViolationLog violationLogs[MAX_VIOLATION_LOGS];
uint8_t violationLogCount = 0;
uint8_t violationLogNext = 0;

bool browserTimeSynced = false;
unsigned long syncedEpochSec = 0;
unsigned long syncedAtMs = 0;

// ===== Speed history =====
const uint8_t MAX_SPEED_HISTORY = 80;

struct SpeedHistoryItem {
  unsigned long id;
  unsigned long uptimeMs;
  unsigned long epochSec;
  float speedKmh;
  float limitKmh;
  bool violation;
};

SpeedHistoryItem speedHistory[MAX_SPEED_HISTORY];
uint8_t speedHistoryCount = 0;
uint8_t speedHistoryNext = 0;

struct DebouncedSensor {
  uint8_t pin;
  bool rawActive;
  bool stableActive;
  bool activeEdge;
  unsigned long changedAtMs;
};

DebouncedSensor sensor1 = {SENSOR_1_PIN, false, false, false, 0};
DebouncedSensor sensor2 = {SENSOR_2_PIN, false, false, false, 0};

int firstSensor = 0;
unsigned long firstSensorMicros = 0;

bool buzzerRunning = false;
unsigned long buzzerStartMs = 0;

bool voicePulseRunning = false;
bool voiceHasTriggered = false;
unsigned long voicePulseStartMs = 0;
unsigned long lastVoiceMs = 0;

char lcdLine2Cache[LCD_COLS + 1] = "";

bool readSensorActive(uint8_t pin) {
  int value = digitalRead(pin);
  return SENSOR_ACTIVE_LOW ? value == LOW : value == HIGH;
}

void beginDebouncedSensor(DebouncedSensor &sensor, unsigned long nowMs) {
  sensor.rawActive = readSensorActive(sensor.pin);
  sensor.stableActive = sensor.rawActive;
  sensor.activeEdge = false;
  sensor.changedAtMs = nowMs;
}

void updateDebouncedSensor(DebouncedSensor &sensor, unsigned long nowMs) {
  sensor.activeEdge = false;

  bool currentRaw = readSensorActive(sensor.pin);
  if (currentRaw != sensor.rawActive) {
    sensor.rawActive = currentRaw;
    sensor.changedAtMs = nowMs;
  }

  if (sensor.rawActive != sensor.stableActive && nowMs - sensor.changedAtMs >= DEBOUNCE_MS) {
    sensor.stableActive = sensor.rawActive;
    sensor.activeEdge = sensor.stableActive;
  }
}

void printLcdLine(uint8_t row, const char *text) {
  char buffer[LCD_COLS + 1];
  uint8_t i = 0;

  while (i < LCD_COLS && text[i] != '\0') {
    buffer[i] = text[i];
    i++;
  }

  while (i < LCD_COLS) {
    buffer[i] = ' ';
    i++;
  }

  buffer[LCD_COLS] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buffer);
}

void updateSpeedLcd() {
  char speedText[8];
  char line[LCD_COLS + 1];

  dtostrf(lastSpeedKmh, 5, 1, speedText);
  snprintf(line, sizeof(line), "Speed:%s km/h", speedText);
  printLcdLine(0, line);
}

void updateAlertLcd(const char *message) {
  if (strncmp(lcdLine2Cache, message, LCD_COLS) == 0) {
    return;
  }

  strncpy(lcdLine2Cache, message, LCD_COLS);
  lcdLine2Cache[LCD_COLS] = '\0';
  printLcdLine(1, lcdLine2Cache);
}

float calculateSpeedKmh(unsigned long dtUs) {
  return (GAP_CM * 36000.0 / dtUs) * SPEED_CALIBRATION_FACTOR;
}

unsigned long currentEpochSec(unsigned long nowMs) {
  if (!browserTimeSynced) {
    return 0;
  }

  return syncedEpochSec + ((nowMs - syncedAtMs) / 1000UL);
}

unsigned long resolveEpochSec(unsigned long storedEpochSec, unsigned long uptimeMs) {
  if (storedEpochSec > 0) {
    return storedEpochSec;
  }

  if (!browserTimeSynced) {
    return 0;
  }

  if (uptimeMs >= syncedAtMs) {
    return syncedEpochSec + ((uptimeMs - syncedAtMs) / 1000UL);
  }

  unsigned long deltaSec = (syncedAtMs - uptimeMs) / 1000UL;
  if (deltaSec > syncedEpochSec) {
    return 0;
  }

  return syncedEpochSec - deltaSec;
}

void addSpeedHistory(unsigned long nowMs, bool violation) {
  totalMeasurements++;

  if (lastSpeedKmh > maxSpeedKmh) {
    maxSpeedKmh = lastSpeedKmh;
  }

  SpeedHistoryItem &item = speedHistory[speedHistoryNext];
  item.id = totalMeasurements;
  item.uptimeMs = nowMs;
  item.epochSec = currentEpochSec(nowMs);
  item.speedKmh = lastSpeedKmh;
  item.limitKmh = speedLimitKmh;
  item.violation = violation;

  speedHistoryNext = (speedHistoryNext + 1) % MAX_SPEED_HISTORY;
  if (speedHistoryCount < MAX_SPEED_HISTORY) {
    speedHistoryCount++;
  }
}

uint8_t getSpeedHistoryIndex(uint8_t logicalIndex) {
  if (speedHistoryCount < MAX_SPEED_HISTORY) {
    return logicalIndex;
  }

  return (speedHistoryNext + logicalIndex) % MAX_SPEED_HISTORY;
}

void addViolationLog(unsigned long nowMs) {
  ViolationLog &log = violationLogs[violationLogNext];
  log.id = violationCount;
  log.uptimeMs = nowMs;
  log.epochSec = currentEpochSec(nowMs);
  log.speedKmh = lastSpeedKmh;
  log.limitKmh = speedLimitKmh;

  violationLogNext = (violationLogNext + 1) % MAX_VIOLATION_LOGS;
  if (violationLogCount < MAX_VIOLATION_LOGS) {
    violationLogCount++;
  }
}

uint8_t getViolationLogIndex(uint8_t logicalIndex) {
  if (violationLogCount < MAX_VIOLATION_LOGS) {
    return logicalIndex;
  }

  return (violationLogNext + logicalIndex) % MAX_VIOLATION_LOGS;
}

unsigned long getLogEpochSec(const ViolationLog &log) {
  return resolveEpochSec(log.epochSec, log.uptimeMs);
}

void startBuzzer(unsigned long nowMs) {
  buzzerRunning = true;
  buzzerStartMs = nowMs;
}

void handleBuzzer(unsigned long nowMs) {
  if (!buzzerRunning) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  unsigned long elapsed = nowMs - buzzerStartMs;
  if (elapsed >= BUZZER_TOTAL_MS) {
    buzzerRunning = false;
    digitalWrite(BUZZER_PIN, LOW);
    updateAlertLcd("");
    return;
  }

  unsigned long cycleMs = BEEP_ON_MS + BEEP_OFF_MS;
  unsigned long cyclePos = elapsed % cycleMs;
  digitalWrite(BUZZER_PIN, cyclePos < BEEP_ON_MS ? HIGH : LOW);
}

void triggerVoice(unsigned long nowMs) {
  if (voicePulseRunning) {
    return;
  }

  if (!voiceHasTriggered || nowMs - lastVoiceMs >= VOICE_COOLDOWN_MS) {
    voiceHasTriggered = true;
    lastVoiceMs = nowMs;
    voicePulseStartMs = nowMs;
    voicePulseRunning = true;
    digitalWrite(VOICE_PIN, HIGH);
  }
}

void handleVoice(unsigned long nowMs) {
  if (voicePulseRunning && nowMs - voicePulseStartMs >= VOICE_TRIGGER_MS) {
    voicePulseRunning = false;
    digitalWrite(VOICE_PIN, LOW);
  }
}

void setSafeResult() {
  currentStatus = "SAFE";
  updateAlertLcd("");
}

void setTimeoutResult() {
  currentStatus = "TIMEOUT";
  firstSensor = 0;
  firstSensorMicros = 0;
  updateAlertLcd("");
  Serial.println("Speed measurement timeout");
}

void setOverSpeedResult(unsigned long nowMs) {
  currentStatus = "OVER_SPEED";
  violationCount++;
  lastViolationSpeed = lastSpeedKmh;
  addViolationLog(nowMs);

  Serial.print("Over speed: ");
  Serial.print(lastSpeedKmh, 1);
  Serial.println(" km/h");

  updateAlertLcd("VUOT TOC DO!");
  startBuzzer(nowMs);
  triggerVoice(nowMs);
}

void handleSensorTrigger(int sensorNumber, unsigned long nowMs, unsigned long nowUs) {
  Serial.print("Sensor ");
  Serial.print(sensorNumber);
  Serial.println(" triggered");

  if (firstSensor == 0) {
    firstSensor = sensorNumber;
    firstSensorMicros = nowUs;
    currentStatus = "MEASURING";
    updateAlertLcd("");
    return;
  }

  if (firstSensor == sensorNumber) {
    firstSensorMicros = nowUs;
    currentStatus = "MEASURING";
    return;
  }

  unsigned long dtUs = nowUs - firstSensorMicros;
  firstSensor = 0;
  firstSensorMicros = 0;

  if (dtUs == 0 || dtUs > SPEED_TIMEOUT_US) {
    setTimeoutResult();
    return;
  }

  lastSpeedKmh = calculateSpeedKmh(dtUs);
  updateSpeedLcd();

  Serial.print("Speed: ");
  Serial.print(lastSpeedKmh, 1);
  Serial.print(" km/h, limit: ");
  Serial.print(speedLimitKmh, 1);
  Serial.print(" km/h, dt: ");
  Serial.print(dtUs);
  Serial.println(" us");

  bool isViolation = lastSpeedKmh > speedLimitKmh;
  addSpeedHistory(nowMs, isViolation);

  if (isViolation) {
    setOverSpeedResult(nowMs);
  } else {
    setSafeResult();
  }
}

void handleSpeedSensors(unsigned long nowMs, unsigned long nowUs) {
  if (sensor1.activeEdge) {
    handleSensorTrigger(1, nowMs, nowUs);
  }

  if (sensor2.activeEdge) {
    handleSensorTrigger(2, nowMs, nowUs);
  }

  if (firstSensor != 0 && nowUs - firstSensorMicros > SPEED_TIMEOUT_US) {
    setTimeoutResult();
  }
}

void sendStaticFile(const char *path, const char *contentType) {
  if (!littleFsReady) {
    server.send(500, "text/plain", "LittleFS not mounted");
    return;
  }

  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File file = LittleFS.open(path, "r");
  server.streamFile(file, contentType);
  file.close();
}

void handleDataApi() {
  String json = "{";
  json += "\"speed\":" + String(lastSpeedKmh, 1) + ",";
  json += "\"limit\":" + String(speedLimitKmh, 1) + ",";
  json += "\"status\":\"" + currentStatus + "\",";
  json += "\"violations\":" + String(violationCount) + ",";
  json += "\"lastViolation\":" + String(lastViolationSpeed, 1) + ",";
  json += "\"maxSpeed\":" + String(maxSpeedKmh, 1) + ",";
  json += "\"measurements\":" + String(totalMeasurements);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleSetLimitApi() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }

  float value = server.arg("value").toFloat();
  if (value <= 0.0 || value > 300.0) {
    server.send(400, "text/plain", "Invalid value");
    return;
  }

  speedLimitKmh = value;
  server.send(200, "text/plain", "OK");
}

void handleLogsApi() {
  String json = "{";
  json += "\"timeSynced\":" + String(browserTimeSynced ? "true" : "false") + ",";
  json += "\"count\":" + String(violationLogCount) + ",";
  json += "\"items\":[";

  for (uint8_t i = 0; i < violationLogCount; i++) {
    uint8_t index = getViolationLogIndex(i);
    ViolationLog &log = violationLogs[index];

    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"id\":" + String(log.id) + ",";
    json += "\"uptimeMs\":" + String(log.uptimeMs) + ",";
    json += "\"epoch\":" + String(getLogEpochSec(log)) + ",";
    json += "\"speed\":" + String(log.speedKmh, 1) + ",";
    json += "\"limit\":" + String(log.limitKmh, 1);
    json += "}";
  }

  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleHistoryApi() {
  String json = "{";
  json += "\"timeSynced\":" + String(browserTimeSynced ? "true" : "false") + ",";
  json += "\"count\":" + String(speedHistoryCount) + ",";
  json += "\"total\":" + String(totalMeasurements) + ",";
  json += "\"maxSpeed\":" + String(maxSpeedKmh, 1) + ",";
  json += "\"items\":[";

  for (uint8_t i = 0; i < speedHistoryCount; i++) {
    uint8_t index = getSpeedHistoryIndex(i);
    SpeedHistoryItem &item = speedHistory[index];

    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"id\":" + String(item.id) + ",";
    json += "\"uptimeMs\":" + String(item.uptimeMs) + ",";
    json += "\"epoch\":" + String(resolveEpochSec(item.epochSec, item.uptimeMs)) + ",";
    json += "\"speed\":" + String(item.speedKmh, 1) + ",";
    json += "\"limit\":" + String(item.limitKmh, 1) + ",";
    json += "\"status\":\"";
    json += item.violation ? "OVER_SPEED" : "SAFE";
    json += "\"";
    json += "}";
  }

  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleExportCsv() {
  String csv = "id,epoch_sec,uptime_ms,speed_kmh,limit_kmh,status\n";

  for (uint8_t i = 0; i < speedHistoryCount; i++) {
    uint8_t index = getSpeedHistoryIndex(i);
    SpeedHistoryItem &item = speedHistory[index];

    csv += String(item.id) + ",";
    csv += String(resolveEpochSec(item.epochSec, item.uptimeMs)) + ",";
    csv += String(item.uptimeMs) + ",";
    csv += String(item.speedKmh, 1) + ",";
    csv += String(item.limitKmh, 1) + ",";
    csv += String(item.violation ? "OVER_SPEED" : "SAFE") + "\n";
  }

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=ai-speedguard-history.csv");
  server.send(200, "text/csv; charset=utf-8", csv);
}

void handleSyncTimeApi() {
  if (!server.hasArg("epoch")) {
    server.send(400, "text/plain", "Missing epoch");
    return;
  }

  unsigned long value = strtoul(server.arg("epoch").c_str(), NULL, 10);
  if (value == 0) {
    server.send(400, "text/plain", "Invalid epoch");
    return;
  }

  syncedEpochSec = value;
  syncedAtMs = millis();
  browserTimeSynced = true;
  server.send(200, "text/plain", "OK");
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    sendStaticFile("/index.html", "text/html; charset=utf-8");
  });

  server.on("/log", HTTP_GET, []() {
    sendStaticFile("/log.html", "text/html; charset=utf-8");
  });

  server.on("/log.html", HTTP_GET, []() {
    sendStaticFile("/log.html", "text/html; charset=utf-8");
  });

  server.on("/style.css", HTTP_GET, []() {
    sendStaticFile("/style.css", "text/css; charset=utf-8");
  });

  server.on("/script.js", HTTP_GET, []() {
    sendStaticFile("/script.js", "application/javascript; charset=utf-8");
  });

  server.on("/log.js", HTTP_GET, []() {
    sendStaticFile("/log.js", "application/javascript; charset=utf-8");
  });

  server.on("/data", HTTP_GET, handleDataApi);
  server.on("/setLimit", HTTP_GET, handleSetLimitApi);
  server.on("/logs", HTTP_GET, handleLogsApi);
  server.on("/history", HTTP_GET, handleHistoryApi);
  server.on("/export.csv", HTTP_GET, handleExportCsv);
  server.on("/syncTime", HTTP_GET, handleSyncTimeApi);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Web server started on port 80");
}

void setupWifiAp() {
  WiFi.mode(WIFI_AP);

  IPAddress localIp(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(localIp, gateway, subnet);

  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apStarted) {
    Serial.println("WiFi AP start failed");
    return;
  }

  Serial.print("WiFi AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("WiFi AP password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("WiFi AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  updateSpeedLcd();
  printLcdLine(1, "");

  pinMode(SENSOR_1_PIN, SENSOR_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  pinMode(SENSOR_2_PIN, SENSOR_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  pinMode(VOICE_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(VOICE_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  unsigned long nowMs = millis();
  beginDebouncedSensor(sensor1, nowMs);
  beginDebouncedSensor(sensor2, nowMs);

  littleFsReady = LittleFS.begin();
  if (!littleFsReady) {
    Serial.println("LittleFS mount failed");
  }

  setupWifiAp();
  setupWebServer();

  Serial.println("AI SpeedGuard IoT ready");
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  server.handleClient();

  updateDebouncedSensor(sensor1, nowMs);
  updateDebouncedSensor(sensor2, nowMs);
  handleSpeedSensors(nowMs, nowUs);
  handleBuzzer(nowMs);
  handleVoice(nowMs);
}
