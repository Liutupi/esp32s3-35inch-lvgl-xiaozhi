# 3.5inch ESP32-S3 LVGL XiaoZhi Base

This repository freezes the current working baseline for the 3.5-inch ESP32-S3 board project.

Current firmware target:

`1-示例程序Demo/ESP-IDF/3.5inch_ESP32-S3_LVGL`

Current verified board port:

- ESP32-S3 3.5-inch ST77922 QSPI LCD board
- LVGL landscape UI, 480 x 320 after software rotation
- Touch working, including menu/back tap and left/right swipe navigation
- WiFi captive portal setup AP: `xiaozhi-setup`
- Stores up to 3 WiFi credentials in NVS and auto-reconnects
- System-wide WiFi is used by time sync and weather sync
- SNTP time sync and Open-Meteo weather sync are wired into the UI

Build and flash from an ASCII path when possible. Windows ESP-IDF can be fragile with long or non-ASCII paths.

```powershell
git clone https://github.com/Liutupi/esp32s3-35inch-lvgl-xiaozhi.git C:\espbuild\esp32s3-35inch-lvgl-xiaozhi
cd C:\espbuild\esp32s3-35inch-lvgl-xiaozhi\1-示例程序Demo\ESP-IDF\3.5inch_ESP32-S3_LVGL
idf.py set-target esp32s3
idf.py build
idf.py -p COM13 flash monitor
```

Important project notes:

- [Project handoff](docs/PROJECT_HANDOFF.md)
- [WiFi, time, and weather sync](docs/WIFI_TIME_WEATHER.md)
- [Hardware notes](docs/HARDWARE_NOTES.md)
- [Roadmap](docs/ROADMAP.md)
- [Known issues](docs/KNOWN_ISSUES.md)

Reference design images are kept at the repository root:

- `主界面.png`
- `副页面.png`

Vendor documentation kept in this repository:

- `2-规格书_Specification`
- `3-尺寸图_Structure_Diagram`
- `4-数据手册_DataSheet`
- `5-原理图_Schematic`
- `6-用户手册_User_Manual`

Generated build output, local `sdkconfig`, firmware binaries, flash tools, and large installer/tool archives are intentionally ignored.
