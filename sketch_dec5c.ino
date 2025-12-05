#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <WiFiManager.h>

// ===== I2C 配置 =====
#define SDA_PIN 12
#define SCL_PIN 13
TwoWire I2CBus = Wire;

// ===== OLED 配置 =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2CBus, OLED_RESET);

// ===== RGB LED 配置 =====
#define RGB_LED_PIN 48
int breathBrightness = 0;
int breathDirection = 1;

// ===== 触摸按键 =====
#define TOUCH_LEFT_PIN   1
#define TOUCH_SELECT_PIN 2
#define TOUCH_RIGHT_PIN  7
#define TOUCH_THRESHOLD 50000

bool touched(int pin) { return touchRead(pin) > TOUCH_THRESHOLD; }
bool lastLeft = false, lastRight = false, lastSelect = false;

// ===== MAX30102 传感器 =====
MAX30105 particleSensor;
#define MAX_SAMPLES 100
uint32_t irBuffer[MAX_SAMPLES];
uint32_t redBuffer[MAX_SAMPLES];
int32_t spo2, heartRate;
int8_t validSPO2, validHeartRate;
int idx = 0;
unsigned long lastMeasureUpdate = 0;

// ===== WiFi / 配网状态 =====
bool wifiConnected = false;
unsigned long lastWiFiCheck = 0;  // 周期检查 WiFi 是否掉线

// ===== API 配置 =====
const char* API_URL = "http://apifj.com:16666/api/v1/ir/upload";
// 变为可修改字符数组（用于在配网页面中编辑）
char STUDENT_ID[20]  = "2230026069";
char DEVICE_NAME[20] = "minitor-esp32s3";
const char FW_VERSION[] = "1.0";   // 固件版本，仅显示用

// ===== 数据收集 =====
#define COLLECT_SAMPLES 500
uint32_t collectIRBuffer[COLLECT_SAMPLES];
unsigned long collectTimestamps[COLLECT_SAMPLES];
int collectIdx = 0;
bool isCollecting = false;
unsigned long collectStartTime = 0;
unsigned long lastCollectTime = 0;

// ===== 菜单系统 =====
const int MENU_NUM = 3;
const char* menuItems[MENU_NUM] = {"Overall Health", "BPM Only", "SpO2 Only"};
int menuIndex = 0;

enum State { MENU, OVERALL, BPM_ONLY, SPO2_ONLY };
State state = MENU;

// ===== 测量状态 =====
bool fingerDetected = false;
int displayBPM = 0;
int displaySPO2 = 0;
long displayIR = 0;

// ===== 波形显示 =====
#define WAVE_WIDTH 128
#define WAVE_HEIGHT 20
#define WAVE_Y 35
int waveBuffer[WAVE_WIDTH];
int waveIdx = 0;

// ===== 上传状态 =====
String uploadStatus = "";
unsigned long uploadStatusTime = 0;

// ==================== RGB LED ====================

void setupLED() {
  ledcAttach(RGB_LED_PIN, 5000, 8);
}

void setLED(int brightness) {
  ledcWrite(RGB_LED_PIN, brightness);
}

void updateLED() {
  if (!fingerDetected) {
    setLED(255);
  } else {
    breathBrightness += breathDirection * 3;
    if (breathBrightness >= 255) {
      breathBrightness = 255;
      breathDirection = -1;
    } else if (breathBrightness <= 50) {
      breathBrightness = 50;
      breathDirection = 1;
    }
    setLED(breathBrightness);
  }
}

// ==================== 配网 OLED 界面 ====================

void drawConfigPortalScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 4);
  display.println("WiFi Config Mode");
  display.setCursor(0, 18);
  display.println("SSID: ESP32-HealthMonitor");
  display.setCursor(0, 30);
  display.println("PASS: 12345678");
  display.setCursor(0, 44);
  display.println("Open 192.168.4.1");
  display.display();
}

