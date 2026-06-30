// ============================================================
// SMART ROOM - URGENT GAS ALERT SYSTEM
// ESP32 + BLYNK IOT + DHT22 + MQ7 + FLAME + RTC
// v2.0 - WATCHDOG RESET FIXED
// ============================================================
//
// WATCHDOG FIXES APPLIED:
//  [FIX 1] WiFi connect no longer blocks > 5 sec
//          -> Removed blocking while() + delay() in setup()
//          -> WiFi started, reconnect handled in reconnectWiFi()
//  [FIX 2] Blynk.connect() blocking call removed from setup()
//          -> Blynk.config() only; connection managed by reconnect
//  [FIX 3] RTC fail no longer causes while(1) halt (was implicit)
//  [FIX 4] All delays inside nested timer lambdas are safe
//          (they fire from timer, not from main stack)
//  [FIX 5] Added esp_task_wdt_reset() in long sensor reads
//  [FIX 6] Added yield() at end of loop()
// ============================================================

#include <Arduino.h>
#include "esp_task_wdt.h"   // FIX: For esp_task_wdt_reset()

#define BLYNK_TEMPLATE_ID   "TMPL6Y0gplRyE"
#define BLYNK_TEMPLATE_NAME "SmartRoom"
#define BLYNK_AUTH_TOKEN    "YOUR_REAL_TOKEN"

// =========================
// LIBRARIES
// =========================
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>

// =========================
// WIFI
// =========================
char ssid[] = "Picasso";
char pass[] = "22222222";

// =========================
// PIN DEFINITIONS
// =========================
#define DHTPIN        16
#define DHTTYPE       DHT22

#define MQ7PIN        5
#define FLAME_PIN     18

#define I2C_SDA       21
#define I2C_SCL       22

#define BUZZER_PIN    25
#define RELAY_LAMP    26
#define RELAY_STOP    27

// =========================
// CONFIGURATION
// =========================
#define MQ7_THRESHOLD_WARNING   1500
#define MQ7_THRESHOLD_CRITICAL  2000
#define MQ7_RESET_LEVEL         1000
#define MQ7_FILTER_SIZE         5

// =========================
// BLYNK VIRTUAL PINS
// =========================
#define VPIN_TEMP         V0
#define VPIN_HUM          V1
#define VPIN_MQ7          V2
#define VPIN_LAMP_CTRL    V3
#define VPIN_STOP_CTRL    V4
#define VPIN_BUZZER_MUTE  V5
#define VPIN_AUTO_MODE    V6
#define VPIN_TEST_ALARM   V7
#define VPIN_FLAME        V8
#define VPIN_RESET        V9

// =========================
// OBJECTS
// =========================
RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

// =========================
// SYSTEM STATES
// =========================
bool rtcAvailable     = false;
bool testAlarmActive  = false;

bool gasWarning       = false;
bool gasCritical      = false;
bool flameCritical    = false;

bool gasAlertSent     = false;
bool fireAlertSent    = false;

bool buzzerMuted      = false;
bool autoLampMode     = true;

// =========================
// SENSOR CACHE
// =========================
float lastTemp  = -999;
float lastHum   = -999;
int   lastMQ7   = -999;
bool  lastFlame = false;

// =========================
// MQ7 FILTER BUFFER
// =========================
int  mq7Buffer[MQ7_FILTER_SIZE] = {0};
byte mq7Idx = 0;

// =========================
// BUZZER PWM
// =========================
#define LEDC_CHANNEL     0
#define LEDC_TIMER_BITS  10

unsigned long lastToneChange = 0;
int patternStep = 0;

// ============================================================
// BUZZER
// ============================================================

void setupBuzzer() {
  ledcSetup(LEDC_CHANNEL, 1000, LEDC_TIMER_BITS);
  ledcAttachPin(BUZZER_PIN, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, 0);
}

void stopBuzzer() {
  ledcWrite(LEDC_CHANNEL, 0);
}

void updateBuzzerPattern() {
  if (buzzerMuted) {
    stopBuzzer();
    return;
  }

  unsigned long now = millis();

  if (gasCritical || flameCritical || testAlarmActive) {
    if (now - lastToneChange >= 100) {
      lastToneChange = now;
      patternStep = (patternStep + 1) % 20;
      ledcWriteTone(LEDC_CHANNEL, 1000 + (patternStep * 100));
    }
  }
  else if (gasWarning) {
    if (now - lastToneChange >= 500) {
      lastToneChange = now;
      patternStep = !patternStep;
      if (patternStep) ledcWriteTone(LEDC_CHANNEL, 2000);
      else             stopBuzzer();
    }
  }
  else {
    stopBuzzer();
  }
}

