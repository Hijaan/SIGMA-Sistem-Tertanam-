# SIGMA v2.0

### Smart IoT for Gas, teMperature, and electrical Automation

SIGMA v2.0 merupakan pembaruan dari sistem Smart Room yang kini berbasis **ESP32-S3**. Sistem ini dirancang untuk monitoring suhu, kelembapan, gas berbahaya, serta deteksi api secara *real-time* dengan integrasi langsung ke **Telegram Bot** dan dukungan pembaruan nirkabel (OTA).

Sistem ini mengintegrasikan sensor lingkungan, alarm darurat, relay otomatis (untuk lampu dan stop kontak/kipas), serta notifikasi *Telegram* untuk meningkatkan keamanan ruangan secara otomatis dan efisien tanpa bergantung pada platform pihak ketiga yang berbayar.

---

# 🚀 Fitur Utama

## 🌡 Monitoring Lingkungan

* Monitoring suhu menggunakan DHT22
* Monitoring kelembapan ruangan
* Monitoring gas Karbon Monoksida menggunakan sensor MQ7
* Deteksi api menggunakan sensor Flame (KY-026)

---

## 🚨 Sistem Keamanan

* Alarm buzzer bertingkat:
  * **Mode Warning:** Peringatan gas mulai tinggi.
  * **Mode Critical:** Deteksi gas berbahaya atau api.
* Stop Kontak keamanan yang memutus/mengambil alih arus secara otomatis saat kondisi bahaya.
* Sistem *Fail-safe Lock* (Alarm dan Relay akan terkunci saat bahaya dan hanya bisa di-reset manual melalui Telegram).
* Push notification darurat instan via Telegram.

---

## 💡 Otomasi

* Kontrol Lampu menggunakan relay.
* Kontrol Stop Kontak (kipas/exhaust) menggunakan relay.
* Penjadwalan otomatis menyala/mati menggunakan RTC DS3231 (Mendukung rentang waktu melintasi tengah malam).
* Sistem pendingin otomatis (Stop kontak otomatis menyala jika suhu > 35°C).

---

## 📡 Integrasi IoT & Kendali Jarak Jauh

* Integrasi **Telegram Bot** untuk kontrol penuh (Tanpa Blynk).
* Kontrol mode **Auto / Manual** secara independen untuk Lampu dan Stop Kontak.
* Pengecekan status sensor *real-time* dengan perintah `/status`.
* **OTA (Over-The-Air) Updates:** Mendukung pembaruan *firmware* via WiFi tanpa kabel USB.

---

## ⚙️ Sistem Embedded

* Arsitektur *non-blocking* menggunakan `millis()`.
* Moving average filter untuk stabilisasi pembacaan sensor MQ7.
* Manajemen status (*State-based*) alarm system.
* Sinkronisasi RTC cerdas (Hanya mengatur waktu jika RTC kehilangan daya).

---

# 🛠 Komponen yang Digunakan

| Komponen       | Fungsi                   |
| -------------- | ------------------------ |
| ESP32-S3       | Mikrokontroler utama     |
| DHT22          | Sensor suhu & kelembapan |
| MQ7            | Sensor gas CO            |
| Flame Sensor   | Sensor api               |
| DS3231         | RTC (Real Time Clock)    |
| Active Buzzer  | Alarm suara              |
| Relay 2 Channel| Kontrol lampu & stop kontak |

---

# 🔌 Konfigurasi Pin (Updated for ESP32-S3)

| Perangkat         | GPIO    | Keterangan |
| ----------------- | ------- | ---------- |
| Buzzer            | GPIO 4  | OUTPUT     |
| MQ7 (Gas CO)      | GPIO 5  | INPUT (ADC)|
| Flame Sensor      | GPIO 6  | INPUT      |
| DHT22             | GPIO 7  | DATA       |
| RTC SDA           | GPIO 8  | I2C        |
| RTC SCL           | GPIO 9  | I2C        |
| Relay Lampu       | GPIO 10 | OUTPUT     |
| Relay Stop Kontak | GPIO 20 | OUTPUT     |

---

# 📱 Daftar Perintah Telegram Bot

Alih-alih menggunakan Virtual Pin Blynk, SIGMA v2.0 dikontrol sepenuhnya melalui perintah Telegram:

| Perintah | Fungsi |
| :--- | :--- |
| `/status` | Menampilkan ringkasan data sensor dan jadwal |
| `/set_lampu_on [Jam]` | Mengubah jadwal lampu menyala |
| `/set_lampu_off [Jam]`| Mengubah jadwal lampu mati |
| `/set_stop_on [Jam]`  | Mengubah jadwal stop kontak menyala |
| `/set_stop_off [Jam]` | Mengubah jadwal stop kontak mati |
| `/lampu_on` / `/lampu_off` | Kontrol manual lampu (Override RTC) |
| `/stop_on` / `/stop_off` | Kontrol manual stop kontak (Override RTC) |
| `/auto` | Mengembalikan kontrol lampu & stop kontak ke jadwal RTC |
| `/manual` | Menonaktifkan jadwal RTC |
| `/reset` | Membuka kunci sistem (*unlock*) setelah alarm bahaya |

---

# 📂 Struktur Proyek

```text
SIGMA-v2.0/
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
├── platformio.ini
│
└── README.md
