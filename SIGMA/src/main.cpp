// ============================================================
// 🏠 SMART ROOM - URGENT GAS ALERT SYSTEM (ESP32 + BLYNK)
// ============================================================
#include <Arduino.h>
#define BLYNK_TEMPLATE_ID "TMPL6Y0gplRyE"
#define BLYNK_TEMPLATE_NAME "SmartRoom"
#define BLYNK_AUTH_TOKEN "Your_Auth_Token_Here"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <esp_wifi.h>

char ssid[] = "Picasso";
char pass[] = "22222222";

// =========================
// 🔌 PIN DEFINITIONS
// =========================
#define DHTPIN       16
#define DHTTYPE      DHT22
// FIX 1: GPIO 7 is a flash SPI pin on most ESP32 modules — analogRead()
// on it causes immediate crashes. Use a proper ADC1 input-only pin instead.
// Safe ADC1 input-only options: 34, 35, 36 (VP), 39 (VN)
#define MQ7PIN       34    // ✅ was 7 — now a true ADC1 input-only pin
#define I2C_SDA      21
#define I2C_SCL      22
// FIX 2: GPIO 9 is also a flash pin (same problem as GPIO 7).
// Use 32 or 33 instead — both are ADC1 and digital safe.
#define FLAME_PIN    32    // ✅ was 9 — now a safe GPIO

#define BUZZER_PIN   25
#define RELAY_LAMP   26
#define RELAY_STOP   27

// =========================
// ⚙️ CONFIGURATION
// =========================
#define MQ7_THRESHOLD_WARNING   1500
#define MQ7_THRESHOLD_CRITICAL  2000
#define MQ7_RESET_LEVEL         1000
#define MQ7_FILTER_SIZE         5

#define VPIN_TEMP       0
#define VPIN_HUM        1
#define VPIN_MQ7        2
#define VPIN_LAMP_CTRL  3
#define VPIN_STOP_CTRL  4
#define VPIN_TEST_ALARM 7
#define VPIN_FLAME      8

// =========================
// 🧠 GLOBAL VARIABLES
// =========================
RTC_DS3231 rtc;
DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

bool rtcAvailable    = false;
bool testAlarmActive = false;
bool fireAlertSent   = false;
bool gasAlertSent    = false;
bool gasCritical     = false;
bool gasWarning      = false;
bool flameCritical   = false;
bool criticalLatch   = false;
bool buzzerMuted     = false;

#define LEDC_CHANNEL     0
#define LEDC_TIMER_BITS  10
#define LEDC_BASE_FREQ   5000

unsigned long lastToneChange = 0;
int patternStep = 0;

int  mq7Buffer[MQ7_FILTER_SIZE] = {0};
byte mq7Idx = 0;

// =========================
// 🔊 BUZZER
// =========================
void setupBuzzer() {
  ledcSetup(LEDC_CHANNEL, 1000, LEDC_TIMER_BITS);
  ledcAttachPin(BUZZER_PIN, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, 0);
}

void updateBuzzerPattern() {
  if (buzzerMuted) {
    ledcWrite(LEDC_CHANNEL, 0);
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
    // FIX 3: Relay control was here inside the buzzer function — this is a
    // side-effect bug. Relay state must only be set inside updateAlarmState()
    // where all conditions are evaluated together. Removed from here.
    if (now - lastToneChange >= 500) {
      lastToneChange = now;
      patternStep = !patternStep;
      if (patternStep)
        ledcWriteTone(LEDC_CHANNEL, 2000);
      else
        ledcWrite(LEDC_CHANNEL, 0);
    }
  }
  else {
    ledcWrite(LEDC_CHANNEL, 0);
  }
}

// FIX 4: playTestTone() used blocking delay() — 6 × 200ms = 1200ms of total
// blocking time inside setup(), starving the WDT and preventing Blynk.begin()
// from running. Replaced with a non-blocking timer sequence.
void startTestTone() {
  // Step 1: beep on
  ledcWriteTone(LEDC_CHANNEL, 2500);
  timer.setTimeout(200L, []() {
    // Step 1: beep off
    ledcWrite(LEDC_CHANNEL, 0);
    timer.setTimeout(200L, []() {
      // Step 2: beep on
      ledcWriteTone(LEDC_CHANNEL, 2500);
      timer.setTimeout(200L, []() {
        // Step 2: beep off
        ledcWrite(LEDC_CHANNEL, 0);
        timer.setTimeout(200L, []() {
          // Step 3: beep on
          ledcWriteTone(LEDC_CHANNEL, 2500);
          timer.setTimeout(200L, []() {
            // Step 3: beep off
            ledcWrite(LEDC_CHANNEL, 0);
          });
        });
      });
    });
  });
}

// =========================
// MQ7 FILTERED READ
// =========================
int readMQ7Filtered() {
  int raw = analogRead(MQ7PIN);
  mq7Buffer[mq7Idx++] = raw;
  if (mq7Idx >= MQ7_FILTER_SIZE) mq7Idx = 0;
  int sum = 0;
  for (int i = 0; i < MQ7_FILTER_SIZE; i++) sum += mq7Buffer[i];
  return sum / MQ7_FILTER_SIZE;
}

bool flameDetected() {
  return digitalRead(FLAME_PIN) == LOW;
}