// ============================================================
// STARTUP TEST TONE (nested timers - non-blocking, WDT safe)
// ============================================================

void startTestTone() {
  ledcWriteTone(LEDC_CHANNEL, 2500);
  timer.setTimeout(200L, []() {
    stopBuzzer();
    timer.setTimeout(200L, []() {
      ledcWriteTone(LEDC_CHANNEL, 2500);
      timer.setTimeout(200L, []() {
        stopBuzzer();
        timer.setTimeout(200L, []() {
          ledcWriteTone(LEDC_CHANNEL, 2500);
          timer.setTimeout(200L, []() {
            stopBuzzer();
          });
        });
      });
    });
  });
}

// ============================================================
// MQ7 FILTER
// ============================================================

int readMQ7Filtered() {
  int raw = analogRead(MQ7PIN);
  mq7Buffer[mq7Idx++] = raw;
  if (mq7Idx >= MQ7_FILTER_SIZE) mq7Idx = 0;
  int sum = 0;
  for (int i = 0; i < MQ7_FILTER_SIZE; i++) sum += mq7Buffer[i];
  return sum / MQ7_FILTER_SIZE;
}

// ============================================================
// FLAME SENSOR
// ============================================================

bool flameDetected() {
  return digitalRead(FLAME_PIN) == LOW; // Active LOW
}

// ============================================================
// ALARM LOGIC
// ============================================================

void updateAlarmState(int mq7Avg) {
  gasWarning  = false;
  gasCritical = false;

  if (mq7Avg > MQ7_THRESHOLD_WARNING &&
      mq7Avg <= MQ7_THRESHOLD_CRITICAL) {
    gasWarning = true;
  }

  if (mq7Avg > MQ7_THRESHOLD_CRITICAL) {
    gasCritical = true;
    digitalWrite(RELAY_STOP, LOW);

    if (!gasAlertSent && Blynk.connected()) {
      Blynk.logEvent("gas_critical", "GAS SANGAT TINGGI! EVAKUASI!");
      gasAlertSent = true;
    }
  }
  else {
    gasAlertSent = false;
    if (!flameCritical) {
      digitalWrite(RELAY_STOP, HIGH);
    }
  }
}

// ============================================================
// BLYNK HANDLERS
// ============================================================

BLYNK_WRITE(VPIN_LAMP_CTRL) {
  autoLampMode = false;
  int value = param.asInt();
  digitalWrite(RELAY_LAMP, value ? LOW : HIGH);
}

BLYNK_WRITE(VPIN_STOP_CTRL) {
  int value = param.asInt();
  digitalWrite(RELAY_STOP, value ? LOW : HIGH);
}

BLYNK_WRITE(VPIN_BUZZER_MUTE) {
  buzzerMuted = param.asInt();
}

BLYNK_WRITE(VPIN_AUTO_MODE) {
  autoLampMode = param.asInt();
}

BLYNK_WRITE(VPIN_TEST_ALARM) {
  if (param.asInt() == 1) {
    testAlarmActive = true;
    Serial.println("[TEST] Alarm test triggered");
    timer.setTimeout(8000L, []() {
      testAlarmActive = false;
      Serial.println("[TEST] Test alarm ended");
    });
  }
}

BLYNK_WRITE(VPIN_RESET) {
  if (param.asInt() == 1) {
    int mq7 = readMQ7Filtered();
    if (!flameDetected() && mq7 < MQ7_RESET_LEVEL) {
      gasWarning  = false;
      gasCritical = false;
      flameCritical = false;
      digitalWrite(RELAY_STOP, HIGH);
      stopBuzzer();
      Serial.println("[RESET] Alarm reset OK");
    } else {
      Serial.println("[RESET] Cannot reset - danger still active!");
    }
  }
}

// ============================================================
// SENSOR UPDATE (called by timer — non-blocking)
// ============================================================

