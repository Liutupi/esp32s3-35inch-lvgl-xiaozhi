# Project Handoff

## Current Goal

This is the fixed baseline after bringing up the main/sub UI, touch, WiFi setup, time sync, and weather sync.

The next sessions should continue from this repository instead of re-discovering the board from scratch.

## Firmware Location

Main ESP-IDF project:

`1-示例程序Demo/ESP-IDF/3.5inch_ESP32-S3_LVGL`

Key files:

- `main/main.cpp`: board startup, LVGL task, UI startup, network startup, time/weather startup
- `main/app_ui.cpp`: current home/apps UI, time/weather/quote panels, swipe page switching
- `main/app_ui.h`: UI update API
- `main/app_net.cpp`: WiFi AP, captive portal, DNS hijack, NVS credential memory
- `main/app_net.h`: network startup API
- `main/app_time_weather.cpp`: SNTP time sync and weather HTTP sync
- `main/app_time_weather.h`: time/weather startup API
- `main/CMakeLists.txt`: component requirements and source registration

## Verified Runtime Baseline

Last verified board setup:

- Port: `COM13`
- Board boots and shows LVGL UI
- Touch works
- Left/right swipe switches pages
- Setup AP appears as `xiaozhi-setup`
- Captive portal works after increasing HTTP server stack
- WiFi credential save and reconnect path is implemented
- Time/weather task starts after WiFi connects

Last known important boot logs:

```text
DHCP portal options: dns=192.168.4.1 uri=http://192.168.4.1/
setup AP started: SSID=xiaozhi-setup auth=open channel=1 ip=192.168.4.1
```

## Build Notes

Prefer cloning/building from an ASCII path:

```powershell
cd C:\espbuild\esp32s3-35inch-lvgl-xiaozhi\1-示例程序Demo\ESP-IDF\3.5inch_ESP32-S3_LVGL
idf.py set-target esp32s3
idf.py build
idf.py -p COM13 flash monitor
```

The original vendor folder contains Chinese path segments. It works for file reference, but ESP-IDF on Windows can fail or behave inconsistently in non-ASCII/long paths.

## What Is Intentionally Not Tracked

- `sdkconfig`: can contain local settings and should stay local
- `build/`: generated output
- `managed_components/`: fetched by ESP-IDF from dependency metadata
- flash tools, installer archives, generated `.bin` files

## Current Product Shape

Home page:

- Large clock
- Date/week display
- Weather area with reserved animation space
- Quote/status area with enough room for longer text

Apps page:

- Network Radio placeholder
- XiaoZhi AI placeholder
- Daily Quote placeholder
- Calendar placeholder
- Photo Album placeholder

The UI avoids Chinese text for now because the current font path is English-only and earlier Chinese rendering caused garbled text.
