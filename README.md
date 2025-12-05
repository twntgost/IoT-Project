# 🩺 ESP32-S3 Wearable Health Monitoring Device

## ✨ Project Overview

[cite_start]This project involves the design and implementation of a multi-functional wearable health monitoring device based on the **ESP32-S3** microcontroller[cite: 3]. [cite_start]The device is capable of capturing real-time physiological signals, including Heart Rate (**BPM**), Blood Oxygen Saturation (**SpO2**), and Heart Rate Variability (**HRV**)[cite: 4]. [cite_start]It uses an OLED display for intuitive user feedback and implements **Smart WiFi Configuration** and **HTTP Data Upload** as key advanced features[cite: 5, 6, 44, 51].

---

## 🚀 Feature Implementation Summary

| Feature Category | ID | Feature Description | Status | Points |
| :--- | :--- | :--- | :--- | :--- |
| **Core Minimum** | 2.1 | [cite_start]WiFi Connection at Boot (Replaced by WiFiManager) [cite: 10] | **Implemented** | 6\% |
| | 2.2 | [cite_start]Main Menu System (Overall, BPM Only, SpO2 Only) [cite: 17] | **Implemented** | 6\% |
| | 2.3 | [cite_start]Touch Button Navigation (Left, Select, Right) [cite: 23] | **Implemented** | 6\% |
| | 2.4 | [cite_start]Measurement Screens & Real-Time Functionality [cite: 28] | **Implemented** | 12\% |
| **Advanced Features** | 3.1 | [cite_start]**Smart WiFi Configuration via Captive Portal** [cite: 44] | **Implemented** | 6\% |
| | 3.2 | [cite_start]**HTTP Data Upload + Server Response Handling** (BPM Only) [cite: 51] | **Implemented** | 4\% |
| | 3.3 | [cite_start]**IR Signal Waveform Display** (BPM Only) [cite: 58] | **Implemented** | 2\% |
| | 3.4 | [cite_start]HRV / Stress Index Calculation [cite: 65] | Not Implemented | 0\% |

---

## 🛠️ Hardware Setup Details

The device is built using the ESP32-S3 with a MAX30102 sensor and an SSD1306 OLED display.

### 🔌 Wiring Diagram

The key pins used for the project are:

| Component | Signal | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- | :--- |
| MAX30102 / OLED | SDA (I2C Data) | `12` | Shared I2C Bus (`I2CBus`) |
| MAX30102 / OLED | SCL (I2C Clock) | `13` | Shared I2C Bus (`I2CBus`) |
| RGB LED | Control Pin | `48` | [cite_start]Dynamic feedback via PWM [cite: 39] |
| Touch Button | Left Button | `1` | [cite_start]Menu navigation [cite: 24] |
| Touch Button | Select Button | `2` | [cite_start]Enter/Exit Menu [cite: 25] |
| Touch Button | Right Button | `7` | [cite_start]Menu navigation [cite: 26] |



---

## 💻 Software Implementation and Submission Requirements

### 📚 Libraries Required

Ensure you have the following libraries installed in your Arduino IDE:

* `Adafruit GFX Library`
* `Adafruit SSD1306`
* `SparkFun MAX30105` (Compatible with MAX30102)
* `ArduinoJson`
* `WiFiManager`

[cite_start]The project also requires the custom source files `spo2_algorithm.h` and `spo2_algorithm.cpp`[cite: 78].

### ⚙️ How to Compile and Flash

1.  Open the project file in the Arduino IDE.
2.  Select the **ESP32-S3** board and the correct COM port.
3.  Click the "Upload" button.

### 📝 Submission Highlights

* [cite_start]**Runnable Source Code** is provided with clear comments[cite: 78].
* [cite_start]**Hardware Setup Details** and a wiring diagram are included in this document[cite: 80].
* [cite_start]**Advanced Features** configuration flow and usage are detailed below[cite: 81].

---

## 🧠 Advanced Features Deep Dive

### 3.1 🌐 Smart WiFi Configuration (6%)

[cite_start]This implementation replaces the hardcoded credentials requirement[cite: 44].

* [cite_start]**Logic:** At boot, the device uses the **WiFiManager** library to check **Non-Volatile Storage (NVS)** for saved credentials (SSID, password, Student ID, Device Name)[cite: 45].
* **Configuration Flow:**
    * [cite_start]If credentials are not found or the connection fails, the device launches an **Access Point (AP)** named `ESP32-HealthMonitor` and a **Captive Portal**[cite: 47].
    * [cite_start]Users connect and input the required settings (WiFi credentials, Student ID, Device Name) via a configuration webpage[cite: 48].
    * [cite_start]Settings are saved to NVS, and the device attempts to auto-connect[cite: 49].
* [cite_start]**Robustness:** If the connection is lost during operation, the device automatically reverts back to AP mode after a periodic check (`checkWiFiAndReconnectIfNeeded()`) to await reconfiguration[cite: 50].
* **Manual Reset:** Long-pressing the **Select** touch button ($\geq 2s$) forces the device into the configuration portal.

### 3.2 📤 HTTP Data Upload + Server Response (4%)

[cite_start]This feature is **ONLY** active when selecting the **"BPM Only"** measurement screen[cite: 53].

* [cite_start]**Collection Trigger:** Upon detecting a valid BPM, the device starts a **10-second data collection window**[cite: 54].
* [cite_start]**Sampling:** Raw IR samples are recorded at **50Hz** (500 samples total) along with their timestamps[cite: 55, 56].
* **Upload Logic:**
    * [cite_start]Data is packaged into a **JSON** object containing `studentID`, `device`, `ts_ms` (start time), `bpm`, and the raw `samples` array[cite: 57].
    * The JSON is sent via **HTTP POST** to the configured API URL (`API_URL`).
* [cite_start]**Feedback:** The **"BPM Only"** screen displays a **collection progress bar** and, upon completion, shows the **HTTP response status** (e.g., "Success!", "HTTP:404") for three seconds[cite: 51].

### 3.3 📈 IR Signal Waveform Display (2\%)

[cite_start]This feature is **ONLY** active when selecting the **"BPM Only"** measurement screen[cite: 60].

* [cite_start]**Visualization:** A real-time **scrolling waveform** of the raw IR signal is plotted on the OLED[cite: 61].
* [cite_start]**Mechanism:** A \textbf{rolling buffer} (`waveBuffer`) of 128 pixels wide is used to map the latest raw IR values to the OLED's y-axis, providing a smooth visualization of the PPG signal[cite: 62, 63].