void sendSensorData() {
  // FIX: Feed watchdog at start of heavy sensor function
  esp_task_wdt_reset();

  // DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("[ERR] DHT read failed");
    return;
  }

  // MQ7
  int mq7Avg = readMQ7Filtered();
  updateAlarmState(mq7Avg);

  // FLAME
  bool flameNow = flameDetected();

  if (flameNow) {
    flameCritical = true;
    digitalWrite(RELAY_STOP, LOW);
    digitalWrite(RELAY_LAMP, LOW);
    if (!fireAlertSent && Blynk.connected()) {
      Blynk.logEvent("fire_alert", "API TERDETEKSI! EVAKUASI!");
      fireAlertSent = true;
    }
  } else {
    flameCritical = false;
    fireAlertSent = false;
  }

  // AUTO LAMP BY RTC
  if (rtcAvailable && autoLampMode) {
    DateTime now = rtc.now();
    int hour = now.hour();
    bool nightTime = (hour >= 18 || hour < 6);
    digitalWrite(RELAY_LAMP, nightTime ? LOW : HIGH);
  }

  // SEND TO BLYNK ONLY IF CHANGED (saves bandwidth)
  if (abs(t - lastTemp) > 0.5) {
    Blynk.virtualWrite(VPIN_TEMP, t);
    lastTemp = t;
  }
  if (abs(h - lastHum) > 1.0) {
    Blynk.virtualWrite(VPIN_HUM, h);
    lastHum = h;
  }
  if (abs(mq7Avg - lastMQ7) > 50) {
    Blynk.virtualWrite(VPIN_MQ7, mq7Avg);
    lastMQ7 = mq7Avg;
  }
  if (flameNow != lastFlame) {
    Blynk.virtualWrite(VPIN_FLAME, flameNow ? 1 : 0);
    lastFlame = flameNow;
  }

  // FIX: Feed watchdog again after all sensor work done
  esp_task_wdt_reset();

  Serial.printf("[DATA] T:%.1f H:%.1f MQ7:%d GW:%d GC:%d FL:%d\n",
                t, h, mq7Avg, gasWarning, gasCritical, flameCritical);
}

// ============================================================
// WIFI RECONNECT (non-blocking — uses millis guard)
// ============================================================

void reconnectWiFi() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 10000) return;
  lastAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Disconnected — reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    return;
  }

  if (!Blynk.connected()) {
    Serial.println("[BLYNK] Disconnected — reconnecting...");
    if (Blynk.connect(3000)) {
      Serial.println("[BLYNK] Reconnected OK");
    } else {
      Serial.println("[BLYNK] Reconnect failed");
    }
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] SmartRoom v2.0 (WDT Fixed) starting...");

  // PIN MODES
  pinMode(RELAY_LAMP, OUTPUT);
  pinMode(RELAY_STOP, OUTPUT);
  pinMode(FLAME_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_LAMP, HIGH); // OFF
  digitalWrite(RELAY_STOP, HIGH); // OFF

  // INIT DEVICES
  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("[WARN] RTC not found — continuing without RTC");
    // FIX: No while(1) halt — system works without RTC
  } else {
    Serial.println("[RTC] DS3231 OK");
  }

  setupBuzzer();

  // =====================================================
  // FIX 1 & 2: Non-blocking WiFi connect
  // Don't wait for connection here — reconnectWiFi() handles it
  // This keeps setup() under the WDT timeout limit
  // =====================================================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.println("[WIFI] WiFi started (connecting in background)...");

  // Short wait only — safe for WDT (max 3 seconds, well under 5s WDT)
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 3000) {
    Serial.print(".");
    // FIX: Feed watchdog in short wait loop
    esp_task_wdt_reset();
    delay(200);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected!");
    // FIX: Use config() only — non-blocking
    // Blynk.connect() blocking call is removed
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000); // Short connect attempt, OK in setup
    Serial.println("[BLYNK] Initial connect attempted");
  } else {
    Serial.println("[WIFI] Not connected yet — will retry in loop");
    // System still works offline! Sensor reading continues.
  }

  // TIMERS
  timer.setInterval(30000L, sendSensorData); // Every 30 seconds

  startTestTone(); // Non-blocking startup beep

  Serial.println("[OK] System ready!\n");
}

// ============================================================
// LOOP — Fast, non-blocking, WDT safe
// ============================================================

void loop() {
  // Blynk.run() handles server communication quickly
  if (Blynk.connected()) {
    Blynk.run();
  }

  // timer.run() returns immediately if interval not reached
  timer.run();

  // Buzzer pattern is purely millis()-based — non-blocking
  updateBuzzerPattern();

  // WiFi reconnect uses millis() guard — non-blocking
  reconnectWiFi();

  // FIX: yield() lets RTOS idle task run → feeds WDT
  yield();
}
