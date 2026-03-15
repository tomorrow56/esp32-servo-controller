// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tomorrow56
// https://github.com/tomorrow56/esp32-servo-controller

#include <WiFi.h>
#include <SimpleWiFiManager.h>
#include <WebServer.h>
#include <ESP32FwUploader.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ===== OTA設定 =====
static const char* OTA_USERNAME = "admin";
static const char* OTA_PASSWORD = "password123";
static const uint16_t OTA_PORT  = 8080;
WebServer otaServer(OTA_PORT);

// ===== OLED設定 (SSD1306 128x32) =====
#define OLED_SDA     21
#define OLED_SCL     22
#define OLED_ADDR    0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET   -1

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledAvailable = false;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ===== Wi-Fi設定（WiFiManagerで自動設定） =====
// 接続失敗時: AP "ESP32-ServoAP" が起動 → 192.168.4.1 でSSID/パスワードを設定

// ===== サーボ設定 =====
const int numServos = 10;
const int servoPins[numServos] = {23, 19, 18, 5, 17, 16, 4, 27, 14, 12};
Servo servos[numServos];
int servoAngles[numServos];

// ===== スクリプト実行設定 =====
String currentScript = "";
volatile bool isExecuting = false;
volatile int currentLine = 0;
int totalLines = 0;
TaskHandle_t scriptTaskHandle = NULL;

// ===== Webサーバー設定 =====
WebServer apiServer(80);