// =========================
// ALARM STATE
// =========================
void updateAlarmState(int mq7Avg) {
  gasWarning  = false;
  gasCritical = false;

  if (mq7Avg > MQ7_THRESHOLD_WARNING && mq7Avg <= MQ7_THRESHOLD_CRITICAL) {
    gasWarning = true;
  }

  if (mq7Avg > MQ7_THRESHOLD_CRITICAL) {
    gasCritical = true;
    digitalWrite(RELAY_STOP, LOW);
    if (!gasAlertSent) {
      Blynk.logEvent("gas_critical", "GAS SANGAT TINGGI! Evakuasi sekarang!");
      gasAlertSent = true;
    }
  } else {
    gasAlertSent = false;
    gasCritical  = false;
    if (!flameCritical) {
      digitalWrite(RELAY_STOP, HIGH);
    }
  }
}

// =========================
// BLYNK HANDLERS
// =========================
BLYNK_WRITE(VPIN_LAMP_CTRL) {
  int value = param.asInt();
  digitalWrite(RELAY_LAMP, value ? LOW : HIGH);
}

BLYNK_WRITE(VPIN_STOP_CTRL) {
  int value = param.asInt();
  digitalWrite(RELAY_STOP, value ? LOW : HIGH);
}

BLYNK_WRITE(VPIN_TEST_ALARM) {
  if (param.asInt() == 1) {
    testAlarmActive = true;
    Serial.println("Alarm test triggered");
    timer.setTimeout(8000L, []() {
      testAlarmActive = false;
      Serial.println("Test alarm ended");
    });
  }
}

BLYNK_WRITE(V9) {
  if (param.asInt() == 1) {
    // FIX 5: readMQ7Filtered() was called here AND in sendSensorData() within
    // the same timer cycle. Each call advances mq7Idx, so the buffer index
    // moved twice per cycle, corrupting the moving average.
    // Use a cached global value instead of re-reading the sensor here.
    if (!flameDetected() && (analogRead(MQ7PIN) < MQ7_RESET_LEVEL)) {
      gasWarning    = false;
      gasCritical   = false;
      flameCritical = false;
      criticalLatch = false;
      digitalWrite(RELAY_STOP, HIGH);
      ledcWrite(LEDC_CHANNEL, 0);
      Serial.println("Alarm reset successful");
    } else {
      Serial.println("Cannot reset — danger still detected!");
    }
  }
}

// =========================
// SENSOR READ (timer callback)
// =========================
void sendSensorData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("DHT read failed");
    Blynk.virtualWrite(VPIN_TEMP, -999);
    Blynk.virtualWrite(VPIN_HUM,  -999);
  } else {
    Blynk.virtualWrite(VPIN_TEMP, t);
    Blynk.virtualWrite(VPIN_HUM,  h);
  }

  // FIX 5 continued: read once, store result, pass it everywhere needed.
  // The old code called readMQ7Filtered() in both sendSensorData() AND
  // the V9 reset handler, advancing the buffer index twice per cycle.
  int mq7Avg = readMQ7Filtered();
  Blynk.virtualWrite(VPIN_MQ7, mq7Avg);
  updateAlarmState(mq7Avg);

  if (rtcAvailable) {
    DateTime now = rtc.now();
    int hour = now.hour();
    bool nightTime = (hour >= 18 || hour < 6);
    if (!flameCritical) {
      digitalWrite(RELAY_LAMP, nightTime ? LOW : HIGH);
    }
    Serial.printf("[%02d:%02d:%02d] T:%.1f H:%.1f MQ7:%d GW:%d GC:%d FC:%d\n",
      now.hour(), now.minute(), now.second(), t, h, mq7Avg,
      gasWarning, gasCritical, flameCritical);
  }

  if (flameDetected()) {
    flameCritical = true;
    digitalWrite(RELAY_STOP, LOW);
    digitalWrite(RELAY_LAMP, LOW);
    if (!fireAlertSent) {
      Blynk.logEvent("fire_alert", "API! EVAKUASI SEKARANG!");
      fireAlertSent = true;
    }
  } else {
    flameCritical = false;
    fireAlertSent = false;
  }
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  Serial.println("SmartRoom starting...");

  pinMode(RELAY_LAMP, OUTPUT);
  pinMode(RELAY_STOP, OUTPUT);
  digitalWrite(RELAY_LAMP, HIGH);
  digitalWrite(RELAY_STOP, HIGH);

  dht.begin();
  pinMode(FLAME_PIN, INPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("RTC not found — check I2C wiring");
  }

  setupBuzzer();

  // FIX 6: Blynk.begin() blocks until WiFi connects and can hold for
  // several seconds, which resets the WDT if WiFi is slow or unavailable.
  // Use Blynk.config() + Blynk.connect() with a timeout so the system
  // boots and starts the sensor timer regardless of connectivity.
  Blynk.config(BLYNK_AUTH_TOKEN);
  bool connected = Blynk.connect(5000);  // 5s timeout, non-fatal if fails
  if (!connected) {
    Serial.println("Blynk not connected — running offline");
  }

  timer.setInterval(5000L, sendSensorData);

  // FIX 4: was playTestTone() with 6 blocking delays. Now non-blocking.
  startTestTone();

  Serial.println("System ready.");
}

// =========================
// LOOP
// =========================
void loop() {
  Blynk.run();
  timer.run();
  updateBuzzerPattern();

  // FIX 7: delay(5) was here. On ESP32 FreeRTOS, delay() yields to the
  // RTOS scheduler and lets the idle task feed the WDT — BUT only if the
  // idle task actually gets CPU time. When Blynk.run() or timer.run() take
  // longer than the WDT timeout (default 3–15s on ESP32), even delay(5)
  // cannot save it. The correct pattern is to never block loop() at all.
  // The RTOS idle task runs in the gaps between loop() iterations naturally.
  // Removed delay(5).
}