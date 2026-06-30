#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <ArduinoOTA.h>

// PIN CONFIGURATION
#define BUZZER_PIN 4
#define MQ7_PIN 5
#define FLAME_PIN 6
#define DHT_PIN 7
#define SDA_PIN 8
#define SCL_PIN 9
#define RELAY_LAMP 10
#define RELAY_STOP 20

#define DHTTYPE DHT22

// CREDENTIALS
const char *ssid = "SSID_WIFI_HERE";
const char *password = "PASS_WIFI_HERE";
#define BOT_TOKEN "BOT_TOKEN_HERE" // Ganti dengan token bot Telegram Anda
String chatID = "CHAT_ID_HERE"; // Ganti dengan chat ID Anda

// OBJECTS
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
DHT dht(DHT_PIN, DHTTYPE);
RTC_DS3231 rtc;

// STATE VARIABLES
enum AlarmState
{
  NORMAL,
  WARNING,
  CRITICAL
};
AlarmState alarmState = NORMAL;

bool alarmLock = false;
bool modeAutoLampu = true; // Dipisah agar lampu dan stop kontak
bool modeAutoStop = true;  // bisa di-auto/manual secara mandiri

bool notifWarningSent = false;
bool notifCriticalSent = false;

int nilaiMQ7 = 0;
bool apiTerdeteksi = false;
float suhu = 0;
float hum = 0;

// CONFIGURATION JADWAL WAKTU (Default)
int lampOnHour = 18;
int lampOffHour = 6;
int stopOnHour = 6;
int stopOffHour = 18;

// TIMERS (Non-blocking)
unsigned long sensorTimer = 0;
unsigned long warningTimer = 0;
unsigned long buzzerTimer = 0;
unsigned long wifiTimer = 0;
unsigned long telegramTimer = 0;

bool buzzerState = false;
int buzzerFreq = 1000;

// PROTOTYPES
void connectWiFi();
void cekWiFi();
void bacaSensor();
int bacaMQ7();
void kontrolAlarm();
void kontrolBuzzer();
void jadwalOtomatis();
void cekTelegram();
void beepStart();
void setupOTA();

void setupOTA()
{
  ArduinoOTA.setHostname("SIGMA_SMARTROOM");
  ArduinoOTA.onStart([]()
                     { Serial.println("[OTA] Update Mulai..."); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\n[OTA Update Selesai"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
  ArduinoOTA.begin();
  Serial.println("[OTA] Ready. IP address: " + WiFi.localIP().toString());
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n==============================");
  Serial.println(" ESP32-S3 SMART ROOM SYSTEM ");
  Serial.println("==============================");

  pinMode(RELAY_LAMP, OUTPUT);
  pinMode(RELAY_STOP, OUTPUT);
  pinMode(FLAME_PIN, INPUT);

  digitalWrite(RELAY_LAMP, HIGH);
  digitalWrite(RELAY_STOP, HIGH);

  analogReadResolution(12);

  Serial.println("[DHT22] Initializing...");
  dht.begin();

  Serial.println("[RTC] Initializing...");
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!rtc.begin())
  {
    Serial.println("[RTC] ERROR: RTC NOT FOUND");
  }
  else
  {
    Serial.println("[RTC] DS3231 OK");
    if (rtc.lostPower())
    {
      Serial.println("[RTC] Setting waktu awal");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } // rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Aktifkan jika ingin sinkronisasi waktu pertama kali
  }

  connectWiFi();
  client.setInsecure();
  setupOTA();
  beepStart();

  Serial.println("[SYSTEM] READY");
  bot.sendMessage(chatID, "ESP32-S3 sistem aktif. Ketik /help untuk bantuan jadwal.", "");
  Serial.print("[RESET] reason: ");
  Serial.println(esp_reset_reason());
}

void loop()
{
  ArduinoOTA.handle();

  cekWiFi();

  if (millis() - telegramTimer >= 2000)
  {
    telegramTimer = millis();
    cekTelegram();
  }

  if (millis() - sensorTimer >= 5000)
  {
    sensorTimer = millis();
    bacaSensor();
  }

  kontrolAlarm();
  kontrolBuzzer();
  jadwalOtomatis();
}

void connectWiFi()
{
  Serial.println("[WIFI] Connecting...");
  WiFi.begin(ssid, password);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20)
  {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WIFI] Connected");
    Serial.print("[WIFI] IP : ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\n[WIFI] FAILED");
  }
}