// ===== OLED表示ヘルパー =====
void oledPrint(const String &line1, const String &line2 = "", const String &line3 = "") {
  if (!oledAvailable) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);  oled.println(line1);
  oled.setCursor(0, 11); oled.println(line2);
  oled.setCursor(0, 22); oled.println(line3);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  // ===== OLED初期化 =====
  Wire.begin(OLED_SDA, OLED_SCL);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledAvailable = true;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("ESP32 ServoAPI");
    oled.println("Initializing...");
    oled.display();
  } else {
    Serial.println("OLED not found");
  }

  // ===== サーボ初期化 =====
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  for (int i = 0; i < numServos; i++) {
    servos[i].attach(servoPins[i], 500, 2500);
    servoAngles[i] = 90;
    servos[i].write(servoAngles[i]);
    delay(20);
  }
  Serial.println("Servos initialized.");
  oledPrint("ESP32 ServoAPI", "Servos OK", "WiFi connecting...");

  // ===== SimpleWiFiManager =====
  SimpleWiFiManager wm;
  wm.setConfigPortalTimeout(5);
  wm.setAPCallback([](SimpleWiFiManager *wm) {
    Serial.println("Config portal started");
    oledPrint("WiFi Setup AP", "ESP32-ServoAP", "192.168.4.1");
    digitalWrite(LED_BUILTIN, HIGH);
  });

  if (!wm.autoConnect("ESP32-ServoAP")) {
    Serial.println("WiFi connection failed. Restarting...");
    oledPrint("WiFi Failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  Serial.println("WiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_BUILTIN, LOW);
  oledPrint("ESP32 ServoAPI", WiFi.localIP().toString(), "Port: 80");

  // ===== ESP32FwUploader (OTA) =====
  ESP32FwUploader.setDebug(true);
  ESP32FwUploader.setDarkMode(false);
  ESP32FwUploader.setAuth(OTA_USERNAME, OTA_PASSWORD);
  ESP32FwUploader.setAutoReboot(true);

  ESP32FwUploader.onStart([]() {
    Serial.println("[OTA] Started");
    oledPrint("OTA Update", "Starting...");
    digitalWrite(LED_BUILTIN, HIGH);
  });
  ESP32FwUploader.onProgress([](size_t current, size_t total) {
    if (total > 0) {
      int pct = (int)((float)current / (float)total * 100.0f);
      Serial.printf("[OTA] Progress: %d%% (%u/%u bytes)\n", pct, (unsigned)current, (unsigned)total);
      oledPrint("OTA Update", String(pct) + "%");
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  });
  ESP32FwUploader.onEnd([](bool success) {
    Serial.printf("[OTA] End: %s\n", success ? "SUCCESS" : "FAILED");
    if (success) {
      oledPrint("OTA Update", "Complete!", "Rebooting...");
    } else {
      oledPrint("OTA Failed", ESP32FwUploader.getLastErrorMessage());
    }
    digitalWrite(LED_BUILTIN, LOW);
  });
  ESP32FwUploader.onError([](ESP32Fw_Error error, const String& message) {
    Serial.printf("[OTA] Error %d: %s\n", error, message.c_str());
    oledPrint("OTA Error", message);
  });

  ESP32FwUploader.begin(&otaServer);
  otaServer.begin();
  Serial.printf("OTA server started on port %u\n", OTA_PORT);
  Serial.printf("OTA URL: http://%s:%u/update\n", WiFi.localIP().toString().c_str(), OTA_PORT);
  Serial.printf("OTA Username: %s\n", OTA_USERNAME);
  Serial.printf("OTA Password: %s\n", OTA_PASSWORD);

  // ===== HTTPサーバー起動 =====
  apiServer.on("/", HTTP_GET, []() {
    apiServer.sendHeader("Access-Control-Allow-Origin", "*");
    apiServer.send(200, "text/plain", "ESP32 Servo Web API");
  });
  apiServer.on("/api/servos", HTTP_GET, handleGetServosStatus);
  apiServer.on("/api/servos", HTTP_POST, handleMultiServoControl);
  apiServer.on("/api/script/upload", HTTP_POST, handleScriptUpload);
  apiServer.on("/api/script/execute", HTTP_POST, handleScriptExecute);
  apiServer.on("/api/script/stop", HTTP_POST, handleScriptStop);
  apiServer.on("/api/script/status", HTTP_GET, handleScriptStatus);
  apiServer.onNotFound([]() {
    apiServer.sendHeader("Access-Control-Allow-Origin", "*");
    if (apiServer.method() == HTTP_OPTIONS) {
      apiServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      apiServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      apiServer.send(204);
    } else if (apiServer.uri().startsWith("/api/servo/")) {
      handleServoControl();
    } else {
      apiServer.send(404, "application/json", "{\"error\":\"Not Found\"}");
    }
  });
  apiServer.begin();
  Serial.println("Server started.");
  Serial.printf("URL: http://%s\n", WiFi.localIP().toString().c_str());
  oledPrint("Ready!", WiFi.localIP().toString(), "OTA port:" + String(OTA_PORT));
}

void loop() {
  otaServer.handleClient();
  ESP32FwUploader.loop();
  apiServer.handleClient();
}


void sendJSON(int code, const String &body) {
  apiServer.sendHeader("Access-Control-Allow-Origin", "*");
  apiServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  apiServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  apiServer.send(code, "application/json", body);
}

void handleScriptUpload() {
  String body = apiServer.arg("plain");
  Serial.print("Upload body: "); Serial.println(body);
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error || !doc.containsKey("script")) {
    sendJSON(400, "{\"error\":\"Invalid request\"}");
    return;
  }

  currentScript = doc["script"].as<String>();
  totalLines = (currentScript.length() == 0) ? 0 : 1;
  for (int i = 0; i < (int)currentScript.length(); i++) {
    if (currentScript.charAt(i) == '\n') totalLines++;
  }

  String response = "{\"status\":\"success\",\"script_id\":\"script_001\",\"lines\":" + String(totalLines) + "}";
  sendJSON(200, response);
  Serial.println("Script uploaded:");
  Serial.println(currentScript);
}

void handleScriptExecute() {
  Serial.println("handleScriptExecute called");
  if (currentScript.length() == 0) {
    sendJSON(400, "{\"error\":\"No script uploaded\"}");
    return;
  }
  if (isExecuting) {
    sendJSON(400, "{\"error\":\"Script already running\"}");
    return;
  }
  isExecuting = true;
  currentLine = 0;
  xTaskCreatePinnedToCore(
    scriptExecutionTask,
    "ScriptTask",
    8192,
    NULL,
    1,
    &scriptTaskHandle,
    1
  );
  sendJSON(200, "{\"status\":\"running\",\"execution_id\":\"exec_001\"}");
}

void handleScriptStop() {
  if (scriptTaskHandle != NULL) {
    vTaskDelete(scriptTaskHandle);
    scriptTaskHandle = NULL;
  }
  isExecuting = false;
  sendJSON(200, "{\"status\":\"stopped\"}");
}