// ==================== WiFiManager 集成 ====================
// firstTime = true: 启动时自动配网 (autoConnect)
// firstTime = false: 掉线后重新进入配网 (startConfigPortal)

void startWiFiConfig(bool firstTime) {
  // 提示用户当前进入配网模式
  drawConfigPortalScreen();

  // 为了避免旧连接状态干扰
  WiFi.disconnect(true, true);
  delay(100);

  WiFiManager wm;

  // 构造可编辑参数缓冲区（以当前全局变量为默认值）
  char studentIdBuf[sizeof(STUDENT_ID)];
  strncpy(studentIdBuf, STUDENT_ID, sizeof(studentIdBuf));
  studentIdBuf[sizeof(studentIdBuf) - 1] = '\0';

  char deviceNameBuf[sizeof(DEVICE_NAME)];
  strncpy(deviceNameBuf, DEVICE_NAME, sizeof(deviceNameBuf));
  deviceNameBuf[sizeof(deviceNameBuf) - 1] = '\0';

  // 设备状态字符串，在进入配网时带上最近一次测量值
  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf),
           "FW:%s BPM:%d SpO2:%d IR:%ld",
           FW_VERSION, displayBPM, displaySPO2, displayIR);

  // 自定义参数：学生 ID、设备名、设备状态
  WiFiManagerParameter custom_student("studentID", "Student ID", studentIdBuf, sizeof(studentIdBuf));
  WiFiManagerParameter custom_device("deviceName", "Device Name", deviceNameBuf, sizeof(deviceNameBuf));
  WiFiManagerParameter custom_status("devStatus", "Device Status (readonly)", statusBuf, sizeof(statusBuf));

  wm.addParameter(&custom_student);
  wm.addParameter(&custom_device);
  wm.addParameter(&custom_status);

  // 可选：只保留 WiFi 菜单，界面更简洁
  std::vector<const char *> menu = {"wifi", "info"};
  wm.setMenu(menu);

  // 设置标题（浏览器页面顶部标题）
  wm.setTitle("ESP32 Health Monitor");

  // 首次启动一般不限时；掉线重配适当设置超时
  if (!firstTime) {
    wm.setConfigPortalTimeout(180);  // 180 秒
  }

  bool res;
  if (firstTime) {
    // 如果已有保存 WiFi，会直接连接；否则开启 AP + 配网门户
    res = wm.autoConnect("ESP32-HealthMonitor", "12345678");
  } else {
    // 强制进入配网模式（AP + Portal）
    res = wm.startConfigPortal("ESP32-HealthMonitor", "12345678");
  }

  if (!res) {
    Serial.println("[WiFi] Config/Connect failed");
    wifiConnected = false;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("WiFi Config Fail");
    display.setCursor(0, 32);
    display.println("Restart or retry");
    display.display();

    delay(2000);
    return;
  }

  // 连接成功
  wifiConnected = true;
  Serial.println("[WiFi] Connected via WiFiManager");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  // 将用户可能修改过的参数写回全局变量
  strncpy(STUDENT_ID, custom_student.getValue(), sizeof(STUDENT_ID));
  STUDENT_ID[sizeof(STUDENT_ID) - 1] = '\0';

  strncpy(DEVICE_NAME, custom_device.getValue(), sizeof(DEVICE_NAME));
  DEVICE_NAME[sizeof(DEVICE_NAME) - 1] = '\0';

  // OLED 提示连接成功
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("WiFi Connected");
  display.setCursor(0, 28);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(1500);
}

// 周期检查 WiFi，如果掉线则自动进入配网
void checkWiFiAndReconnectIfNeeded() {
  if (!wifiConnected) return;

  unsigned long now = millis();
  if (now - lastWiFiCheck < 5000) return;  // 每 5 秒检查一次
  lastWiFiCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost connection, entering config portal...");
    wifiConnected = false;
    startWiFiConfig(false);  // 掉线重新配网
  }
}

// ==================== 菜单 UI ====================

