# Network Radio Plan

## Goal

Build a stable network radio app for the 3.5-inch ESP32-S3 side screen.

The first usable version should:

- Reuse the existing saved WiFi connection.
- Show a small station list on the Apps page or a Radio detail page.
- Play one known-good direct MP3 stream.
- Show clear states: idle, connecting, buffering, playing, reconnecting, and error.
- Support play/pause, previous/next station, volume up/down, and mute.

## Suggested Milestones

### Milestone 0: Compilable App Skeleton

- Added `app_radio.cpp` and `app_radio.h`.
- Added a Radio detail page with station, state, metadata, and transport buttons.
- Wired the Radio tile to open the detail page.
- Added a background radio task and command queue.
- Added HTTP MP3 stream probing without audio decode yet.

### Milestone 1: Audio Bring-Up

- Confirm ES8311 I2C address and I2S pins from schematic.
- Add a small audio component for codec init, speaker enable, I2S TX, volume, and mute.
- Verify speaker output with a generated tone before adding network playback.
- Keep this test independent from LVGL so audio failures are easy to isolate.

Current hardware confirmation:

- ES8311 responds at I2C address `0x18`.
- I2C control bus is shared with touch: SDA `IO38`, SCL `IO39`.
- Amplifier enable is `IO1`, active low.
- I2S pins from board notes: codec DIN/playback data `IO15`, codec DOUT/record data `IO16`, MCLK `IO17`, BCLK `IO18`, LRCK `IO21`.
- `app_audio` now initializes I2S TX, opens ES8311, and exposes PCM write plus a low-volume test tone.
- The Radio page plays a short test tone once on the first `Play` tap, then continues with stream probing.

### Milestone 2: Minimal Radio Playback

- Add `app_radio.cpp` and `app_radio.h`.
- Use `esp_http_client` to open one MP3 stream over HTTP first.
- Decode MP3 with a proven ESP-IDF-compatible decoder.
- Feed decoded PCM into I2S with a small ring buffer.
- Add automatic stop/retry when WiFi disconnects or the stream stalls.

### Milestone 3: Radio UI

- Turn the current `Radio` tile into a clickable detail page.
- Show station name, current state, stream quality, and simple transport controls.
- Keep the first UI in English until the CJK font path is stable.
- Update UI from the radio task through small setter APIs, following the existing time/weather pattern.

### Milestone 4: Station List

- Store a local station table in firmware first.
- Start with 3 to 5 direct MP3 stations.
- Add NVS-backed favorite/current station later.
- Avoid HLS/AAC/redirect-heavy streams until MP3 playback is reliable.

### Milestone 5: Resilience

- Add reconnect backoff.
- Add buffer health reporting.
- Add a short watchdog-style silence timeout.
- Pause radio cleanly before XiaoZhi voice playback once the AI audio path is added.

## Proposed Code Shape

- `main/app_audio.cpp`: codec, I2S, volume, mute, PCM output.
- `main/app_radio.cpp`: station list, stream fetch, decoder, buffering, reconnect logic.
- `main/app_ui.cpp`: Radio page and UI update functions.
- `main/CMakeLists.txt`: add new radio/audio source files and decoder dependency.

## First Stations

Use direct MP3 HTTP streams during bring-up. Keep a station disabled if it requires HTTPS redirects, HLS playlists, AAC, or browser-only headers.

Station table fields:

- `name`
- `url`
- `codec`
- `bitrate_hint`
- `enabled`

## Risks

- Audio pin or codec settings may differ from the board documentation.
- HTTPS radio streams can consume more memory and fail on certificates.
- Some public stations change URLs or reject embedded clients.
- LVGL and audio tasks must avoid blocking each other.
- PSRAM pressure should be watched once decoder buffers are added.

## Done Criteria For First Usable Version

- Board connects to saved WiFi.
- User opens Radio page.
- Pressing play starts one station within a few seconds.
- Audio continues for at least 15 minutes without UI freezing.
- Disconnecting WiFi shows an error or reconnecting state instead of hanging.
- Volume and mute work without rebooting.