void handleScriptStatus() {
  String status = isExecuting ? "running" : "idle";
  String response = "{\"status\":\"" + status + "\",\"current_line\":" + String(currentLine) + ",\"total_lines\":" + String(totalLines) + "}";
  sendJSON(200, response);
}

// ===== 条件評価ヘルパー =====
bool evaluateCondition(String args) {
  // 書式: servo<ch> <op> <angle>
  // 例: servo0 >= 90
  int sp1 = args.indexOf(' ');
  if (sp1 < 0) return false;
  String lhs = args.substring(0, sp1);
  lhs.trim();

  String rest = args.substring(sp1 + 1);
  rest.trim();

  // 演算子を抽出
  String op = "";
  int valStart = 0;
  if (rest.startsWith(">=")) { op = ">="; valStart = 2; }
  else if (rest.startsWith("<=")) { op = "<="; valStart = 2; }
  else if (rest.startsWith("!=")) { op = "!="; valStart = 2; }
  else if (rest.startsWith("==")) { op = "=="; valStart = 2; }
  else if (rest.startsWith(">"))  { op = ">";  valStart = 1; }
  else if (rest.startsWith("<"))  { op = "<";  valStart = 1; }
  else if (rest.startsWith("="))  { op = "=="; valStart = 1; }
  else return false;

  int rhs = rest.substring(valStart).toInt();

  // 左辺の値を取得
  int lhsVal = 0;
  lhs.toLowerCase();
  if (lhs.startsWith("servo")) {
    int ch = lhs.substring(5).toInt();
    if (ch >= 0 && ch < numServos) lhsVal = servoAngles[ch];
  }

  if (op == "==") return lhsVal == rhs;
  if (op == "!=") return lhsVal != rhs;
  if (op == ">")  return lhsVal >  rhs;
  if (op == ">=") return lhsVal >= rhs;
  if (op == "<")  return lhsVal <  rhs;
  if (op == "<=") return lhsVal <= rhs;
  return false;
}

void scriptExecutionTask(void *parameter) {
  Serial.println("Script execution started");

  // スクリプトを行配列に分割
  const int MAX_LINES = 512;
  String lines[MAX_LINES];
  int lineCount = 0;

  int lineStart = 0;
  while (lineStart < (int)currentScript.length() && lineCount < MAX_LINES) {
    int lineEnd = currentScript.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = currentScript.length();
    String ln = currentScript.substring(lineStart, lineEnd);
    ln.trim();
    lines[lineCount++] = ln;
    lineStart = lineEnd + 1;
  }
  totalLines = lineCount;
  currentLine = 0;

  // if/else/endif スタック（最大ネスト8段）
  const int MAX_NEST = 8;
  bool execStack[MAX_NEST];
  bool inElseStack[MAX_NEST];
  int nestDepth = 0;
  execStack[0]  = true;
  inElseStack[0] = false;

  int i = 0;
  while (i < lineCount && isExecuting) {
    currentLine = i + 1;
    String line = lines[i];
    i++;

    if (line.length() == 0 || line.startsWith("//")) continue;

    // コメントはスキップ（if/else/endifは除く）
    if (line.startsWith("#")) continue;

    // コマンドと引数を分離
    int sp = line.indexOf(' ');
    String cmd = (sp > 0) ? line.substring(0, sp) : line;
    String args = (sp > 0) ? line.substring(sp + 1) : String("");
    cmd.toLowerCase();
    args.trim();

    bool executing = execStack[nestDepth];

    if (cmd == "if") {
      if (nestDepth < MAX_NEST - 1) nestDepth++;
      bool cond = executing ? evaluateCondition(args) : false;
      execStack[nestDepth]   = cond;
      inElseStack[nestDepth] = false;
    } else if (cmd == "else") {
      if (nestDepth > 0 && !inElseStack[nestDepth]) {
        execStack[nestDepth]   = execStack[nestDepth - 1] && !execStack[nestDepth];
        inElseStack[nestDepth] = true;
      }
    } else if (cmd == "endif") {
      if (nestDepth > 0) nestDepth--;
    } else if (executing) {
      if (cmd == "servo") {
        executeServoCommand(args);
      } else if (cmd == "servos") {
        executeServosCommand(args);
      } else if (cmd == "wait") {
        int duration = args.toInt();
        vTaskDelay(pdMS_TO_TICKS(duration));
      } else {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
      }
    }
  }

  isExecuting = false;
  scriptTaskHandle = NULL;
  Serial.println("Script execution completed");
  vTaskDelete(NULL);
}