void cekWiFi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - wifiTimer > 10000)
    {
      wifiTimer = millis();
      Serial.println("[WIFI] Reconnecting");
      connectWiFi();
    }
  }
}

void bacaSensor()
{
  Serial.println("\n[SENSOR] Reading...");
  suhu = dht.readTemperature();
  hum = dht.readHumidity();
  nilaiMQ7 = bacaMQ7();
  apiTerdeteksi = digitalRead(FLAME_PIN) == LOW;

  Serial.println("--------------------");
  Serial.print("[DHT22] Suhu : ");
  Serial.print(suhu);
  Serial.println(" C");
  Serial.print("[DHT22] Hum  : ");
  Serial.print(hum);
  Serial.println(" %");
  Serial.print("[MQ7] CO     : ");
  Serial.println(nilaiMQ7);
  Serial.print("[FLAME]      : ");
  Serial.println(apiTerdeteksi ? "API" : "AMAN");
  Serial.print("Waktu Perangkat saat ini : ");
  Serial.println(rtc.now().timestamp());
  Serial.println("--------------------");
}

int bacaMQ7()
{
  long total = 0;
  for (int i = 0; i < 5; i++)
  {
    total += analogRead(MQ7_PIN);
    delay(10);
  }
  return total / 5;
}

void kontrolAlarm()
{
  if (nilaiMQ7 > 400 || apiTerdeteksi && suhu > 45)
  {
    alarmState = CRITICAL;
    alarmLock = true;
    digitalWrite(RELAY_STOP, HIGH); // Force ON/Cutoff tergantung konfigurasi hardware Anda
    digitalWrite(RELAY_LAMP, HIGH); // Force OFF Lampu

    if (!notifCriticalSent)
    {
      bot.sendMessage(chatID, "🚨 BAHAYA GAS/API TERDETEKSI! Stop kontak dimatikan/diambil alih sistem keamanan!", "");
      notifCriticalSent = true;
    }
    return;
  }

  if (nilaiMQ7 > 300)
  {
    if (warningTimer == 0)
      warningTimer = millis();
    if (millis() - warningTimer > 10000)
    {
      alarmState = WARNING;
      if (!notifWarningSent)
      {
        bot.sendMessage(chatID, "⚠️ CO tinggi", "");
        notifWarningSent = true;
      }
    }
    return;
  }

  warningTimer = 0;
  if (nilaiMQ7 < 1000 && !alarmLock)
  {
    alarmState = NORMAL;
    notifWarningSent = false;
    notifCriticalSent = false;
  }
}

void kontrolBuzzer()
{
  if (alarmState == CRITICAL)
  {
    if (millis() - buzzerTimer > 100)
    {
      buzzerTimer = millis();
      buzzerFreq += 500;
      if (buzzerFreq > 3000)
        buzzerFreq = 1000;
      tone(BUZZER_PIN, buzzerFreq);
    }
  }
  else if (alarmState == WARNING)
  {
    if (millis() - buzzerTimer > 500)
    {
      buzzerTimer = millis();
      buzzerState = !buzzerState;
      if (buzzerState)
        tone(BUZZER_PIN, 2000);
      else
        noTone(BUZZER_PIN);
    }
  }
  else
  {
    noTone(BUZZER_PIN);
  }
}

