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
WiFiServer server(80);

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
  server.begin();
  Serial.println("Server started.");
  Serial.printf("URL: http://%s\n", WiFi.localIP().toString().c_str());
  oledPrint("Ready!", WiFi.localIP().toString(), "OTA port:" + String(OTA_PORT));
}

void loop() {
  otaServer.handleClient();
  ESP32FwUploader.loop();

  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("New Client.");
    String headerLine = "";
    String requestMethod = "";
    String requestPath = "";
    String requestBody = "";
    bool isBodyReading = false;
    int contentLength = 0;
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (isBodyReading) {
          requestBody += c;
          if (requestBody.length() >= contentLength) {
            handleRequest(client, requestMethod, requestPath, requestBody);
            break;
          }
        } else {
          if (c == '\n') {
            if (headerLine.length() == 0) {
              if (contentLength > 0) {
                isBodyReading = true;
              } else {
                handleRequest(client, requestMethod, requestPath, "");
                break;
              }
            } else {
              if (headerLine.startsWith("GET ") || headerLine.startsWith("POST ") || 
                  headerLine.startsWith("PUT ") || headerLine.startsWith("DELETE ") ||
                  headerLine.startsWith("OPTIONS ")) {
                int firstSpace = headerLine.indexOf(' ');
                int secondSpace = headerLine.indexOf(' ', firstSpace + 1);
                requestMethod = headerLine.substring(0, firstSpace);
                requestPath = headerLine.substring(firstSpace + 1, secondSpace);
              }
              
              if (headerLine.startsWith("Content-Length: ")) {
                contentLength = headerLine.substring(16).toInt();
              }
              
              headerLine = "";
            }
          } else if (c != '\r') {
            headerLine += c;
          }
        }
      }
    }
    
    client.flush();
    delay(50);
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

void handleRequest(WiFiClient &client, String method, String path, String body) {
  Serial.print("Method: ");
  Serial.println(method);
  Serial.print("Path: ");
  Serial.println(path);
  Serial.print("Body: ");
  Serial.println(body);
  
  if (method == "OPTIONS") {
    sendCORSResponse(client);
  } else if (path == "/" && method == "GET") {
    serveWebUI(client);
  } else if (path == "/api/script/upload" && method == "POST") {
    handleScriptUpload(client, body);
  } else if (path == "/api/script/execute" && method == "POST") {
    handleScriptExecute(client);
  } else if (path == "/api/script/stop" && method == "POST") {
    handleScriptStop(client);
  } else if (path == "/api/script/status" && method == "GET") {
    handleScriptStatus(client);
  } else if (path.startsWith("/api/servo/") && method == "POST") {
    handleServoControl(client, path, body);
  } else if (path == "/api/servos" && method == "POST") {
    handleMultiServoControl(client, body);
  } else if (path == "/api/servos" && method == "GET") {
    handleGetServosStatus(client);
  } else {
    sendJSONResponse(client, 404, "{\"error\":\"Not Found\"}");
  }
}

void handleScriptUpload(WiFiClient &client, String body) {
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    sendJSONResponse(client, 400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  if (!doc.containsKey("script")) {
    sendJSONResponse(client, 400, "{\"error\":\"Missing script parameter\"}");
    return;
  }
  
  currentScript = doc["script"].as<String>();
  
  // 行数をカウント
  totalLines = (currentScript.length() == 0) ? 0 : 1;
  for (int i = 0; i < currentScript.length(); i++) {
    if (currentScript.charAt(i) == '\n') totalLines++;
  }
  
  String response = "{\"status\":\"success\",\"script_id\":\"script_001\",\"lines\":" + String(totalLines) + "}";
  sendJSONResponse(client, 200, response);
  
  Serial.println("Script uploaded:");
  Serial.println(currentScript);
}

void handleScriptExecute(WiFiClient &client) {
  if (currentScript.length() == 0) {
    sendJSONResponse(client, 400, "{\"error\":\"No script uploaded\"}");
    return;
  }
  
  if (isExecuting) {
    sendJSONResponse(client, 400, "{\"error\":\"Script already running\"}");
    return;
  }
  
  // スクリプト実行タスクを作成
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
  
  sendJSONResponse(client, 200, "{\"status\":\"running\",\"execution_id\":\"exec_001\"}");
}

void handleScriptStop(WiFiClient &client) {
  if (!isExecuting) {
    sendJSONResponse(client, 400, "{\"error\":\"No script running\"}");
    return;
  }
  
  if (scriptTaskHandle != NULL) {
    vTaskDelete(scriptTaskHandle);
    scriptTaskHandle = NULL;
  }
  
  isExecuting = false;
  sendJSONResponse(client, 200, "{\"status\":\"stopped\"}");
}

void handleScriptStatus(WiFiClient &client) {
  String status = isExecuting ? "running" : "idle";
  String response = "{\"status\":\"" + status + "\",\"current_line\":" + String(currentLine) + ",\"total_lines\":" + String(totalLines) + "}";
  sendJSONResponse(client, 200, response);
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

void handleServoControl(WiFiClient &client, String path, String body) {
  int channelStart = path.lastIndexOf('/') + 1;
  int channel = path.substring(channelStart).toInt();
  
  if (channel < 0 || channel >= numServos) {
    sendJSONResponse(client, 400, "{\"error\":\"Invalid channel\"}");
    return;
  }
  
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error || !doc.containsKey("angle")) {
    sendJSONResponse(client, 400, "{\"error\":\"Invalid request\"}");
    return;
  }
  
  int angle = doc["angle"];
  
  if (angle < 0 || angle > 180) {
    sendJSONResponse(client, 400, "{\"error\":\"Angle must be between 0 and 180\"}");
    return;
  }
  
  servos[channel].write(angle);
  servoAngles[channel] = angle;
  
  String response = "{\"channel\":" + String(channel) + ",\"angle\":" + String(angle) + ",\"status\":\"success\"}";
  sendJSONResponse(client, 200, response);
}

void handleMultiServoControl(WiFiClient &client, String body) {
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error || !doc.containsKey("servos")) {
    sendJSONResponse(client, 400, "{\"error\":\"Invalid request\"}");
    return;
  }
  
  JsonArray servosArray = doc["servos"];
  String results = "[";
  
  for (JsonObject servo : servosArray) {
    if (!servo.containsKey("channel") || !servo.containsKey("angle")) continue;
    
    int channel = servo["channel"];
    int angle = servo["angle"];
    
    if (channel >= 0 && channel < numServos && angle >= 0 && angle <= 180) {
      servos[channel].write(angle);
      servoAngles[channel] = angle;
      
      if (results.length() > 1) results += ",";
      results += "{\"channel\":" + String(channel) + ",\"angle\":" + String(angle) + ",\"status\":\"success\"}";
    }
  }
  
  results += "]";
  sendJSONResponse(client, 200, results);
}