void executeServoCommand(String args) {
  int spaceIndex = args.indexOf(' ');
  if (spaceIndex < 0) return;
  
  int channel = args.substring(0, spaceIndex).toInt();
  int angle = args.substring(spaceIndex + 1).toInt();
  
  if (channel >= 0 && channel < numServos && angle >= 0 && angle <= 180) {
    servos[channel].write(angle);
    servoAngles[channel] = angle;
    Serial.print("Servo ");
    Serial.print(channel);
    Serial.print(" -> ");
    Serial.println(angle);
  }
}

void executeServosCommand(String args) {
  int startIndex = 0;
  
  while (startIndex < args.length()) {
    int spaceIndex = args.indexOf(' ', startIndex);
    String pair;
    
    if (spaceIndex < 0) {
      pair = args.substring(startIndex);
      startIndex = args.length();
    } else {
      pair = args.substring(startIndex, spaceIndex);
      startIndex = spaceIndex + 1;
    }
    
    int colonIndex = pair.indexOf(':');
    if (colonIndex < 0) continue;
    
    int channel = pair.substring(0, colonIndex).toInt();
    int angle = pair.substring(colonIndex + 1).toInt();
    
    if (channel >= 0 && channel < numServos && angle >= 0 && angle <= 180) {
      servos[channel].write(angle);
      servoAngles[channel] = angle;
    }
  }
}

void handleServoControl() {
  String path = apiServer.uri();
  int channelStart = path.lastIndexOf('/') + 1;
  int channel = path.substring(channelStart).toInt();

  if (channel < 0 || channel >= numServos) {
    sendJSON(400, "{\"error\":\"Invalid channel\"}");
    return;
  }

  String body = apiServer.arg("plain");
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error || !doc.containsKey("angle")) {
    sendJSON(400, "{\"error\":\"Invalid request\"}");
    return;
  }

  int angle = doc["angle"];
  if (angle < 0 || angle > 180) {
    sendJSON(400, "{\"error\":\"Angle must be between 0 and 180\"}");
    return;
  }

  servos[channel].write(angle);
  servoAngles[channel] = angle;
  String response = "{\"channel\":" + String(channel) + ",\"angle\":" + String(angle) + ",\"status\":\"success\"}";
  sendJSON(200, response);
}

void handleMultiServoControl() {
  String body = apiServer.arg("plain");
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error || !doc.containsKey("servos")) {
    sendJSON(400, "{\"error\":\"Invalid request\"}");
    return;
  }

  JsonArray servosArray = doc["servos"];
  String results = "[";
  for (JsonObject servo : servosArray) {
    if (!servo.containsKey("channel") || !servo.containsKey("angle")) continue;
    int channel = servo["channel"];
    int angle   = servo["angle"];
    if (channel >= 0 && channel < numServos && angle >= 0 && angle <= 180) {
      servos[channel].write(angle);
      servoAngles[channel] = angle;
      if (results.length() > 1) results += ",";
      results += "{\"channel\":" + String(channel) + ",\"angle\":" + String(angle) + ",\"status\":\"success\"}";
    }
  }
  results += "]";
  sendJSON(200, results);
}

void handleGetServosStatus() {
  String response = "{\"servos\":[";
  for (int i = 0; i < numServos; i++) {
    if (i > 0) response += ",";
    response += "{\"channel\":" + String(i) + ",\"angle\":" + String(servoAngles[i]) + ",\"pin\":" + String(servoPins[i]) + "}";
  }
  
  response += "]}";
  sendJSON(200, response);
}


void serveWebUI() {
  String html = "";
  
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32</title></head><body>";
  html += "<h1>ESP32 Servo Controller</h1><p>Use the external Web UI to control servos.</p>";
  html += "</body></html>";
  apiServer.sendHeader("Access-Control-Allow-Origin", "*");
  apiServer.send(200, "text/html", html);
}
