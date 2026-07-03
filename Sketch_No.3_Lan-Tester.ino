#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Mapped 8-pin Array to handle active states natively
const int lanPins[8] = {5, 4, 0, 2, 14, 12, 13, 16};
int pinStates[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int pinSignalCount[8] = {0, 0, 0, 0, 0, 0, 0, 0};
bool pinAckState[8] = {false, false, false, false, false, false, false, false};
unsigned long pinAckSince[8] = {0, 0, 0, 0, 0, 0, 0, 0};
const unsigned long pinAckDisplayMs = 5000;

#define BUZZER_PIN 15 
#define DHTPIN 2      
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
bool dhtAvailable = false;

const char* ssid = "Yash_Network's_Limited";
const char* apPassword = "yash@1234";
const IPAddress apIP(192, 168, 2, 1);
const IPAddress netMask(255, 255, 255, 0);
ESP8266WebServer server(80);
DNSServer dnsServer;

enum ViewMode { VIEW_LAN, VIEW_CLOCK_ZONES, VIEW_STOPWATCH, VIEW_SENSORS, VIEW_SYNC, VIEW_COUNTDOWN, VIEW_GATE_TIMER, VIEW_TODOS, VIEW_DIAGNOSTICS, VIEW_POWER, VIEW_CLIENTS };
ViewMode currentView = VIEW_LAN;

bool countdownRunning = false;
unsigned long countdownLastTick = 0;
unsigned long gateTimerLastTick = 0;
float gateTimerAccumulator = 0.0;
bool powerSavingMode = false;

bool alarmTuneAscending = true;
int alarmToneFreq = 800;
unsigned long alarmTuneLastMs = 0;

unsigned long lastTick = 0;
long localEpochSeconds = 43200; 
int offsetIST = 19800;
int offsetEST = -18000;         
int offsetGMT = 0;              

int currentYear = 2026;
int currentMonth = 6;
int currentDay = 23;

int timeMultiplier = 1; 

bool chronoRunning = false;
unsigned long chronoElapsedBase = 0;
unsigned long chronoStartMillis = 0;

long countdownTotalSeconds = 0;
long initialCountdownSeconds = 0;

long gateTimerTotalSeconds = 0;
long initialGateTimerSeconds = 0;
float gateTimerSpeed = 1.0;
bool gateTimerActive = false;

bool alarmActive = false;
unsigned long alarmToggleMillis = 0;
bool alarmToneState = false;

float currentTemp = 24.0; 
float currentHumid = 50.0;
float currentHeatIndex = 24.0;
float currentDewPoint = 13.0;
String comfortLevel = "Comfortable";
String airQualityEst = "Optimal";
unsigned long lastSensorPoll = 0;

float busVoltage = 5.08;
float currentAmps = 0.084; 
float calculatedPower = 0.426;
int stationCount = 0;

struct TodoTask {
  char title[22];
  int targetHour;
  int targetMin;
  bool isCompleted;
  bool activeTriggered;
};
TodoTask taskList[4]; 
int totalTasksConfigured = 0;
String activeTaskDisplayStr = "No Active Alarm Task";

// Flash Storage State Management Model
struct BackupConfig {
  int savedView;
  unsigned long savedChronoBase;
  long savedCountdownSeconds;
  int savedMultiplier;
  long savedInitialCountdown;
} appState;

void syncPinsToMode();
void refreshOledDisplay();
void readPins();
void computeAdvancedMetrics();
void pollPowerTelemetry();
void executeBuzzerSirenPattern();
void handleRootRequest();
void handleAppRequest();
void handleTerminalRequest();
void handleCmdRequest();
void handleStatusPayload();
void handleActionCommand();
void handleCaptiveRedirect();
void saveStateToEeprom();
void loadStateFromEeprom();
void drawProgressBar(int x, int y, int width, int height, int progress);

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  loadStateFromEeprom();
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(4, 5);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED Allocation Failure"));
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.drawRect(0, 0, 128, 64, WHITE);
  display.setCursor(8, 8);
  display.println("CORE ENGINE BOOT OK");
  display.drawLine(4, 20, 124, 20, WHITE);
  display.setCursor(8, 28);
  display.print("SSID: "); display.println(ssid);
  display.setCursor(8, 40);
  display.println("GATE IP: 192.168.2.1");
  display.display();

  Serial.println("Starting local server in AP mode...");
  Serial.println("Connect to WiFi SSID: " + String(ssid));
  Serial.println("Open http://192.168.2.1 in a browser.");

  syncPinsToMode();
  pinMode(DHTPIN, INPUT_PULLUP);
  dht.begin();
  dhtAvailable = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMask);
  WiFi.softAP(ssid, apPassword);
  dnsServer.start(53, "*", apIP);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  
  server.on("/", handleRootRequest);
  server.on("/app", handleAppRequest);
  server.on("/terminal", handleTerminalRequest);
  server.on("/status", handleStatusPayload);
  server.on("/action", handleActionCommand);
  server.on("/cmd", handleCmdRequest);
  server.on("/generate_204", handleCaptiveRedirect);
  server.on("/hotspot-detect.html", handleCaptiveRedirect);
  server.on("/ncsi.txt", handleCaptiveRedirect);
  server.on("/connecttest.txt", handleCaptiveRedirect);
  server.on("/library/test/success.html", handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);
  server.begin();

  delay(2000);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();
  if (lastTick == 0) lastTick = now;
  if (now - lastTick >= 1000) {
    unsigned long secondsPassed = (now - lastTick) / 1000;
    localEpochSeconds += secondsPassed;
    lastTick += secondsPassed * 1000;
    while (localEpochSeconds >= 86400) {
      localEpochSeconds -= 86400;
      advanceCalendarDate();
    }
  }
  int pollInterval = powerSavingMode ? 3000 : 1000;

  if (now - lastSensorPoll > pollInterval) {
    lastSensorPoll = now;

    if (dhtAvailable) {
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(t) && !isnan(h)) {
        currentTemp = t;
        currentHumid = h;
      }
    }

    computeAdvancedMetrics();
    pollPowerTelemetry();
    readPins();
    syncPinsToMode();
  }

  if (countdownRunning) {
    unsigned long elapsed = now - countdownLastTick;
    if (elapsed >= 1000) {
      long steps = elapsed / 1000;
      countdownLastTick += steps * 1000;
      countdownTotalSeconds -= steps;
      if (countdownTotalSeconds <= 0) {
        countdownTotalSeconds = 0;
        countdownRunning = false;
        alarmActive = true;
        alarmToneFreq = 800;
        alarmTuneAscending = true;
        alarmTuneLastMs = now;
        tone(BUZZER_PIN, alarmToneFreq);
      }
    }
  }

  if (alarmActive) {
    executeBuzzerSirenPattern();
  }

  if (gateTimerActive) {
    unsigned long elapsed = now - gateTimerLastTick;
    if (elapsed >= 200) {
      gateTimerLastTick = now;
      gateTimerAccumulator += (elapsed / 1000.0f) * gateTimerSpeed;
      if (gateTimerAccumulator >= 1.0f) {
        long dec = (long)gateTimerAccumulator;
        gateTimerAccumulator -= dec;
        gateTimerTotalSeconds -= dec;
        if (gateTimerTotalSeconds <= 0) {
          gateTimerTotalSeconds = 0;
          gateTimerActive = false;
          alarmActive = true;
          alarmToneFreq = 800;
          alarmTuneAscending = true;
          alarmTuneLastMs = now;
          tone(BUZZER_PIN, alarmToneFreq);
        }
      }
    }
  }

  for (int i = 0; i < 8; i++) {
    if (pinAckState[i] && (millis() - pinAckSince[i]) > pinAckDisplayMs) {
      pinAckState[i] = false;
    }
  }
}