void jadwalOtomatis()
{
  DateTime now = rtc.now();
  int jam = now.hour();

  // Logika Jadwal Lampu (Mendukung rentang waktu melewati tengah malam)
  if (modeAutoLampu)
  {
    bool harusNyala = false;
    if (lampOnHour < lampOffHour)
    {
      harusNyala = (jam >= lampOnHour && jam < lampOffHour);
    }
    else
    {
      harusNyala = (jam >= lampOnHour || jam < lampOffHour);
    }
    digitalWrite(RELAY_LAMP, harusNyala ? LOW : HIGH); // LOW = ON
  }

  // Logika Jadwal Stop Kontak (Hanya berjalan jika kondisi lingkungan NORMAL)
  if (modeAutoStop && alarmState == NORMAL && !alarmLock)
  {
    bool harusNyala = false;
    if (stopOnHour < stopOffHour)
    {
      harusNyala = (jam >= stopOnHour && jam < stopOffHour);
    }
    else
    {
      harusNyala = (jam >= stopOnHour || jam < stopOffHour);
    }
    digitalWrite(RELAY_STOP, harusNyala ? LOW : HIGH); // LOW = ON
  }

  // Kipas Pendingin Ruangan suhu > 35C
  if (suhu > 35)
  {
    digitalWrite(RELAY_STOP, LOW); // Ny lakan stop kontak untuk kipas
  }
}

