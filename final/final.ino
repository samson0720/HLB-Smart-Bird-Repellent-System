#include <Arduino.h>
#include <Stepper.h>
#include <Adafruit_NeoPixel.h> // 務必確認已安裝此函式庫
#include <WiFi.h>
#include <WebServer.h>

// ================= 網路設定 =================
const char* ssid = "A104-1";           // TODO: 改成您的 WiFi SSID
const char* password = "3400834008"; // TODO: 改成您的 WiFi 密碼

WebServer server(80);

// ================= 腳位設定 =================
// 1. 超音波 (HC-SR04)
const int TRIG_PIN = 12; // 接 GP12
const int ECHO_PIN = 14; // 接 GP14

// 2. WS2812B 燈條
const int LED_PIN = 16;  // 綠線 (Data) 接 GP16
// 您的燈條有 144 顆
const int NUMPIXELS = 144; 

// 3. 28BYJ-48 步進馬達 (接 GP0 ~ GP3)
const int MOTOR_IN1 = 0;
const int MOTOR_IN2 = 1;
const int MOTOR_IN3 = 2;
const int MOTOR_IN4 = 3;

// ================= 參數設定 =================
const int STEPS_PER_REV = 2048; // 馬達一圈步數
const float STOP_DIST = 45.0;   // < 45cm: 紅燈 + 停止
const float WARN_DIST = 60.0;   // 45~60cm: 黃燈 + 運轉

// 建立物件
// ⚠️ 注意: Stepper 宣告時，中間兩腳順序要對調 (1, 3, 2, 4)，馬達才會順轉
Stepper motor(STEPS_PER_REV, MOTOR_IN1, MOTOR_IN3, MOTOR_IN2, MOTOR_IN4);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// 變數
float distanceCM = 0.0;
bool distanceValid = false;
bool motorIsRunning = true;
bool manualMotorStop = false;

// 函式宣告
void handleRoot();
void handleApiData();
void handleMotorControl();
void handleOptions();
void addCorsHeaders();

void setup() {
  Serial.begin(115200);

  // --- 初始化超音波 ---
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // --- 初始化燈條 ---
  pixels.begin();
  // ⚠️ 重要保護：144顆燈如果全亮電流很大，USB會推不動當機。
  // 這裡將亮度限制在 10 (最大255)，既省電又夠亮。
  pixels.setBrightness(10); 
  pixels.show(); // 先全暗

  // --- 初始化馬達 ---
  motor.setSpeed(10); // 設定轉速 (RPM)
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);

  // --- 連線到 WiFi ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("正在連線到 WiFi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(650);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("==============================");
    Serial.println("✅ WiFi 連線成功");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("GW: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println("==============================");
  } else {
    Serial.println();
    Serial.println("❌ WiFi 連線失敗，請檢查網路設定");
  }

  // --- HTTP 伺服器 ---
  server.on("/", handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/api/data", HTTP_OPTIONS, handleOptions);
  server.on("/api/control/motor", HTTP_GET, handleMotorControl);
  server.on("/api/control/motor", HTTP_POST, handleMotorControl);
  server.on("/api/control/motor", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
  server.begin();
  Serial.println("HTTP 伺服器已啟動，API: /api/data");

  Serial.println("系統啟動: >60cm綠燈, 45-60cm黃燈, <45cm紅燈停");
}

// 讀取距離函式
void readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL); // 等待 30ms
  
  if (duration == 0) {
    // 如果沒收到回波 (通常是太遠或沒插好)，視為「安全距離」
    distanceCM = 999.0; 
    distanceValid = false;
  } else {
    distanceCM = (float)duration / 29.4 / 2.0;
    if (distanceCM > 400) distanceCM = 400; // 限制最大值
    distanceValid = true;
  }
}

