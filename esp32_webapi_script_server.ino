#include <WiFi.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ===== Wi-Fi設定 =====
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// ===== サーボ設定 =====
const int numServos = 10;
const int servoPins[numServos] = {23, 22, 21, 19, 18, 5, 17, 16, 4, 12};
Servo servos[numServos];
int servoAngles[numServos];

// ===== スクリプト実行設定 =====
String currentScript = "";
bool isExecuting = false;
int currentLine = 0;
int totalLines = 0;
TaskHandle_t scriptTaskHandle = NULL;

// ===== Webサーバー設定 =====
WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  
  // サーボ初期化
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
  
  // Wi-Fi接続
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected.");
  server.begin();
  Serial.println("Server started.");
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("New Client.");
    String currentLine = "";
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
            if (currentLine.length() == 0) {
              if (contentLength > 0 && requestMethod == "POST") {
                isBodyReading = true;
              } else {
                handleRequest(client, requestMethod, requestPath, "");
                break;
              }
            } else {
              if (currentLine.startsWith("GET ") || currentLine.startsWith("POST ") || 
                  currentLine.startsWith("PUT ") || currentLine.startsWith("DELETE ")) {
                int firstSpace = currentLine.indexOf(' ');
                int secondSpace = currentLine.indexOf(' ', firstSpace + 1);
                requestMethod = currentLine.substring(0, firstSpace);
                requestPath = currentLine.substring(firstSpace + 1, secondSpace);
              }
              
              if (currentLine.startsWith("Content-Length: ")) {
                contentLength = currentLine.substring(16).toInt();
              }
              
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
      }
    }
    
    delay(10);
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
  
  if (path == "/" && method == "GET") {
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
  totalLines = 1;
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
    0
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

void scriptExecutionTask(void *parameter) {
  Serial.println("Script execution started");
  
  // スクリプトを行ごとに分割
  int lineStart = 0;
  currentLine = 0;
  
  while (lineStart < currentScript.length() && isExecuting) {
    int lineEnd = currentScript.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = currentScript.length();
    
    String line = currentScript.substring(lineStart, lineEnd);
    line.trim();
    
    currentLine++;
    
    if (line.length() > 0 && !line.startsWith("#") && !line.startsWith("//")) {
      executeScriptLine(line);
    }
    
    lineStart = lineEnd + 1;
  }
  
  isExecuting = false;
  Serial.println("Script execution completed");
  vTaskDelete(NULL);
}

void executeScriptLine(String line) {
  Serial.print("Executing: ");
  Serial.println(line);
  
  // コマンドを解析
  int spaceIndex = line.indexOf(' ');
  String cmd = "";
  String args = "";
  
  if (spaceIndex > 0) {
    cmd = line.substring(0, spaceIndex);
    args = line.substring(spaceIndex + 1);
  } else {
    cmd = line;
  }
  
  cmd.toLowerCase();
  
  if (cmd == "servo") {
    executeServoCommand(args);
  } else if (cmd == "servos") {
    executeServosCommand(args);
  } else if (cmd == "wait") {
    int duration = args.toInt();
    delay(duration);
  } else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
  }
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

void sendJSONResponse(WiFiClient &client, int statusCode, String jsonBody) {
  String statusText = "OK";
  if (statusCode == 400) statusText = "Bad Request";
  else if (statusCode == 404) statusText = "Not Found";
  
  client.println("HTTP/1.1 " + String(statusCode) + " " + statusText);
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.println(jsonBody);
}

void serveWebUI(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
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