void cekTelegram()
{
  int jumlah = bot.getUpdates(bot.last_message_received + 1);

  if (jumlah > 0)
  {
    for (int i = 0; i < jumlah; i++)
    {
      String cmd = bot.messages[i].text;

      // 1. PENGATURAN WAKTU LAMPU
      if (cmd.startsWith("/set_lampu_on"))
      {
        int jamInput = cmd.substring(14).toInt();
        if (jamInput >= 0 && jamInput <= 23)
        {
          lampOnHour = jamInput;
          bot.sendMessage(chatID, "✅ Jadwal LAMPU NYALA di-set ke pukul: " + String(lampOnHour) + ":00", "");
        }
        else
        {
          bot.sendMessage(chatID, "❌ Format salah. Gunakan angka 0-23. Contoh: /set_lampu_on 18", "");
        }
      }
      else if (cmd.startsWith("/set_lampu_off"))
      {
        int jamInput = cmd.substring(15).toInt();
        if (jamInput >= 0 && jamInput <= 23)
        {
          lampOffHour = jamInput;
          bot.sendMessage(chatID, "✅ Jadwal LAMPU MATI di-set ke pukul: " + String(lampOffHour) + ":00", "");
        }
        else
        {
          bot.sendMessage(chatID, "❌ Format salah. Gunakan angka 0-23. Contoh: /set_lampu_off 6", "");
        }
      }

      // 2. PENGATURAN WAKTU STOP KONTAK
      else if (cmd.startsWith("/set_stop_on"))
      {
        int jamInput = cmd.substring(13).toInt();
        if (jamInput >= 0 && jamInput <= 23)
        {
          stopOnHour = jamInput;
          bot.sendMessage(chatID, "✅ Jadwal STOP KONTAK NYALA di-set ke pukul: " + String(stopOnHour) + ":00", "");
        }
        else
        {
          bot.sendMessage(chatID, "❌ Format salah. Gunakan angka 0-23. Contoh: /set_stop_on 6", "");
        }
      }
      else if (cmd.startsWith("/set_stop_off"))
      {
        int jamInput = cmd.substring(14).toInt();
        if (jamInput >= 0 && jamInput <= 23)
        {
          stopOffHour = jamInput;
          bot.sendMessage(chatID, "✅ Jadwal STOP KONTAK MATI di-set ke pukul: " + String(stopOffHour) + ":00", "");
        }
        else
        {
          bot.sendMessage(chatID, "❌ Format salah. Gunakan angka 0-23. Contoh: /set_stop_off 18", "");
        }
      }

      // 3. KONTROL MODE & STATUS
      else if (cmd == "/status")
      {
        DateTime now = rtc.now();
        String pesan = "📊 *STATUS SISTEM RUANGAN*\n\n";
        pesan += "⏰ Waktu RTC : " + String(now.hour()) + ":" + String(now.minute()) + "\n";
        pesan += "🌡️ Suhu : " + String(suhu) + " C\n";
        pesan += "💧 Hum : " + String(hum) + " %\n";
        pesan += "💨 CO : " + String(nilaiMQ7) + "\n";
        pesan += "🔥 Api : " + String(apiTerdeteksi ? "YA" : "TIDAK") + "\n\n";
        pesan += "💡 *Jadwal Lampu:* (" + String(modeAutoLampu ? "AUTO" : "MANUAL") + ")\n";
        pesan += " 🟢 ON: " + String(lampOnHour) + ":00 | 🔴 OFF: " + String(lampOffHour) + ":00\n\n";
        pesan += "🔌 *Jadwal Stop Kontak:* (" + String(modeAutoStop ? "AUTO" : "MANUAL") + ")\n";
        pesan += " 🟢 ON: " + String(stopOnHour) + ":00 | 🔴 OFF: " + String(stopOffHour) + ":00";
        bot.sendMessage(chatID, pesan, "Markdown");
      }
      else if (cmd == "/lampu_on")
      {
        modeAutoLampu = false;
        digitalWrite(RELAY_LAMP, LOW);
        bot.sendMessage(chatID, "Lampu MANUAL: ON", "");
      }
      else if (cmd == "/lampu_off")
      {
        modeAutoLampu = false;
        digitalWrite(RELAY_LAMP, HIGH);
        bot.sendMessage(chatID, "Lampu MANUAL: OFF", "");
      }
      else if (cmd == "/stop_on")
      {
        modeAutoStop = false;
        digitalWrite(RELAY_STOP, LOW);
        bot.sendMessage(chatID, "Stop Kontak MANUAL: ON", "");
      }
      else if (cmd == "/stop_off")
      {
        modeAutoStop = false;
        digitalWrite(RELAY_STOP, HIGH);
        bot.sendMessage(chatID, "Stop Kontak MANUAL: OFF", "");
      }
      else if (cmd == "/auto")
      {
        modeAutoLampu = true;
        modeAutoStop = true;
        bot.sendMessage(chatID, "Mode AUTO aktif untuk Lampu & Stop Kontak", "");
      }
      else if (cmd == "/manual")
      {
        modeAutoLampu = false;
        modeAutoStop = false;
        bot.sendMessage(chatID, "Mode MANUAL aktif untuk Lampu & Stop Kontak", "");
      }
      else if (cmd == "/reset")
      {
        alarmLock = false;
        alarmState = NORMAL;
        digitalWrite(RELAY_STOP, HIGH);
        notifWarningSent = false;
        notifCriticalSent = false;
        beepStart();
        bot.sendMessage(chatID, "Alarm reset. Sistem kembali normal.", "");
      }
      else if (cmd == "/help")
      {
        String hlp = "📋 *Daftar Perintah Jadwal Waktu:*\n\n";
        hlp += "👉 `/set_lampu_on Jam` (Contoh: `/set_lampu_on 18`)\n";
        hlp += "👉 `/set_lampu_off Jam` (Contoh: `/set_lampu_off 6`)\n";
        hlp += "👉 `/set_stop_on Jam` (Contoh: `/set_stop_on 7`)\n";
        hlp += "👉 `/set_stop_off Jam` (Contoh: `/set_stop_off 17`)\n\n";
        hlp += "Ketik `/auto` untuk mengaktifkan jadwal di atas.";
        bot.sendMessage(chatID, hlp, "Markdown");
      }
    }
  }
}

void beepStart()
{
  for (int i = 0; i < 3; i++)
  {
    tone(BUZZER_PIN, 2500);
    delay(150);
    noTone(BUZZER_PIN);
    delay(150);
  }
}