void computeAdvancedMetrics() {
  currentHeatIndex = dht.computeHeatIndex(currentTemp, currentHumid, false);
  float a = 17.27;
  float b = 237.7;
  float alpha = ((a * currentTemp) / (b + currentTemp)) + log(currentHumid / 100.0);
  currentDewPoint = (b * alpha) / (a - alpha);

  if (currentHumid < 30) comfortLevel = "Dry Air";
  else if (currentHumid > 70) comfortLevel = "Humid";
  else comfortLevel = "Optimal";
  
  if (currentHumid >= 45 && currentHumid <= 55) airQualityEst = "Excellent";
  else airQualityEst = "Normal";
}

void pollPowerTelemetry() {
  float variance = (random(-10, 10) / 1000.0);
  busVoltage = 5.06 + (random(-1, 2) / 100.0);
  stationCount = WiFi.softAPgetStationNum();
  float baseDraw = powerSavingMode ? 0.045 : 0.082;
  currentAmps = baseDraw + (stationCount * 0.028) + variance;
  if (currentAmps < 0.02) currentAmps = 0.02;
  calculatedPower = busVoltage * currentAmps;
}

void executeBuzzerSirenPattern() {
  unsigned long now = millis();
  if (now - alarmTuneLastMs >= 160) {
    alarmToneFreq += alarmTuneAscending ? 120 : -120;
    if (alarmToneFreq >= 1800) {
      alarmToneFreq = 1800;
      alarmTuneAscending = false;
    } else if (alarmToneFreq <= 800) {
      alarmToneFreq = 800;
      alarmTuneAscending = true;
    }
    alarmTuneLastMs = now;
    tone(BUZZER_PIN, alarmToneFreq);
  }
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  if (month == 2) return isLeapYear(year) ? 29 : 28;
  if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
  return 31;
}

void advanceCalendarDate() {
  currentDay++;
  if (currentDay > daysInMonth(currentYear, currentMonth)) {
    currentDay = 1;
    currentMonth++;
    if (currentMonth > 12) {
      currentMonth = 1;
      currentYear++;
    }
  }
}

void readPins() {
  for (int i = 0; i < 8; i++) {
    pinMode(lanPins[i], INPUT_PULLUP);
    int raw = digitalRead(lanPins[i]);
    pinStates[i] = (raw == HIGH) ? 1 : 0;
  }
}

void drawOledHeader(const char *title) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 10, 127, 10, WHITE);
}

void formatTimeOutput(char *buffer, long epochSeconds, int offsetSeconds) {
  long adjusted = epochSeconds + offsetSeconds;
  adjusted %= 86400;
  if (adjusted < 0) adjusted += 86400;
  long hours = adjusted / 3600;
  long mins = (adjusted / 60) % 60;
  long secs = adjusted % 60;
  sprintf(buffer, "%02ld:%02ld:%02ld", hours, mins, secs);
}

void saveStateToEeprom() {
  appState.savedView = (int)currentView;
  appState.savedChronoBase = chronoElapsedBase;
  appState.savedCountdownSeconds = countdownTotalSeconds;
  appState.savedMultiplier = timeMultiplier;
  appState.savedInitialCountdown = initialCountdownSeconds;
  EEPROM.put(0, appState);
  EEPROM.commit();
}

void loadStateFromEeprom() {
  EEPROM.get(0, appState);
  if (appState.savedView >= VIEW_LAN && appState.savedView <= VIEW_CLIENTS) {
    currentView = (ViewMode)appState.savedView;
  } else {
    currentView = VIEW_LAN;
  }
  chronoElapsedBase = appState.savedChronoBase;
  countdownTotalSeconds = appState.savedCountdownSeconds;
  timeMultiplier = (appState.savedMultiplier > 0) ? appState.savedMultiplier : 1;
  initialCountdownSeconds = appState.savedInitialCountdown;
}

void drawProgressBar(int x, int y, int width, int height, int progress) {
  display.drawRect(x, y, width, height, WHITE);
  int fillWidth = (progress * width) / 100;
  if (fillWidth > 0) {
    display.fillRect(x + 1, y + 1, fillWidth - 1, height - 2, WHITE);
  }
}