void drawMenu() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(35, 2);
  display.println("MAIN MENU");
  display.setTextColor(SSD1306_WHITE);

  for (int i = 0; i < MENU_NUM; i++) {
    int y = 18 + i * 14;
    if (i == menuIndex) {
      display.fillRoundRect(2, y - 1, 124, 12, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRoundRect(2, y - 1, 124, 12, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(8, y + 1);
    display.print(menuItems[i]);
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

void drawOverallHealth() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(20, 1);
  display.println("Overall Health");
  display.setTextColor(SSD1306_WHITE);

  if (!fingerDetected) {
    display.setTextSize(1);
    display.setCursor(25, 28);
    display.println("Place Finger");
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(2, 14);
  display.print("BPM:");
  display.setTextSize(2);
  display.setCursor(35, 12);
  if (displayBPM > 0) display.print(displayBPM);
  else display.print("--");

  display.setTextSize(1);
  display.setCursor(2, 34);
  display.print("SpO2:");
  display.setTextSize(2);
  display.setCursor(35, 32);
  if (displaySPO2 > 0) {
    display.print(displaySPO2);
    display.setTextSize(1);
    display.print("%");
  } else display.print("--");

  display.setTextSize(1);
  display.setCursor(2, 52);
  display.print("IR:");
  display.setCursor(35, 52);
  if (displayIR > 0) display.print(displayIR);
  else display.print("--");

  display.display();
}

void drawBPMOnly() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(35, 1);
  display.println("BPM Only");
  display.setTextColor(SSD1306_WHITE);

  if (!fingerDetected) {
    display.setTextSize(1);
    display.setCursor(25, 28);
    display.println("Place Finger");
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(35, 14);
  display.print("Heart Rate");
  display.setTextSize(3);
  display.setCursor(25, 25);
  if (displayBPM > 0) display.print(displayBPM);
  else display.print("--");
  display.setTextSize(1);
  display.setCursor(95, 35);
  display.print("bpm");

  // 波形
  if (displayIR > 0) {
    for (int i = 0; i < WAVE_WIDTH - 1; i++) {
      if (waveBuffer[i] > 0 && waveBuffer[i + 1] > 0) {
        display.drawLine(i, waveBuffer[i], i + 1, waveBuffer[i + 1], SSD1306_WHITE);
      }
    }
  }

  // 收集进度 or 上传状态
  if (isCollecting) {
    int progress = (collectIdx * 100) / COLLECT_SAMPLES;
    display.fillRect(0, 54, SCREEN_WIDTH, 10, SSD1306_BLACK);
    display.setCursor(2, 55);
    display.print("Collect:");
    display.print(progress);
    display.print("%");
    int barWidth = (collectIdx * 60) / COLLECT_SAMPLES;
    display.drawRect(65, 55, 62, 8, SSD1306_WHITE);
    display.fillRect(66, 56, barWidth, 6, SSD1306_WHITE);
  } else if (uploadStatus.length() > 0 && millis() - uploadStatusTime < 3000) {
    display.fillRect(0, 54, SCREEN_WIDTH, 10, SSD1306_BLACK);
    display.setCursor(2, 55);
    display.print(uploadStatus);
  }

  display.display();
}

void drawSpO2Only() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(30, 1);
  display.println("SpO2 Only");
  display.setTextColor(SSD1306_WHITE);

  if (!fingerDetected) {
    display.setTextSize(1);
    display.setCursor(25, 28);
    display.println("Place Finger");
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(20, 18);
  display.print("Blood Oxygen");
  display.setTextSize(3);
  display.setCursor(30, 32);
  if (displaySPO2 > 0) display.print(displaySPO2);
  else display.print("--");
  display.setTextSize(2);
  display.setCursor(85, 38);
  display.print("%");
  display.display();
}

// ==================== 数据上传 ====================

void uploadDataToServer() {
  Serial.println("\n========== UPLOAD START ==========");

  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    uploadStatus = "No WiFi";
    Serial.println("X Upload failed: WiFi not connected");
    Serial.println("==================================\n");
    return;
  }

  if (collectIdx == 0) {
    uploadStatus = "No Data";
    Serial.println("X Upload failed: No data");
    Serial.println("==================================\n");
    return;
  }

  uploadStatus = "Uploading...";
  uploadStatusTime = millis();

  Serial.printf("√ Samples: %d\n", collectIdx);
  Serial.printf("√ BPM: %d\n", displayBPM);
  Serial.printf("√ Timestamp: %lu\n", collectStartTime);
  Serial.printf("√ API: %s\n", API_URL);

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(20480);
  doc["studentID"] = STUDENT_ID;
  doc["device"]    = DEVICE_NAME;
  doc["ts_ms"]     = collectStartTime;
  doc["bpm"]       = displayBPM;

  JsonArray samples = doc.createNestedArray("samples");
  for (int i = 0; i < collectIdx; i++) {
    JsonArray sample = samples.createNestedArray();
    sample.add((int)(collectTimestamps[i] - collectStartTime));
    sample.add((int)collectIRBuffer[i]);
  }

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.println("√ JSON (first 500 chars):");
  Serial.println(jsonString.substring(0, min(500, (int)jsonString.length())));
  Serial.printf("√ Size: %d bytes\n", jsonString.length());

  int httpCode = http.POST(jsonString);
  Serial.printf("\n-> HTTP Code: %d\n", httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("-> Response:");
    Serial.println(response);

    if (httpCode == 200) {
      uploadStatus = "Success!";
      Serial.println("\n*** UPLOAD SUCCESS ***");
    } else {
      uploadStatus = "HTTP:" + String(httpCode);
      Serial.println("\n*** UPLOAD FAILED ***");
    }
  } else {
    uploadStatus = "Error";
    Serial.printf("X Error: %s\n", http.errorToString(httpCode).c_str());
    Serial.println("\n*** CONNECTION ERROR ***");
  }

  Serial.println("==================================\n");
  uploadStatusTime = millis();
  http.end();
}

// ==================== 测量逻辑 ====================

void updateMeasurement() {
  bool S = touched(TOUCH_SELECT_PIN);

  // 短按 SELECT 返回主菜单（不清空 WiFi 配置）
  if (S && !lastSelect) {
    state = MENU;
    fingerDetected = false;
    isCollecting = false;
    collectIdx = 0;
    idx = 0;
    displayBPM = 0;
    displaySPO2 = 0;
    displayIR = 0;
    drawMenu();
    lastSelect = S;
    return;
  }
  lastSelect = S;

  long ir = particleSensor.getIR();
  displayIR = ir;
  fingerDetected = (ir > 50000);
  updateLED();

  if (!fingerDetected) {
    idx = 0;
    isCollecting = false;
    collectIdx = 0;
    displayBPM = 0;
    displaySPO2 = 0;

    if (state == OVERALL) drawOverallHealth();
    else if (state == BPM_ONLY) drawBPMOnly();
    else if (state == SPO2_ONLY) drawSpO2Only();
    return;
  }

  if (millis() - lastMeasureUpdate > 40) {
    lastMeasureUpdate = millis();

    irBuffer[idx] = ir;
    redBuffer[idx] = particleSensor.getRed();
    idx++;

    if (state == BPM_ONLY) {
      int waveY = map(ir % 20000, 0, 20000, WAVE_Y + WAVE_HEIGHT, WAVE_Y);
      waveBuffer[waveIdx] = waveY;
      waveIdx = (waveIdx + 1) % WAVE_WIDTH;
    }

    if (idx >= MAX_SAMPLES) {
      maxim_heart_rate_and_oxygen_saturation(
        irBuffer, MAX_SAMPLES,
        redBuffer,
        &spo2, &validSPO2,
        &heartRate, &validHeartRate
      );

      Serial.printf("HR=%d valid=%d SpO2=%d valid=%d\n",
                    heartRate, validHeartRate, spo2, validSPO2);

      if (validHeartRate && heartRate > 40 && heartRate < 180) {
        displayBPM = heartRate;

        if (state == BPM_ONLY && !isCollecting && collectIdx == 0) {
          isCollecting = true;
          collectIdx = 0;
          collectStartTime = millis();
          lastCollectTime = millis();
          Serial.println("=== START COLLECTION ===");
          Serial.printf("BPM: %d\n", heartRate);
        }
      }

      if (validSPO2 && spo2 > 85 && spo2 <= 100) {
        displaySPO2 = spo2;
      }

      idx = 0;
    }
  }

  if (state == BPM_ONLY && isCollecting) {
    unsigned long now = millis();
    if (now - lastCollectTime >= 20 && collectIdx < COLLECT_SAMPLES) {
      lastCollectTime = now;
      collectIRBuffer[collectIdx] = ir;
      collectTimestamps[collectIdx] = now;
      collectIdx++;

      if (collectIdx % 100 == 0) {
        Serial.printf("Collecting: %d/%d\n", collectIdx, COLLECT_SAMPLES);
      }
    }

    if (collectIdx >= COLLECT_SAMPLES) {
      Serial.println("=== COLLECTION COMPLETE ===");
      Serial.printf("Total: %d samples\n", collectIdx);
      Serial.printf("Duration: %lu ms\n", millis() - collectStartTime);

      isCollecting = false;
      uploadDataToServer();

      delay(2000);
      collectIdx = 0;
    }
  }

  if (state == OVERALL) drawOverallHealth();
  else if (state == BPM_ONLY) drawBPMOnly();
  else if (state == SPO2_ONLY) drawSpO2Only();
}

// ==================== 主程序 ====================

void setup() {
  Serial.begin(115200);
  delay(200);

  I2CBus.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
  } else {
    Serial.println("OLED OK");
  }

  setupLED();
  setLED(255);

  if (!particleSensor.begin(I2CBus, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
  } else {
    Serial.println("MAX30102 OK");
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x3F);
    particleSensor.setPulseAmplitudeIR(0x3F);
    particleSensor.setPulseAmplitudeGreen(0);
  }

  // 用 WiFiManager 进行首次配网（替代原来的 connectWiFi()）
  startWiFiConfig(true);

  drawMenu();
}

void loop() {
  bool L = touched(TOUCH_LEFT_PIN);
  bool R = touched(TOUCH_RIGHT_PIN);
  bool S = touched(TOUCH_SELECT_PIN);

  // 长按 SELECT（>2s）强制重新进入配网模式（手动触发）
  static unsigned long selectPressStart = 0;
  if (S) {
    if (selectPressStart == 0) {
      selectPressStart = millis();
    } else if (millis() - selectPressStart > 2000 && !lastSelect) {
      // 进入手动配网
      Serial.println("[Key] Long press SELECT -> Start config portal");
      startWiFiConfig(false);
    }
  } else {
    selectPressStart = 0;
  }

  if (state == MENU) {
    if (L && !lastLeft) {
      menuIndex = (menuIndex - 1 + MENU_NUM) % MENU_NUM;
      drawMenu();
    }

    if (R && !lastRight) {
      menuIndex = (menuIndex + 1) % MENU_NUM;
      drawMenu();
    }

    if (S && !lastSelect) {
      if (menuIndex == 0) state = OVERALL;
      else if (menuIndex == 1) state = BPM_ONLY;
      else if (menuIndex == 2) state = SPO2_ONLY;

      idx = 0;
      displayBPM = 0;
      displaySPO2 = 0;
      displayIR = 0;
      fingerDetected = false;
      isCollecting = false;
      collectIdx = 0;
      waveIdx = 0;
      memset(waveBuffer, 0, sizeof(waveBuffer));
    }

    updateLED();
  } else {
    updateMeasurement();
  }

  lastLeft = L;
  lastRight = R;
  lastSelect = S;

  // 周期检查 WiFi 掉线，必要时自动进入 Captive Portal
  checkWiFiAndReconnectIfNeeded();

  delay(20);
}
