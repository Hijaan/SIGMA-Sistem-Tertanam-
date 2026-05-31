# SIGMA v1.0

### Smart IoT for Gas, teMperature, and electrical Automation

SIGMA v1.0 merupakan sistem Smart Room berbasis ESP32 yang dirancang untuk monitoring suhu, kelembapan, gas berbahaya, serta deteksi api secara real-time menggunakan platform IoT Blynk.

Sistem ini mengintegrasikan sensor lingkungan, alarm darurat, relay otomatis, dan notifikasi cloud untuk meningkatkan keamanan ruangan secara otomatis dan efisien.

---

# 🚀 Fitur Utama

## 🌡 Monitoring Lingkungan

* Monitoring suhu menggunakan DHT22
* Monitoring kelembapan ruangan
* Monitoring gas menggunakan sensor MQ7
* Deteksi api menggunakan sensor KY-026

---

## 🚨 Sistem Keamanan

* Alarm buzzer bertingkat:

  * Mode Warning
  * Mode Critical
* Deteksi gas berbahaya
* Deteksi api secara realtime
* StopKontak yang terhubung dengan kipas otomatis saat kondisi bahaya
* Sistem reset aman (alarm tidak dapat di-reset jika kondisi masih berbahaya)

---

## 💡 Otomasi

* Kontrol lampu menggunakan relay
* Kontrol stopkontak yang terhubung dengan kipas menggunakan relay
* Penjadwalan lampu otomatis menggunakan RTC DS3231
* Kontrol manual melalui aplikasi Blynk

---

## 📡 Integrasi IoT

* Monitoring realtime menggunakan Blynk
* Push notification saat kondisi darurat
* Remote monitoring melalui smartphone
* Kontrol aktuator dari jarak jauh

---

## ⚙️ Sistem Embedded

* Arsitektur non-blocking
* PWM passive buzzer menggunakan LEDC ESP32
* Moving average filter untuk stabilisasi sensor MQ7
* WiFi modem sleep untuk efisiensi daya

---

# 🛠 Komponen yang Digunakan

| Komponen       | Fungsi                   |
| -------------- | ------------------------ |
| ESP32          | Mikrokontroler utama     |
| DHT22          | Sensor suhu & kelembapan |
| MQ7            | Sensor gas               |
| KY-026         | Sensor api               |
| DS3231         | RTC (Real Time Clock)    |
| Passive Buzzer | Alarm suara              |
| Relay Module   | Kontrol lampu & exhaust  |

---

# 🔌 Konfigurasi Pin

| Perangkat     | GPIO    |
| ------------- | ------- |
| DHT22         | GPIO 21 |
| MQ7           | GPIO 34 |
| Flame Sensor  | GPIO 35 |
| RTC SDA       | GPIO 21 |
| RTC SCL       | GPIO 22 |
| Buzzer        | GPIO 25 |
| Relay Lampu   | GPIO 26 |
| Relay Exhaust | GPIO 27 |

---

# 📱 Virtual Pin Blynk

| Virtual Pin | Fungsi             |
| ----------- | ------------------ |
| V0          | Suhu               |
| V1          | Kelembapan         |
| V2          | Nilai MQ7          |
| V3          | Kontrol Lampu      |
| V4          | Kontrol Exhaust    |
| V7          | Test Alarm         |
| V8          | Status Deteksi Api |
| V9          | Reset Alarm        |

---

# 📂 Struktur Proyek

```text
SIGMA-v1.0/
│
├── src/
│   └── main.cpp
│
├── include/
│
├── lib/
│
├── docs/
│
├── test/
│
├── platformio.ini
│
└── README.md
```

---

# ⚡ Alur Sistem

```text
Pembacaan Sensor
        ↓
Filtering Data
        ↓
Logika Alarm
        ↓
Kontrol Relay & Buzzer
        ↓
Notifikasi Blynk
```

---

# 🔥 Logika Alarm

## Warning Gas

Aktif ketika nilai MQ7 melewati threshold warning.

## Critical Gas

Aktif ketika nilai MQ7 melewati threshold critical:

* Exhaust fan menyala
* Alarm aktif
* Notifikasi dikirim ke Blynk

## Deteksi Api

Aktif ketika sensor KY-026 mendeteksi api:

* Exhaust menyala
* Lampu dimatikan
* Alarm aktif
* Notifikasi darurat dikirim

---

# 🧠 Konsep Embedded System yang Digunakan

* State-based alarm system
* Non-blocking timing menggunakan millis()
* Moving average filtering
* Fail-safe reset logic
* PWM tone generation
* IoT telemetry
* Sistem monitoring realtime

---

# 📋 Bug dan Kendala Saat Ini (SIGMA v1.0)

## False Trigger pada Sensor Api

Sensor KY-026 masih berpotensi menghasilkan deteksi palsu akibat noise dan sensitivitas cahaya.
Rencana Perbaikan:

* Debounce software
* Stable sampling
* Kalibrasi threshold

---

## Warmup Sensor MQ7

Sensor MQ7 membutuhkan waktu warmup sebelum pembacaan menjadi stabil.

Rencana Perbaikan:

* Sistem warmup saat startup
* Mode kalibrasi sensor

---

## RTC Masih Auto Adjust

RTC masih melakukan penyesuaian waktu setiap board restart/upload.
Rencana Perbaikan:

* Menggunakan rtc.lostPower()
* Menonaktifkan auto-adjust setelah setup awal

---

## Kontrol Manual dan Otomatis Lampu Masih Konflik

Kontrol manual lampu dari Blynk masih dapat tertimpa oleh scheduler otomatis RTC.

Rencana Perbaikan:

* Sistem manual override

---

# Pengembangan SIGMA v2.0

Fitur yang direncanakan:

* Integrasi dengan telegram untuk notifikasi darurat

---

# Tim Pengembang

Kelompok 6 — Sistem Tertanam

1. Hasbi Ma’arif
2. Luthfy Dian Afrizal
3. Syallomitha Clara Halelia Wangania
4. Syahwa Novianti Eka Nugraha

Institut Teknologi Kalimantan