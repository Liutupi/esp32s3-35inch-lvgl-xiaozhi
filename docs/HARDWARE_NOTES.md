# Hardware Notes

## Board

Known board from the current bring-up:

- ESP32-S3
- 3.5-inch ST77922 QSPI LCD
- 8 MB PSRAM observed in boot log
- USB-Serial/JTAG flashing
- Verified flashing port: `COM13`

## Display

Observed firmware configuration:

- LCD controller: ST77922
- Source resolution: 320 x 480
- LVGL runtime orientation: landscape, 480 x 320 after software rotation
- UI currently uses LVGL and avoids Chinese glyph rendering until a stable CJK font path is added

Relevant firmware area:

- `components/esp_lcd_st77922`
- `main/main.cpp`
- `main/app_ui.cpp`

## Touch

Observed touch bring-up:

- Touch integrated with ST77922/TDDI path
- I2C address: `0x55`
- SDA: GPIO38
- SCL: GPIO39
- RST: GPIO48
- INT: GPIO47
- Max touch points: 5

Important behavior:

- Tap menu/back works
- Manual left/right swipe detection works
- Swipe drives LVGL page switching

Relevant firmware area:

- `components/esp_lcd_touch_st77922`
- `main/main.cpp`

## Audio Notes For Later Work

Audio is not fully integrated in the current baseline.

Candidate information found from vendor examples and board notes, to verify against schematic before coding:

- Codec: ES8311
- MCLK: GPIO17
- BCLK: GPIO18
- DIN: GPIO16
- DOUT: GPIO15
- WS/LRCK: GPIO21
- Codec control I2C: GPIO38/GPIO39
- PA enable: GPIO1, likely low-enable

Before adding network radio or XiaoZhi AI audio:

1. Verify ES8311 pins against `5-原理图_Schematic`.
2. Confirm PA enable polarity on the real board.
3. Bring up a local WAV/MP3 or sine output test first.
4. Only then wire radio streaming and XiaoZhi Opus paths.

## Included Vendor References

Useful local reference folders:

- `2-规格书_Specification`
- `3-尺寸图_Structure_Diagram`
- `4-数据手册_DataSheet`
- `5-原理图_Schematic`
- `6-用户手册_User_Manual`
