# smart-home-esp8266
Code ini untuk mengendalikan lampu menggunakan Remote / Menggunakan HTTP
Alat-alat yang dibutuhkan dalam repositori ini yaitu:
1. Esp8266 (di project ini menggunakan wemos d1 mini)
2. DS3231 (RTC)
3. TSOP Infrared Receiver

Library yang digunakan yaitu:
1. ArduinoJson
2. SPIFFS (Untuk menyimpan konfigurasi)
3. RTCLib
4. IRremote.hpp
5. ESP8266WebServer


Pin Yang digunakan
1. D1 (SCL) -> terhubung dengan SCL DS3231
2. D2 (SDA) -> terhubung dengan SDA DS3231
3. D3       -> terhubung dengan OUT TSOP
4. D4 - D8  -> terhubung dengan relay