void handleGetServosStatus(WiFiClient &client) {
  String response = "{\"servos\":[";
  
  for (int i = 0; i < numServos; i++) {
    if (i > 0) response += ",";
    response += "{\"channel\":" + String(i) + ",\"angle\":" + String(servoAngles[i]) + ",\"pin\":" + String(servoPins[i]) + "}";
  }
  
  response += "]}";
  sendJSONResponse(client, 200, response);
}

void sendCORSResponse(WiFiClient &client) {
  client.println("HTTP/1.1 204 No Content");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Connection: close");
  client.println();
}

void sendJSONResponse(WiFiClient &client, int statusCode, String jsonBody) {
  String statusText = "OK";
  if (statusCode == 400) statusText = "Bad Request";
  else if (statusCode == 404) statusText = "Not Found";

  String response = "HTTP/1.1 " + String(statusCode) + " " + statusText + "\r\n";
  response += "Content-Type: application/json\r\n";
  response += "Access-Control-Allow-Origin: *\r\n";
  response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  response += "Access-Control-Allow-Headers: Content-Type\r\n";
  response += "Content-Length: " + String(jsonBody.length()) + "\r\n";
  response += "Connection: close\r\n";
  response += "\r\n";
  response += jsonBody;
  client.print(response);
  client.flush();
}

void serveWebUI(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html><html lang='ja'><head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>ESP32 Script Controller</title>");
  client.println("<style>");
  client.println("body{font-family:sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}");
  client.println(".container{max-width:1200px;margin:0 auto}");
  client.println(".header{text-align:center;color:white;margin-bottom:30px}");
  client.println(".card{background:white;border-radius:15px;padding:25px;box-shadow:0 10px 30px rgba(0,0,0,0.3);margin-bottom:20px}");
  client.println(".btn{padding:12px 24px;border:none;border-radius:8px;cursor:pointer;font-weight:bold;margin:5px}");
  client.println(".btn-primary{background:#667eea;color:white}");
  client.println(".btn-success{background:#48bb78;color:white}");
  client.println(".btn-danger{background:#f56565;color:white}");
  client.println("textarea{width:100%;min-height:300px;font-family:monospace;padding:10px;border-radius:5px}");
  client.println(".status{padding:10px;margin:10px 0;border-radius:5px;background:#e6fffa}");
  client.println("</style></head><body>");
  
  client.println("<div class='container'>");
  client.println("<div class='header'><h1>🤖 ESP32 Script Controller</h1></div>");
  client.println("<div class='card'>");
  client.println("<h2>スクリプトエディタ</h2>");
  client.println("<textarea id='scriptEditor' placeholder='# スクリプトを入力\\nservo 0 90\\nwait 1000\\nservo 0 0'></textarea>");
  client.println("<div><button class='btn btn-primary' onclick='uploadScript()'>アップロード</button>");
  client.println("<button class='btn btn-success' onclick='executeScript()'>実行</button>");
  client.println("<button class='btn btn-danger' onclick='stopScript()'>停止</button></div>");
  client.println("<div class='status' id='status'>準備完了</div>");
  client.println("</div></div>");
  
  client.println("<script>");
  client.println("async function uploadScript(){");
  client.println("const script=document.getElementById('scriptEditor').value;");
  client.println("const res=await fetch('/api/script/upload',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({script})});");
  client.println("const data=await res.json();");
  client.println("document.getElementById('status').textContent='アップロード完了: '+data.lines+'行';");
  client.println("}");
  client.println("async function executeScript(){");
  client.println("const res=await fetch('/api/script/execute',{method:'POST'});");
  client.println("const data=await res.json();");
  client.println("document.getElementById('status').textContent='実行中...';");
  client.println("checkStatus();");
  client.println("}");
  client.println("async function stopScript(){");
  client.println("await fetch('/api/script/stop',{method:'POST'});");
  client.println("document.getElementById('status').textContent='停止しました';");
  client.println("}");
  client.println("async function checkStatus(){");
  client.println("const res=await fetch('/api/script/status');");
  client.println("const data=await res.json();");
  client.println("document.getElementById('status').textContent='状態: '+data.status+' ('+data.current_line+'/'+data.total_lines+')';");
  client.println("if(data.status==='running')setTimeout(checkStatus,500);");
  client.println("}");
  client.println("</script>");
  
  client.println("</body></html>");
}