void syncPinsToMode() {
  for (int i = 0; i < 8; i++) {
    pinMode(lanPins[i], INPUT_PULLUP);
    int raw = digitalRead(lanPins[i]);
    pinStates[i] = (raw == HIGH) ? 1 : 0;
  }

  display.clearDisplay();

  switch (currentView) {
    case VIEW_LAN:
      drawOledHeader("  LAN MATRIX V2");
      for (int i = 0; i < 8; i++) {
        int xPos = (i % 4) * 31 + 4;
        int yPos = (i / 4) * 22 + 18;
        display.drawRoundRect(xPos, yPos, 28, 18, 3, WHITE);
        if (pinStates[i]) {
          display.fillRect(xPos + 1, yPos + 1, 26, 16, WHITE);
          display.setTextColor(BLACK);
        } else {
          display.setTextColor(WHITE);
        }
        display.setCursor(xPos + 5, yPos + 3);
        display.print("P"); display.print(i + 1);
        display.setCursor(xPos + 5, yPos + 12);
        display.setTextSize(1);
        if (pinAckState[i]) {
          display.print("ACK");
        } else {
          display.print(pinStates[i] ? "ON" : "OFF");
        }
      }
      break;

    case VIEW_CLOCK_ZONES:
      drawOledHeader("  WORLD TIME SYSTEM");
      {
        char timeBuffer[16];
        formatTimeOutput(timeBuffer, localEpochSeconds, offsetIST);
        display.setCursor(4, 18); display.print("IST: "); display.println(timeBuffer);
        formatTimeOutput(timeBuffer, localEpochSeconds, offsetEST);
        display.setCursor(4, 29); display.print("EST: "); display.println(timeBuffer);
        formatTimeOutput(timeBuffer, localEpochSeconds, offsetGMT);
        display.setCursor(4, 40); display.print("GMT: "); display.println(timeBuffer);
      }
      display.drawLine(0, 52, 128, 52, WHITE);
      display.setCursor(4, 55); display.printf("Date Tracker: %02d/%02d", currentDay, currentMonth);
      break;

    case VIEW_STOPWATCH:
      drawOledHeader("  CHRONOGRAPH CORE");
      display.setTextSize(2);
      display.setCursor(16, 24);
      {
        unsigned long totalChrono = chronoElapsedBase;
        if (chronoRunning) totalChrono += (millis() - chronoStartMillis) * timeMultiplier;
        unsigned long s = (totalChrono / 1000) % 60;
        unsigned long m = (totalChrono / 60000) % 60;
        unsigned long h = (totalChrono / 3600000) % 24;
        display.printf("%02ld:%02ld:%02ld", h, m, s);
      }
      display.setTextSize(1);
      display.setCursor(4, 50);
      display.print("Status: "); display.println(chronoRunning ? "RUNNING" : "PAUSED");
      break;

    case VIEW_SENSORS:
      drawOledHeader("  TELEMETRY HUB");
      display.setCursor(4, 18); display.printf("Temp: %.1f C  \nHumi: %.0f%%", currentTemp, currentHumid);
      display.setCursor(4, 29); display.printf("Heat Ix: %.1f C", currentHeatIndex);
      display.setCursor(4, 40); display.printf("Dew Pt: %.1f C", currentDewPoint);
      display.setCursor(4, 51); display.print(comfortLevel);
      break;

    case VIEW_POWER:
      drawOledHeader("  POWER SAVER MODE");
      display.setCursor(4, 18); display.printf("Battery Saver: %s", powerSavingMode ? "ON" : "OFF");
      display.setCursor(4, 30); display.printf("Stations: %d", WiFi.softAPgetStationNum());
      display.setCursor(4, 42); display.printf("Draw: %.0f mA", currentAmps * 1000.0);
      display.setCursor(4, 52); display.printf("Power: %.3f W", calculatedPower);
      break;

    case VIEW_CLIENTS:
      drawOledHeader("  CONNECTED CLIENTS");
      display.setCursor(4, 20); display.print("Clients joined:");
      display.setTextSize(2);
      display.setCursor(4, 32);
      display.print(WiFi.softAPgetStationNum());
      display.setTextSize(1);
      display.setCursor(4, 56);
      display.print("SSID: "); display.print(ssid);
      break;

    case VIEW_SYNC:
      drawOledHeader("  DEVICE SYNC MATRIX");
      display.setTextSize(2);
      display.setCursor(16, 26);
      {
        char timeBuffer[16];
        formatTimeOutput(timeBuffer, localEpochSeconds, 0);
        display.println(timeBuffer);
      }
      break;

    case VIEW_COUNTDOWN:
      drawOledHeader("  COUNTDOWN COUNTER");
      if (countdownTotalSeconds <= 0) {
        display.setCursor(32, 30); display.println("[ FINISHED ]");
      } else {
        display.setTextSize(1);
        display.setCursor(16, 18);
        long workingSecs = countdownTotalSeconds;
        long d = workingSecs / 86400; workingSecs %= 86400;
        long h = workingSecs / 3600;  workingSecs %= 3600;
        long m = workingSecs / 60;    long s = workingSecs % 60;
        display.printf("%02ldd %02ld:%02ld:%02ld", d, h, m, s);
        
        int pct = 0;
        if (initialCountdownSeconds > 0) {
          pct = ((initialCountdownSeconds - countdownTotalSeconds) * 100) / initialCountdownSeconds;
        }
        drawProgressBar(10, 40, 108, 12, pct);
      }
      break;

    case VIEW_GATE_TIMER:
      drawOledHeader("  GATE TIME METRIC");
      display.setCursor(4, 18); display.printf("Engine Factor: %.1fx", gateTimerSpeed);
      display.setTextSize(2);
      display.setCursor(16, 32);
      {
        long workingSecs = gateTimerTotalSeconds;
        long h = workingSecs / 3600; workingSecs %= 3600;
        long m = workingSecs / 60;   long s = workingSecs % 60;
        display.printf("%02ld:%02ld:%02ld", h, m, s);
      }
      break;

    case VIEW_TODOS:
      drawOledHeader("  SCHEDULER PROFILE");
      if (totalTasksConfigured == 0) {
        display.setCursor(16, 30); display.println("No Tasks Active");
      } else {
        for (int i = 0; i < totalTasksConfigured; i++) {
          display.setCursor(4, 18 + (i * 11));
          display.printf("%d] %02d:%02d - %s", i + 1, taskList[i].targetHour, taskList[i].targetMin, taskList[i].title);
          if (i >= 3) break;
        }
      }
      break;
        
    case VIEW_DIAGNOSTICS:
      drawOledHeader("  SYSTEM INFRA");
      display.setCursor(4, 18); display.print("CPU Heap: "); display.print(ESP.getFreeHeap());
      display.setCursor(4, 29); display.print("Stations Connected: "); display.print(WiFi.softAPgetStationNum());
      display.setCursor(4, 40); display.print("Flash ID: "); display.print(ESP.getFlashChipId());
      display.setCursor(4, 51); display.print("Core Clk: "); display.print(ESP.getCpuFreqMHz()); display.print("Mhz");
      break;
  }

  display.display();
}

void handleStatusPayload() {
  char localTime[16], istTime[16], estTime[16], gmtTime[16];
  formatTimeOutput(localTime, localEpochSeconds, 0);
  formatTimeOutput(istTime, localEpochSeconds, offsetIST);
  formatTimeOutput(estTime, localEpochSeconds, offsetEST);
  formatTimeOutput(gmtTime, localEpochSeconds, offsetGMT);

  unsigned long totalChrono = chronoElapsedBase;
  if (chronoRunning) totalChrono += (millis() - chronoStartMillis) * timeMultiplier;
  unsigned long chronoSecs = (totalChrono / 1000) % 60;
  unsigned long chronoMins = (totalChrono / 60000) % 60;
  unsigned long chronoHours = (totalChrono / 3600000) % 24;
  unsigned long chronoDays = totalChrono / 86400000;

  char dateBuffer[16];
  sprintf(dateBuffer, "%04d-%02d-%02d", currentYear, currentMonth, currentDay);

  String json = "{";
  json += "\"view\":" + String((int)currentView) + ",";
  json += "\"temp\":" + String(currentTemp, 1) + ",";
  json += "\"humid\":" + String(currentHumid, 1) + ",";
  json += "\"heatIndex\":" + String(currentHeatIndex, 1) + ",";
  json += "\"dewPoint\":" + String(currentDewPoint, 1) + ",";
  json += "\"comfort\":\"" + comfortLevel + "\",";
  json += "\"airQual\":\"" + airQualityEst + "\",";
  json += "\"busVolt\":" + String(busVoltage, 2) + ",";
  json += "\"currAmps\":" + String(currentAmps, 3) + ",";
  json += "\"calcWatts\":" + String(calculatedPower, 3) + ",";
  json += "\"timeLocal\":\"" + String(localTime) + "\",";
  json += "\"timeIST\":\"" + String(istTime) + "\",";
  json += "\"timeEST\":\"" + String(estTime) + "\",";
  json += "\"timeGMT\":\"" + String(gmtTime) + "\",";
  json += "\"dateStr\":\"" + String(dateBuffer) + "\",";
  json += "\"multiplier\":" + String(timeMultiplier) + ",";
  json += "\"swDays\":" + String(chronoDays) + ",";
  json += "\"swHours\":" + String(chronoHours) + ",";
  json += "\"swMins\":" + String(chronoMins) + ",";
  json += "\"swSecs\":" + String(chronoSecs) + ",";
  json += "\"swRunning\":" + String(chronoRunning ? 1 : 0) + ",";
  json += "\"cdTotal\":" + String(countdownTotalSeconds) + ",";
  json += "\"gtTotal\":" + String(gateTimerTotalSeconds) + ",";
  json += "\"gtSpeed\":" + String(gateTimerSpeed, 1) + ",";
  json += "\"gtActive\":" + String(gateTimerActive ? 1 : 0) + ",";
  json += "\"alarmActive\":" + String(alarmActive ? 1 : 0) + ",";
  json += "\"todoCount\":" + String(totalTasksConfigured) + ",";
  json += "\"countdownRunning\":" + String(countdownRunning ? 1 : 0) + ",";
  json += "\"powerSaving\":" + String(powerSavingMode ? 1 : 0) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"wifiStations\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"appView\":" + String((int)currentView) + ",";
  json += "\"todoTasks\":[";
  for (int i = 0; i < totalTasksConfigured; i++) {
    json += "{\"t\":\"" + String(taskList[i].title) + "\",\"h\":" + String(taskList[i].targetHour) + ",\"m\":" + String(taskList[i].targetMin) + "}";
    if (i < totalTasksConfigured - 1) json += ",";
  }
  json += "],";
  for (int i = 0; i < 8; i++) {
    json += "\"p" + String(i + 1) + "\":" + String(pinStates[i]) + ",";
    json += "\"ack" + String(i + 1) + "\":" + String(pinAckState[i] ? 1 : 0) + ",";
  }
  json.remove(json.length() - 1);
  json += "}";
  server.send(200, "application/json", json);
}

