#include <ESP32Servo.h>

// ===== サーボ設定 =====
const int numServos = 10;
const int servoPins[numServos] = {23, 19, 18, 5, 17, 16, 4, 27, 14, 12};
Servo servos[numServos];
int servoAngles[numServos]; // 各サーボの現在の角度を保存

// ===== シリアル通信設定 =====
String inputBuffer = "";
bool commandComplete = false;

void setup() {
  Serial.begin(115200);
  
  // --- サーボの初期化 ---
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < numServos; i++) {
    servos[i].attach(servoPins[i], 500, 2500);
    servoAngles[i] = 90; // 初期角度を90度に設定
    servos[i].write(servoAngles[i]);
    delay(20);
  }
  
  Serial.println("READY"); // 準備完了を通知
}

void loop() {
  // シリアルデータの受信
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n') {
      commandComplete = true;
    } else {
      inputBuffer += inChar;
    }
  }
  
  // コマンドが完全に受信されたら処理
  if (commandComplete) {
    processCommand(inputBuffer);
    inputBuffer = "";
    commandComplete = false;
  }
}

void processCommand(String command) {
  command.trim(); // 前後の空白を削除
  
  // 空行やコメントは無視
  if (command.length() == 0 || command.startsWith("#") || command.startsWith("//")) {
    Serial.println("OK");
    return;
  }
  
  // コマンドをスペースで分割
  int spaceIndex = command.indexOf(' ');
  String cmd = "";
  String args = "";
  
  if (spaceIndex > 0) {
    cmd = command.substring(0, spaceIndex);
    args = command.substring(spaceIndex + 1);
  } else {
    cmd = command;
  }
  
  cmd.toLowerCase();
  
  // コマンドの処理
  if (cmd == "servo") {
    handleServoCommand(args);
  } else if (cmd == "servos") {
    handleServosCommand(args);
  } else if (cmd == "get") {
    handleGetCommand(args);
  } else if (cmd == "getall") {
    handleGetAllCommand();
  } else if (cmd == "ping") {
    Serial.println("PONG");
  } else if (cmd == "reset") {
    handleResetCommand();
  } else {
    Serial.println("ERROR: Unknown command");
  }
}

void handleServoCommand(String args) {
  // 形式: servo <channel> <angle>
  int spaceIndex = args.indexOf(' ');
  
  if (spaceIndex < 0) {
    Serial.println("ERROR: Invalid servo command format");
    return;
  }
  
  int channel = args.substring(0, spaceIndex).toInt();
  int angle = args.substring(spaceIndex + 1).toInt();
  
  if (channel < 0 || channel >= numServos) {
    Serial.println("ERROR: Invalid channel");
    return;
  }
  
  if (angle < 0 || angle > 180) {
    Serial.println("ERROR: Invalid angle");
    return;
  }
  
  servos[channel].write(angle);
  servoAngles[channel] = angle;
  
  Serial.print("OK: Servo ");
  Serial.print(channel);
  Serial.print(" -> ");
  Serial.println(angle);
}

void handleServosCommand(String args) {
  // 形式: servos 0:90 1:45 2:135
  int startIndex = 0;
  int count = 0;
  
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
    if (colonIndex < 0) {
      continue;
    }
    
    int channel = pair.substring(0, colonIndex).toInt();
    int angle = pair.substring(colonIndex + 1).toInt();
    
    if (channel >= 0 && channel < numServos && angle >= 0 && angle <= 180) {
      servos[channel].write(angle);
      servoAngles[channel] = angle;
      count++;
    }
  }
  
  Serial.print("OK: ");
  Serial.print(count);
  Serial.println(" servos updated");
}

void handleGetCommand(String args) {
  // 形式: get <channel>
  int channel = args.toInt();
  
  if (channel < 0 || channel >= numServos) {
    Serial.println("ERROR: Invalid channel");
    return;
  }
  
  Serial.print("ANGLE:");
  Serial.print(channel);
  Serial.print(":");
  Serial.println(servoAngles[channel]);
}

void handleGetAllCommand() {
  // 全サーボの角度を返す
  Serial.print("ANGLES:");
  for (int i = 0; i < numServos; i++) {
    Serial.print(servoAngles[i]);
    if (i < numServos - 1) {
      Serial.print(",");
    }
  }
  Serial.println();
}

void handleResetCommand() {
  // 全サーボを90度に
  for (int i = 0; i < numServos; i++) {
    servos[i].write(90);
    servoAngles[i] = 90;
  }
  Serial.println("OK: All servos reset to 90");
}
