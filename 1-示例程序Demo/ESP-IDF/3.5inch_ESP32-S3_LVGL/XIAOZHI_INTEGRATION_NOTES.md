# XiaoZhi Integration Notes

## Current Status

- XiaoZhi is integrated into the existing `lvgl_example` project instead of replacing the main firmware.
- Main UI, radio, weather/time, WiFi setup portal, and audio startup are preserved.
- The Apps screen contains a XiaoZhi entry with a conversation-style XiaoZhi page.
- WiFi provisioning still uses the project's own `xiaozhi-setup` SoftAP and web portal.
- The web portal can save optional XiaoZhi OTA URL, WebSocket URL, and token into NVS.
- ES8311 audio now opens in input/output mode so XiaoZhi can capture microphone audio.
- Radio and XiaoZhi use an audio ownership lock so they do not grab I2S/ES8311 at the same time.
- XiaoZhi startup now fetches config before taking audio ownership, and failed WebSocket attempts clean up resources.
- Talk button has debounce protection to avoid repeated connection attempts.

## Verified

- Project builds successfully with ESP-IDF 5.5.
- Firmware flashes as `lvgl_example`, not as a standalone XiaoZhi firmware.
- Boot logs show WiFi reconnects and ES8311 duplex audio initializes.
- Entering the XiaoZhi page is stable.
- Failed XiaoZhi OTA/WebSocket connection no longer repeatedly retries or locks the system.

## Known Issues

- XiaoZhi server OTA/WebSocket connection can still time out on the current network/service path.
- The current XiaoZhi UI is a stable embedded LVGL8 version, not yet a full pixel-perfect classic XiaoZhi UI.
- Audio path is basic duplex capture/playback; echo cancellation, VAD, wake word, and resampling are not fully integrated yet.
- TTS playback sample-rate switching is implemented, but end-to-end server conversation still needs live validation after connection succeeds.

## Next Steps

1. Fix XiaoZhi server connectivity:
   - Confirm whether the default OTA endpoint is reachable from the board network.
   - If needed, configure manual WebSocket URL and token through `xiaozhi-setup`.
   - Verify activation flow and token handling with the chosen XiaoZhi backend.

2. Validate full conversation flow:
   - Confirm OTA config response.
   - Confirm WebSocket hello/session negotiation.
   - Confirm mic Opus upload.
   - Confirm STT, LLM, TTS JSON events update the XiaoZhi page.
   - Confirm decoded TTS audio plays through ES8311.

3. Improve audio quality:
   - Add resampling instead of switching I2S sample rate repeatedly if needed.
   - Add AEC/VAD/wake-word support after basic manual conversation is stable.
   - Tune mic gain and output volume on the actual hardware.

4. Polish the classic XiaoZhi UI:
   - Replace the temporary embedded face with a closer classic XiaoZhi expression system.
   - Add clearer activation/error prompts.
   - Add connection status and manual configuration hints without blocking the main UI.

5. Regression test the original project functions:
   - Radio playback after leaving XiaoZhi.
   - WiFi setup portal save/reconnect.
   - Weather/time updates.
   - Back navigation and gestures.