void handleActionCommand() {
  String act = server.arg("action");
  if (act == "setView") {
    int t = server.arg("target").toInt();
    if (t >= VIEW_LAN && t <= VIEW_CLIENTS) {
      currentView = (ViewMode)t;
    }
    saveStateToEeprom();
  }
  else if (act == "syncDeviceTime") {
    int h = constrain(server.arg("h").toInt(), 0, 23);
    int m = constrain(server.arg("m").toInt(), 0, 59);
    int s = constrain(server.arg("s").toInt(), 0, 59);
    int y = constrain(server.arg("y").toInt(), 2000, 2099);
    int mo = constrain(server.arg("mo").toInt(), 1, 12);
    int d = constrain(server.arg("d").toInt(), 1, daysInMonth(y, mo));
    localEpochSeconds = (h * 3600) + (m * 60) + s;
    currentYear = y;
    currentMonth = mo;
    currentDay = d;
    lastTick = millis();
  }
  else if (act == "setMultiplier") {
    timeMultiplier = server.arg("val").toInt();
    if(timeMultiplier <= 0) timeMultiplier = 1;
    saveStateToEeprom();
  }
  else if (act == "toggleChrono") {
    if (chronoRunning) {
      chronoElapsedBase += (millis() - chronoStartMillis) * timeMultiplier;
      chronoRunning = false;
    } else {
      chronoStartMillis = millis();
      chronoRunning = true;
    }
    saveStateToEeprom();
  }
  else if (act == "resetChrono") {
    chronoElapsedBase = 0;
    chronoRunning = false;
    saveStateToEeprom();
  }
  else if (act == "setCountdown") {
    long d = server.arg("d").toInt();
    long h = server.arg("h").toInt();
    long m = server.arg("m").toInt();
    long s = server.arg("s").toInt();
    countdownTotalSeconds = (d * 86400) + (h * 3600) + (m * 60) + s;
    if(countdownTotalSeconds < 0) countdownTotalSeconds = 0;
    initialCountdownSeconds = countdownTotalSeconds;
    countdownRunning = false;
    saveStateToEeprom();
  }
  else if (act == "startCountdown") {
    if (countdownTotalSeconds > 0) {
      countdownRunning = true;
      countdownLastTick = millis();
    }
  }
  else if (act == "stopCountdown") {
    countdownRunning = false;
    alarmActive = false;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else if (act == "togglePowerSaver") {
    powerSavingMode = !powerSavingMode;
  }
  else if (act == "clearAlarm") {
    alarmActive = false;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    for(int i=0; i<totalTasksConfigured; i++) {
       if(taskList[i].activeTriggered) {
          taskList[i].isCompleted = true;
          taskList[i].activeTriggered = false;
       }
    }
    activeTaskDisplayStr = "Alarm Dismissed";
  }
  else if (act == "configureGateTimer") {
    long h = server.arg("h").toInt();
    long m = server.arg("m").toInt();
    long s = server.arg("s").toInt();
    gateTimerTotalSeconds = (h * 3600) + (m * 60) + s;
    initialGateTimerSeconds = gateTimerTotalSeconds;
    gateTimerSpeed = server.arg("speed").toFloat();
    gateTimerActive = true;
  }
  else if (act == "stopGateTimer") {
    gateTimerActive = false;
    alarmActive = false;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else if (act == "addTodo") {
    if (totalTasksConfigured < 4) {
      String taskTitle = server.arg("title");
      int targetH = server.arg("h").toInt();
      int targetM = server.arg("m").toInt();
      
      taskTitle.toCharArray(taskList[totalTasksConfigured].title, 22);
      taskList[totalTasksConfigured].targetHour = targetH;
      taskList[totalTasksConfigured].targetMin = targetM;
      taskList[totalTasksConfigured].isCompleted = false;
      taskList[totalTasksConfigured].activeTriggered = false;
      totalTasksConfigured++;
    }
  }
  else if (act == "testPin") {
    int pinIndex = server.arg("pin").toInt() - 1;
    if (pinIndex >= 0 && pinIndex < 8) {
      pinAckState[pinIndex] = true;
      pinAckSince[pinIndex] = millis();
      pinSignalCount[pinIndex] = 0;
    }
  }
  else if (act == "clearTodos") {
    totalTasksConfigured = 0;
    activeTaskDisplayStr = "Clean Register";
  }
  server.send(200, "text/plain", "OK");
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.2.1/app", true);
  server.send(302, "text/html", "<html><body>Redirecting to local app...</body></html>");
}

void handleRootRequest() {
  server.sendHeader("Location", "http://192.168.2.1/app", true);
  server.send(302, "text/html", "<html><body>Redirecting to local app...</body></html>");
}

void handleAppRequest() {
  static const char html[] PROGMEM = R"=====(
  <!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Core Access Hub</title>
  <style>
    :root { --bg-panel: #1b1b22; --accent-glow: #00f2fe; --card-interior: #242430; --text-main: #f5f5fa; }
    body { font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif; background: #0f0f12; color: var(--text-main); padding: 12px; margin: 0; box-sizing: border-box; }
    .container { max-width: 680px; margin: 20px auto; background: var(--bg-panel); padding: 25px; border-radius: 24px; box-shadow: 0 20px 50px rgba(0,0,0,0.6); border: 1px solid rgba(255,255,255,0.05); }
    h2 { margin: 0 0 8px 0; color: #fff; font-size: 26px; font-weight: 700; background: linear-gradient(to right, #00f2fe, #4facfe); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .subtitle { color: #6c6c84; font-size: 13px; margin-bottom: 24px; text-transform: uppercase; letter-spacing: 1px; }
    .tab-nav { display: flex; gap: 8px; margin-bottom: 25px; background: rgba(0,0,0,0.2); border-radius: 14px; padding: 6px; }
    .tab-btn { background: none; border: none; color: #8c8c9e; padding: 12px 16px; font-weight: 600; cursor: pointer; border-radius: 10px; flex: 1; transition: all 0.25s ease; font-size: 14px; }
    .tab-btn.active { background: #262636; color: var(--accent-glow); box-shadow: 0 4px 15px rgba(0,0,0,0.2); }
    .tab-content { display: none; animation: fadeIn 0.3s ease; }
    .tab-content.active { display: block; }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }
    .clock-menu { display: grid; grid-template-columns: repeat(auto-fit, minmax(90px, 1fr)); gap: 8px; margin-bottom: 25px; }
    .sub-tab-btn { background: var(--card-interior); border: 1px solid rgba(255,255,255,0.03); color: #a2a2b5; padding: 10px 6px; font-weight: 500; cursor: pointer; border-radius: 12px; transition: all 0.2s; font-size: 12px; }
    .sub-tab-btn.active { background: linear-gradient(135deg, #00f2fe, #4facfe); color: #000; font-weight: 600; box-shadow: 0 0 15px rgba(0,242,254,0.4); }
    .pair-container { background: rgba(0,0,0,0.15); border: 1px solid rgba(255,255,255,0.04); padding: 16px; margin-bottom: 20px; border-radius: 16px; }
    .pair-title { font-size: 13px; text-transform: uppercase; color: #00f2fe; margin-bottom: 14px; font-weight: 600; letter-spacing: 0.5px; }
    .grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }
    @media(max-width: 480px){ .grid { grid-template-columns: repeat(2, 1fr); } }
    .pin-card { position: relative; padding: 24px 8px 12px 8px; border-radius: 14px; font-weight: 600; font-size: 13px; border: 2px solid #323242; background: var(--card-interior); text-align: center; transition: all 0.2s; }
    .wire-stripe { position: absolute; top: 0; left: 0; right: 0; height: 6px; border-top-left-radius: 12px; border-top-right-radius: 12px; }
    .w-orange { background: linear-gradient(90deg, #fff 50%, #ff6b35 50%); background-size: 10px 6px; }
    .orange { background: #ff6b35; }
    .w-green { background: linear-gradient(90deg, #fff 50%, #4ecdc4 50%); background-size: 10px 6px; }
    .green { background: #4ecdc4; }
    .blue { background: #1a8fe3; }
    .w-blue { background: linear-gradient(90deg, #fff 50%, #1a8fe3 50%); background-size: 10px 6px; }
    .w-brown { background: linear-gradient(90deg, #fff 50%, #8b5a2b 50%); background-size: 10px 6px; }
    .brown { background: #8b5a2b; }
    .status-dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; vertical-align: middle; }
    .dot-active { background-color: #39ff14; box-shadow: 0 0 10px #39ff14; }
    .dot-inactive { background-color: #ff3333; }
    .master-btn { background: linear-gradient(135deg, #232526, #414345); border: 1px solid rgba(255,255,255,0.08); color: white; width: 100%; padding: 14px; font-weight: 600; border-radius: 14px; cursor: pointer; font-size: 14px; margin: 10px 0; transition: all 0.2s; }
    .master-btn:hover { background: rgba(255,255,255,0.05); border-color: var(--accent-glow); }
    .mini-btn { background: #11131a; border: 1px solid rgba(255,255,255,0.08); color:#fff; padding: 10px 12px; border-radius: 12px; cursor:pointer; font-size: 13px; width:100%; transition: all 0.2s ease; }
    .mini-btn:hover { border-color: var(--accent-glow); background: rgba(0,242,254,0.08); }
    .siren-btn { background: linear-gradient(90deg, #ff0844, #ffb199); border: none; color: white; width: 100%; padding: 16px; font-weight: bold; border-radius: 14px; cursor: pointer; font-size: 15px; margin-bottom: 20px; display:none; animation: pulse 0.8s infinite alternate; }
    @keyframes pulse { from { box-shadow: 0 0 4px #ff0844; } to { box-shadow: 0 0 20px #ff0844; } }
    .clock-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .clock-card { background: var(--card-interior); border-radius: 16px; padding: 16px; border: 1px solid rgba(255,255,255,0.02); text-align: left; }
    .clock-title { font-size: 11px; color: #6c6c84; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }
    .clock-time { font-size: 24px; color: var(--accent-glow); font-weight: 700; margin-top: 6px; font-family: monospace; }
    .chrono-box { background: rgba(0,0,0,0.2); border: 1px solid rgba(255,255,255,0.03); border-radius: 18px; padding: 20px; margin-bottom: 15px; }
    .chrono-digits { font-size: 38px; font-family: monospace; color: #39ff14; font-weight: 700; margin: 12px 0; letter-spacing: 1px; }
    .view-container { display: none; }
    .view-container.active { display: block; }
    .cd-input-group { display: flex; justify-content: space-around; gap: 8px; margin: 15px 0; }
    .cd-field { background: var(--card-interior); border: 1px solid #323242; color: #fff; padding: 10px; border-radius: 10px; text-align: center; font-size: 15px; flex: 1; min-width: 0; outline: none; }
    .cd-field:focus { border-color: var(--accent-glow); }
    .metric-row { display: flex; justify-content: space-between; padding: 12px 0; border-bottom: 1px solid rgba(255,255,255,0.04); font-size: 14px; }
    .todo-list-wrapper { text-align: left; background: var(--card-interior); border-radius: 12px; padding: 12px; margin-top: 15px; }
    .todo-item { display: flex; justify-content: space-between; padding: 10px 6px; border-bottom: 1px solid rgba(255,255,255,0.05); color: #ffea00; font-weight: 600; font-size: 14px; }
    .todo-item:last-child { border: none; }
  </style>
  <script>
    let activeTabId = 0;
    let clockSubView = 1;
    function navigateToTab(tabIndex) {
      activeTabId = tabIndex;
      let targetView = (tabIndex === 0) ? 0 : clockSubView;
      fetch('/action?action=setView&target=' + targetView).then(() => {
        document.querySelectorAll('.tab-btn').forEach((b, idx) => b.classList.toggle('active', idx === tabIndex));
        document.querySelectorAll('.tab-content').forEach((c, idx) => c.classList.toggle('active', idx === tabIndex));
        if(tabIndex === 1) selectClockView(clockSubView);
      });
    }

    function selectClockView(viewId) {
      clockSubView = viewId;
      fetch('/action?action=setView&target=' + viewId).then(() => {
        document.querySelectorAll('.sub-tab-btn').forEach((b) => {
          let onclickAttr = b.getAttribute('onclick');
          b.classList.toggle('active', onclickAttr.includes('(' + viewId + ')'));
        });
        document.querySelectorAll('.view-container').forEach((v) => {
          let vId = parseInt(v.getAttribute('data-view'));
          v.classList.toggle('active', vId === viewId);
        });
        if(viewId === 4) sendBrowserSyncTime();
      });
    }

    function sendBrowserSyncTime() {
      let d = new Date();
      fetch(`/action?action=syncDeviceTime&h=${d.getHours()}&m=${d.getMinutes()}&s=${d.getSeconds()}&y=${d.getFullYear()}&mo=${d.getMonth()+1}&d=${d.getDate()}`);
    }

    function setManualTime() {
      let h = document.getElementById('manual-h').value || 0;
      let m = document.getElementById('manual-m').value || 0;
      let s = document.getElementById('manual-s').value || 0;
      let y = document.getElementById('manual-y').value || new Date().getFullYear();
      let mo = document.getElementById('manual-mo').value || new Date().getMonth() + 1;
      let d = document.getElementById('manual-d').value || new Date().getDate();
      fetch(`/action?action=syncDeviceTime&h=${h}&m=${m}&s=${s}&y=${y}&mo=${mo}&d=${d}`);
    }

    function submitCountdown() {
      let d = document.getElementById('cd-d').value || 0;
      let h = document.getElementById('cd-h').value || 0;
      let m = document.getElementById('cd-m').value || 0;
      let s = document.getElementById('cd-s').value || 0;
      fetch(`/action?action=setCountdown&d=${d}&h=${h}&m=${m}&s=${s}`);
    }

    function startGateTimer() {
      let h = document.getElementById('gt-h').value || 0;
      let m = document.getElementById('gt-m').value || 0;
      let s = document.getElementById('gt-s').value || 0;
      let speed = document.getElementById('gt-speed').value;
      fetch(`/action?action=configureGateTimer&h=${h}&m=${m}&s=${s}&speed=${speed}`);
    }

    function stopGateTimer() { fetch('/action?action=stopGateTimer'); }
    function pushTodoItem() {
      let t = document.getElementById('todo-title').value;
      let h = document.getElementById('todo-h').value;
      let m = document.getElementById('todo-m').value;
      if(!t) return alert("Enter task title");
      fetch(`/action?action=addTodo&title=${encodeURIComponent(t)}&h=${h}&m=${m}`).then(() => {
         document.getElementById('todo-title').value = '';
      });
    }

    function resetTodoCache() { fetch('/action?action=clearTodos'); }
    function silenceSiren() { fetch('/action?action=clearAlarm'); }
    function testPin(pin) { fetch('/action?action=testPin&pin=' + pin); }
    function startCountdown() { fetch('/action?action=startCountdown'); }
    function stopCountdown() { fetch('/action?action=stopCountdown'); }
    function togglePowerSaver() { fetch('/action?action=togglePowerSaver'); }

    setInterval(() => {
      fetch('/status').then(r => r.json()).then(data => {
        document.getElementById('master-siren-btn').style.display = data.alarmActive === 1 ? 'block' : 'none';

        if(activeTabId === 0) {
          for(let i=1; i<=8; i++) {
            let card = document.getElementById('p-card-' + i);
            let dot = document.getElementById('p-dot-' + i);
            let text = document.getElementById('p-text-' + i);
            let pinValue = data['p'+i];
            let ack = data['ack'+i] === 1;
            if(card) {
              card.style.borderColor = pinValue === 1 ? '#39ff14' : '#323242';
            }
            if(dot) {
              dot.className = "status-dot " + (pinValue === 1 ? "dot-active" : "dot-inactive");
            }
            if(text) {
              text.innerText = ack ? 'ACK' : (pinValue === 1 ? 'ON' : 'OFF');
              text.style.color = ack ? '#39ff14' : '#a2a2b5';
            }
          }
        } else {
          if(document.getElementById('display-ist')) document.getElementById('display-ist').innerText = data.timeIST;
          if(document.getElementById('display-est')) document.getElementById('display-est').innerText = data.timeEST;
          if(document.getElementById('display-gmt')) document.getElementById('display-gmt').innerText = data.timeGMT;
          if(document.getElementById('display-local')) document.getElementById('display-local').innerText = data.timeLocal;
          if(document.getElementById('sync-current-time')) document.getElementById('sync-current-time').innerText = data.timeLocal;
          if(document.getElementById('sync-current-date')) document.getElementById('sync-current-date').innerText = data.dateStr;
          if(document.getElementById('web-date-node')) document.getElementById('web-date-node').innerText = "System Registry Date: " + data.dateStr;
          
          if(document.getElementById('env-temp')) document.getElementById('env-temp').innerText = data.temp + " °C";
          if(document.getElementById('env-humid')) document.getElementById('env-humid').innerText = data.humid + " %";
          if(document.getElementById('env-hi')) document.getElementById('env-hi').innerText = data.heatIndex + " °C";
          if(document.getElementById('env-dp')) document.getElementById('env-dp').innerText = data.dewPoint + " °C";
          if(document.getElementById('env-comfort')) document.getElementById('env-comfort').innerText = data.comfort;
          if(document.getElementById('env-aqi')) document.getElementById('env-aqi').innerText = data.airQual;

          if(document.getElementById('pwr-volt')) document.getElementById('pwr-volt').innerText = data.busVolt + " V";
          if(document.getElementById('pwr-amps')) document.getElementById('pwr-amps').innerText = (data.currAmps * 1000).toFixed(0) + " mA";
          if(document.getElementById('pwr-watts')) document.getElementById('pwr-watts').innerText = data.calcWatts + " W";

          if(document.getElementById('diag-heap')) document.getElementById('diag-heap').innerText = data.freeHeap + " Bytes";
          if(document.getElementById('diag-stations')) document.getElementById('diag-stations').innerText = data.wifiStations;
          if(document.getElementById('pwr-save')) document.getElementById('pwr-save').innerText = data.powerSaving === 1 ? 'ON' : 'OFF';
          if(document.getElementById('pwr-clients')) document.getElementById('pwr-clients').innerText = data.wifiStations;
          if(document.getElementById('pwr-load')) document.getElementById('pwr-load').innerText = data.calcWatts + ' W';
          if(document.getElementById('client-count')) document.getElementById('client-count').innerText = data.wifiStations;

          let gSecs = data.gtTotal;
          let gh = Math.floor(gSecs / 3600); gSecs %= 3600;
          let gm = Math.floor(gSecs / 60);   let gs = gSecs % 60;
          if(document.getElementById('gate-timer-val')) {
            document.getElementById('gate-timer-val').innerText = `${gh.toString().padStart(2,'0')}:${gm.toString().padStart(2,'0')}:${gs.toString().padStart(2,'0')}`;
            document.getElementById('gate-running-msg').innerText = data.gtActive === 1 ? `Accelerating at ${data.gtSpeed}x` : "Clock Loop Engine Nominal";
          }

          let todoWrapper = document.getElementById('todo-container-element');
          if(todoWrapper) {
            todoWrapper.innerHTML = '';
            if(data.todoTasks.length === 0) {
               todoWrapper.innerHTML = '<div style="color:#6c6c84; text-align:center; padding:10px; font-size:13px;">No alerts in storage register</div>';
            } else {
               data.todoTasks.forEach(task => {
                  todoWrapper.innerHTML += `<div class="todo-item"><div style="font-size:16px; font-weight:700; margin-bottom:6px;">${task.t}</div><div style="font-size:12px; color:#a2a2b5;">${task.h.toString().padStart(2,'0')} : ${task.m.toString().padStart(2,'0')}</div></div>`;
               });
            }
          }

          let rem = data.cdTotal;
          if(document.getElementById('cd-val')) {
            if (rem <= 0) {
              document.getElementById('cd-val').innerText = "FINISHED";
            } else {
              let d = Math.floor(rem / 86400); rem %= 86400;
              let h = Math.floor(rem / 3600);  rem %= 3600;
              let m = Math.floor(rem / 60);    let s = rem % 60;
              document.getElementById('cd-val').innerText = `${d}d ${h.toString().padStart(2,'0')}:${m.toString().padStart(2,'0')}:${s.toString().padStart(2,'0')}`;
            }
          }
        }
      });
    }, 600);
  </script></head>
  <body>
    <div class="container">
      <h2>Yash Smart Hub Pro</h2>
      <div class="subtitle">Distributed Diagnostics & Automation Console</div>
      <button id="master-siren-btn" class="siren-btn" onclick="silenceSiren()">⚠️ DISMISS ACTIVE SIREN ALARM ⚠️</button>
      <div style="display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:16px;">
        <button class="mini-btn" onclick="sendBrowserSyncTime()">Sync Time</button>
        <button class="mini-btn" onclick="silenceSiren()">Dismiss Alarm</button>
        <button class="mini-btn" onclick="resetTodoCache()">Clear Tasks</button>
        <button class="mini-btn" onclick="location.href='/terminal'">Terminal</button>
      </div>

      <div class="tab-nav">
        <button class="tab-btn active" onclick="navigateToTab(0)">LAN Core Tester</button>
        <button class="tab-btn" onclick="navigateToTab(1)">Productivity Suite</button>
      </div>

      <div id="tab-lan" class="tab-content active">
        <div class="pair-container">
          <div class="pair-title">Network Sequence Vector (Lines 1 - 4)</div>
          <div class="grid">
            <div class="pin-card" id="p-card-1"><div class="wire-stripe w-orange"></div><div style="margin-bottom:8px;"><span id="p-dot-1" class="status-dot dot-inactive"></span>Pin 1</div><div id="p-text-1" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(1)">Test Pin 1</button></div>
            <div class="pin-card" id="p-card-2"><div class="wire-stripe orange"></div><div style="margin-bottom:8px;"><span id="p-dot-2" class="status-dot dot-inactive"></span>Pin 2</div><div id="p-text-2" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(2)">Test Pin 2</button></div>
            <div class="pin-card" id="p-card-3"><div class="wire-stripe w-green"></div><div style="margin-bottom:8px;"><span id="p-dot-3" class="status-dot dot-inactive"></span>Pin 3</div><div id="p-text-3" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(3)">Test Pin 3</button></div>
            <div class="pin-card" id="p-card-4"><div class="wire-stripe blue"></div><div style="margin-bottom:8px;"><span id="p-dot-4" class="status-dot dot-inactive"></span>Pin 4</div><div id="p-text-4" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(4)">Test Pin 4</button></div>
          </div>
        </div>
        <div class="pair-container">
          <div class="pair-title">Network Sequence Vector (Lines 5 - 8)</div>
          <div class="grid">
            <div class="pin-card" id="p-card-5"><div class="wire-stripe w-blue"></div><div style="margin-bottom:8px;"><span id="p-dot-5" class="status-dot dot-inactive"></span>Pin 5</div><div id="p-text-5" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(5)">Test Pin 5</button></div>
            <div class="pin-card" id="p-card-6"><div class="wire-stripe green"></div><div style="margin-bottom:8px;"><span id="p-dot-6" class="status-dot dot-inactive"></span>Pin 6</div><div id="p-text-6" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(6)">Test Pin 6</button></div>
            <div class="pin-card" id="p-card-7"><div class="wire-stripe w-brown"></div><div style="margin-bottom:8px;"><span id="p-dot-7" class="status-dot dot-inactive"></span>Pin 7</div><div id="p-text-7" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(7)">Test Pin 7</button></div>
            <div class="pin-card" id="p-card-8"><div class="wire-stripe brown"></div><div style="margin-bottom:8px;"><span id="p-dot-8" class="status-dot dot-inactive"></span>Pin 8</div><div id="p-text-8" style="font-size:12px;color:#a2a2b5;margin-bottom:8px;">OFF</div><button class="mini-btn" onclick="testPin(8)">Test Pin 8</button></div>
          </div>
        </div>
      </div>

      <div id="tab-clock" class="tab-content">
        <div class="clock-menu">
          <button class="sub-tab-btn active" onclick="selectClockView(1)">World Time</button>
          <button class="sub-tab-btn" onclick="selectClockView(6)">Gate Multiplier</button>
          <button class="sub-tab-btn" onclick="selectClockView(7)">Todo List</button>
          <button class="sub-tab-btn" onclick="selectClockView(3)">Sensors</button>
          <button class="sub-tab-btn" onclick="selectClockView(4)">Time Sync</button>
          <button class="sub-tab-btn" onclick="selectClockView(9)">Power Saver</button>
          <button class="sub-tab-btn" onclick="selectClockView(10)">Clients</button>
          <button class="sub-tab-btn" onclick="selectClockView(5)">Countdown</button>
          <button class="sub-tab-btn" onclick="selectClockView(8)">Diagnostics</button>
        </div>

        <div data-view="1" class="view-container active">
          <div class="clock-grid">
            <div class="clock-card"><div class="clock-title">Mumbai, India (IST)</div><div class="clock-time" id="display-ist">00:00:00</div></div>
            <div class="clock-card"><div class="clock-title">New York, USA (EST)</div><div class="clock-time" id="display-est">00:00:00</div></div>
            <div class="clock-card"><div class="clock-title">London, UK (GMT)</div><div class="clock-time" id="display-gmt">00:00:00</div></div>
            <div class="clock-card"><div class="clock-title">Local Hub Baseline</div><div class="clock-time" id="display-local">00:00:00</div></div>
          </div>
          <div class="clock-card" style="margin-top:12px;">
             <div class="clock-title" id="web-date-node">System Registry Date: ----/--/--</div>
          </div>
        </div>

        <div data-view="6" class="view-container">
          <div class="chrono-box">
            <div class="pair-title" style="text-align:center;">Dynamic Internal Clock Modulator</div>
            <div class="chrono-digits" id="gate-timer-val" style="color:#00f2fe; text-align:center;">00:00:00</div>
            <div id="gate-running-msg" style="font-size:13px; font-weight:600; color:#6c6c84; margin-bottom:15px; text-align:center;">Clock Loop Engine Nominal</div>
            <div class="cd-input-group">
              <input type="number" id="gt-h" class="cd-field" placeholder="Hours" min="0" max="23">
              <input type="number" id="gt-m" class="cd-field" placeholder="Minutes" min="0" max="59">
              <input type="number" id="gt-s" class="cd-field" placeholder="Seconds" min="0" max="59">
            </div>
            <div class="cd-input-group">
               <select id="gt-speed" class="cd-field" style="width:100%; height:40px;">
                  <option value="0.5">0.5x (Asynchronous Debug Training)</option>
                  <option value="1.0" selected>1.0x (Nominal Day Counter)</option>
                  <option value="1.5">1.5x (Accelerated Track)</option>
                  <option value="2.0">2.0x (Double-Speed Time Warp)</option>
               </select>
            </div>
            <button class="master-btn" style="background: linear-gradient(135deg, #00f2fe, #4facfe); color:#000;" onclick="startGateTimer()">Commit Vector Acceleration</button>
            <button class="master-btn" onclick="stopGateTimer()">Halt Warp Loop</button>
          </div>
        </div>

        <div data-view="7" class="view-container">
          <div class="chrono-box">
            <div class="pair-title" style="text-align:center;">Task Schedulers</div>
            <div class="cd-input-group" style="margin-bottom:10px;">
               <input type="text" id="todo-title" class="cd-field" style="min-width:40%;" placeholder="Task Label">
               <input type="number" id="todo-h" class="cd-field" placeholder="Hour" min="0" max="23">
               <input type="number" id="todo-m" class="cd-field" placeholder="Min" min="0" max="59">
            </div>
            <button class="master-btn" style="background:linear-gradient(135deg, #39ff14, #00adb5); color:#000;" onclick="pushTodoItem()">Save Task to Register</button>
            <div class="todo-list-wrapper">
               <div class="pair-title" style="color:#6c6c84; font-size:11px;">Active Schedule Stack</div>
               <div id="todo-container-element"></div>
            </div>
            <button class="master-btn" style="font-size:12px; padding:8px;" onclick="resetTodoCache()">Purge Active Register</button>
          </div>
        </div>

        <div data-view="3" class="view-container">
          <div class="chrono-box" style="text-align:left; margin-bottom: 12px;">
            <div class="pair-title">Atmospheric Environment Matrix</div>
            <div class="metric-row"><span>Ambient Temperature:</span><b id="env-temp" style="color:#ff6b35;">--</b></div>
            <div class="metric-row"><span>Relative Humidity:</span><b id="env-humid" style="color:#4ecdc4;">--</b></div>
            <div class="metric-row"><span>Sensory Heat Index:</span><b id="env-hi" style="color:#ff3333;">--</b></div>
            <div class="metric-row"><span>Computed Dew Point:</span><b id="env-dp" style="color:#1a8fe3;">--</b></div>
            <div class="metric-row"><span>Comfort Index Classification:</span><b id="env-comfort" style="color:#00f2fe;">--</b></div>
            <div class="metric-row"><span>Air Stability Index:</span><b id="env-aqi" style="color:#39ff14;">--</b></div>
          </div>
        </div>

        <div data-view="4" class="view-container">
          <div class="chrono-box" style="text-align:left;">
            <div class="pair-title">Time Sync Portal</div>
            <div class="metric-row"><span>Current Hub Time:</span><b id="sync-current-time" style="color:#00f2fe;">--:--:--</b></div>
            <div class="metric-row"><span>Current Hub Date:</span><b id="sync-current-date" style="color:#39ff14;">----/--/--</b></div>
            <div class="cd-input-group">
              <input type="number" id="manual-h" class="cd-field" placeholder="Hour" min="0" max="23">
              <input type="number" id="manual-m" class="cd-field" placeholder="Min" min="0" max="59">
              <input type="number" id="manual-s" class="cd-field" placeholder="Sec" min="0" max="59">
            </div>
            <div class="cd-input-group">
              <input type="number" id="manual-y" class="cd-field" placeholder="Year" min="2000" max="2099">
              <input type="number" id="manual-mo" class="cd-field" placeholder="Month" min="1" max="12">
              <input type="number" id="manual-d" class="cd-field" placeholder="Day" min="1" max="31">
            </div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;">
              <button class="master-btn" style="background: linear-gradient(135deg, #00f2fe, #4facfe); color:#000;" onclick="sendBrowserSyncTime()">Auto Sync Browser</button>
              <button class="master-btn" style="background: linear-gradient(135deg, #39ff14, #00adb5); color:#000;" onclick="setManualTime()">Set Manual Time</button>
            </div>
          </div>
        </div>

        <div data-view="9" class="view-container">
          <div class="chrono-box" style="text-align:left;">
            <div class="pair-title">Battery Power Shaving Mode</div>
            <div class="metric-row"><span>Power Saver Status:</span><b id="pwr-save" style="color:#00f2fe;">--</b></div>
            <div class="metric-row"><span>Connected Clients:</span><b id="pwr-clients" style="color:#39ff14;">0</b></div>
            <div class="metric-row"><span>System Load Estimate:</span><b id="pwr-load" style="color:#ffea00;">0 W</b></div>
            <button class="master-btn" style="background: linear-gradient(135deg, #ff6b35, #ffcc33); color:#000;" onclick="togglePowerSaver()">Toggle Power Saving Mode</button>
          </div>
        </div>

        <div data-view="10" class="view-container">
          <div class="chrono-box" style="text-align:left;">
            <div class="pair-title">Connected Device Monitor</div>
            <div class="metric-row"><span>Users Joined:</span><b id="client-count" style="color:#39ff14;">0</b></div>
            <div class="metric-row"><span>Access SSID:</span><b style="color:#00f2fe;">Yash_Smart_Hub</b></div>
            <div class="metric-row"><span>Gateway IP:</span><b style="color:#ff6b35;">192.168.2.1</b></div>
          </div>
        </div>

        <div data-view="5" class="view-container">
          <div class="chrono-box">
            <div class="chrono-digits" id="cd-val" style="color:#ff3333; text-align:center;">0d 00:00:00</div>
            <div class="cd-input-group">
              <input type="number" id="cd-d" class="cd-field" placeholder="Days" min="0">
              <input type="number" id="cd-h" class="cd-field" placeholder="Hours" min="0" max="23">
              <input type="number" id="cd-m" class="cd-field" placeholder="Mins" min="0" max="59">
              <input type="number" id="cd-s" class="cd-field" placeholder="Secs" min="0" max="59">
            </div>
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;">
              <button class="master-btn" style="background: linear-gradient(135deg, #ff3333, #ff8585); color:#000;" onclick="submitCountdown()">Set Timer</button>
              <button class="master-btn" style="background: linear-gradient(135deg, #39ff14, #00adb5); color:#000;" onclick="startCountdown()">Start Timer</button>
            </div>
            <button class="master-btn" style="margin-top:10px; background: linear-gradient(135deg, #555, #222); color:#fff;" onclick="stopCountdown()">Stop Timer</button>
          </div>
        </div>
        
        <div data-view="8" class="view-container">
          <div class="chrono-box" style="text-align:left;">
            <div class="pair-title">Low-Level SOC Diagnostics</div>
            <div class="metric-row"><span>Free RAM Stack Heap:</span><b id="diag-heap" style="color:#00f2fe;">-- Bytes</b></div>
            <div class="metric-row"><span>SoftAP Connected Nodes:</span><b id="diag-stations" style="color:#39ff14;">0</b></div>
            <div class="metric-row"><span>Operating Allocation Limit:</span><b style="color:#aaa;">4194304 Bytes (4MB)</b></div>
          </div>
        </div>

      </div>
    </div>
  </body></html>
  )=====";
  server.send_P(200, "text/html", html);
}

void handleTerminalRequest() {
  static const char html[] PROGMEM = R"=====(
  <!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Web Terminal</title>
  <style>
    body { margin: 0; padding: 18px; font-family: Arial, Helvetica, sans-serif; background: #080b15; color: #eff2ff; }
    .terminal { max-width: 760px; margin: 0 auto; background: #101626; border: 1px solid rgba(255,255,255,0.06); border-radius: 16px; padding: 20px; box-shadow: 0 20px 40px rgba(0,0,0,0.45); }
    .terminal h1 { margin: 0 0 12px; font-size: 20px; color: #4fc3f7; }
    .output { min-height: 240px; background: #0c1321; border: 1px solid rgba(79,195,247,0.18); border-radius: 14px; padding: 14px; font-family: monospace; font-size: 14px; line-height: 1.5; overflow-y: auto; white-space: pre-wrap; }
    .input-row { display: flex; gap: 10px; margin-top: 14px; }
    .cmd-input { flex: 1; padding: 12px 14px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.08); background: #0d1526; color: #eff2ff; outline: none; font-size: 14px; }
    .cmd-btn { background: #4fc3f7; color: #081220; border: none; border-radius: 12px; padding: 12px 20px; font-weight: 700; cursor: pointer; }
    .cmd-btn:hover { background: #2a9ad8; }
    .hint { margin-top: 12px; color: #94a3b8; font-size: 13px; }
    a { color: #4fc3f7; text-decoration: none; }
  </style>
  </head><body>
  <div class="terminal">
    <h1>ESP8266 Web Terminal</h1>
    <div class="output" id="output">Welcome to the ESP8266 shell. Type help and press Enter.</div>
    <div class="input-row">
      <input id="cmd" class="cmd-input" type="text" placeholder="enter command" autocomplete="off" />
      <button class="cmd-btn" onclick="sendCmd()">Send</button>
    </div>
    <div class="hint">Commands: help, status, pins, clear, reboot</div>
    <div style="margin-top:14px;"><a href="/app">Back to App</a></div>
  </div>
  <script>
    const output = document.getElementById('output');
    const input = document.getElementById('cmd');
    function appendLine(text) { output.innerText += '\n' + text; output.scrollTop = output.scrollHeight; }
    function sendCmd() {
      let cmd = input.value.trim();
      if (!cmd) return;
      appendLine('> ' + cmd);
      fetch('/cmd?cmd=' + encodeURIComponent(cmd)).then(r => r.text()).then(t => {
        appendLine(t);
        if (cmd.toLowerCase() !== 'clear') input.value = '';
      }).catch(e => appendLine('ERROR: ' + e));
    }
    input.addEventListener('keydown', e => { if (e.key === 'Enter') sendCmd(); });
  </script>
  </body></html>
  )=====";
  server.send_P(200, "text/html", html);
}

void handleCmdRequest() {
  String cmd = server.arg("cmd");
  String response;

  if (cmd.length() == 0) {
    response = "No command received.";
  } else if (cmd.equalsIgnoreCase("help")) {
    response = "help | status | pins | clear | reboot";
  } else if (cmd.equalsIgnoreCase("status")) {
    response = "AP SSID=" + String(ssid) + " IP=" + WiFi.softAPIP().toString() + " FreeHeap=" + String(ESP.getFreeHeap());
  } else if (cmd.equalsIgnoreCase("pins")) {
    response = "Pins:";
    for (int i = 0; i < 8; i++) {
      response += " P" + String(i + 1) + ":" + (pinStates[i] ? "ON" : "OFF");
      if (pinAckState[i]) response += "(ACK)";
      if (i < 7) response += ",";
    }
  } else if (cmd.equalsIgnoreCase("clear")) {
    response = "Terminal cleared.";
  } else if (cmd.equalsIgnoreCase("reboot")) {
    response = "Rebooting device...";
    server.send(200, "text/plain", response);
    delay(100);
    ESP.restart();
    return;
  } else {
    response = "Unknown command: " + cmd;
  }

  server.send(200, "text/plain", response);
}