// 設定整條燈顏色的函式
void setAllColor(int r, int g, int b) {
  for(int i=0; i<NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

// 停止馬達並放鬆線圈 (省電不發熱)
void stopMotorCoils() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  digitalWrite(MOTOR_IN3, LOW);
  digitalWrite(MOTOR_IN4, LOW);
}

void loop() {
  server.handleClient();

  // 1. 讀取距離
  readUltrasonic();

  bool inStopZone = distanceCM < STOP_DIST;
  bool inWarnZone = distanceCM >= STOP_DIST && distanceCM < WARN_DIST;

  if (inStopZone) {
    setAllColor(255, 0, 0); // > STOP 紅燈
  } else if (inWarnZone) {
    setAllColor(255, 180, 0); // 黃燈
  } else {
    setAllColor(0, 255, 0); // 綠燈
  }

  bool shouldRunMotor = !manualMotorStop && !inStopZone;

  if (shouldRunMotor) {
    motor.step(15); // 繼續轉動
    if (!motorIsRunning) {
      Serial.println("馬達啟動");
      motorIsRunning = true;
    }
  } else {
    stopMotorCoils();
    if (motorIsRunning) {
      Serial.println("馬達停止");
      motorIsRunning = false;
    }
  }

  // 稍微延遲，避免燈光閃爍太快
  delay(10); 
}

// ================= HTTP Handlers =================

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

void handleRoot() {
  addCorsHeaders();
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Bird Guard</title>";
  html += "<style>body{font-family:Arial;background:#0f172a;color:#fff;padding:30px;text-align:center}";
  html += ".card{background:#1e293b;border-radius:16px;padding:24px;display:inline-block;min-width:260px}";
  html += ".value{font-size:48px;margin:0;color:#38bdf8}";
  html += ".status{margin-top:16px;font-size:20px}</style></head><body>";
  html += "<div class='card'><h2>距離感測器</h2>";
  if (distanceValid) {
    html += "<p class='value'>" + String(distanceCM, 1) + " cm</p>";
  } else {
    html += "<p class='value'>NO DATA</p>";
  }
  html += "<p class='status'>馬達: ";
  html += (!manualMotorStop && motorIsRunning) ? "運轉中" : "已停止";
  html += "</p>";
  html += "<p style='font-size:14px;margin-top:12px;'>API: /api/data ・ 控制: /api/control/motor?action=on|off</p>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleApiData() {
  addCorsHeaders();
  String json = "{";
  json += "\"valid\":" + String(distanceValid ? "true" : "false") + ",";
  json += "\"distance_cm\":" + String(distanceCM, 2) + ",";
  json += "\"distance_m\":" + String(distanceCM / 100.0, 3) + ",";
  json += "\"stop_threshold_cm\":" + String(STOP_DIST, 1) + ",";
  json += "\"warn_threshold_cm\":" + String(WARN_DIST, 1) + ",";
  json += "\"timestamp\":" + String(millis()) + ",";
  json += "\"manual_stop\":" + String(manualMotorStop ? "true" : "false") + ",";
  json += "\"motor_running\":" + String((!manualMotorStop && motorIsRunning) ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleMotorControl() {
  addCorsHeaders();
  String action = server.arg("action");
  action.toLowerCase();

  if (action.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"缺少 action 參數\"}");
    return;
  }

  if (action == "off" || action == "stop") {
    manualMotorStop = true;
    stopMotorCoils();
    motorIsRunning = false;
  } else if (action == "on" || action == "start") {
    manualMotorStop = false;
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"action 只能是 on/start 或 off/stop\"}");
    return;
  }

  String json = "{";
  json += "\"success\":true,";
  json += "\"action\":\"" + action + "\",";
  json += "\"manual_stop\":" + String(manualMotorStop ? "true" : "false") + ",";
  json += "\"motor_running\":" + String((!manualMotorStop && motorIsRunning) ? "true" : "false") + ",";
  json += "\"distance_cm\":" + String(distanceCM, 2);
  json += "}";
  server.send(200, "application/json", json);
}

void handleOptions() {
  addCorsHeaders();
  server.send(204, "text/plain", "");
}