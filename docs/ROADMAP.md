# Roadmap

## Phase 1: Fixed Baseline

Done in the current baseline:

- Main page and secondary apps page
- Stable English LVGL UI
- Touch tap and swipe navigation
- WiFi setup AP and captive portal
- Remember 3 WiFi networks
- System-wide STA WiFi
- SNTP time sync
- Weather sync

## Phase 2: UI Polish

Next UI work:

- Add weather animation area on the home page
- Add weather icon/animation mapping
- Add location selection in WiFi setup portal
- Add configurable daily quote text
- Add better offline/reconnect indicators

## Phase 3: Audio Foundation

Before network audio features:

- Verify ES8311 schematic pins
- Bring up I2S + codec init
- Verify speaker output
- Add local audio self-test screen or log command
- Keep audio path independent from UI until stable

## Phase 4: Network Radio

Target behavior:

- Use saved WiFi
- Show station list in UI
- Play direct MP3 streams first
- Add stream reconnect and buffering state
- Add volume/mute controls
- Add station metadata later

Avoid mixing too many stream formats at once. Bring up one reliable MP3 stream first, then extend.

Detailed plan: `docs/NETWORK_RADIO_PLAN.md`

## Phase 5: XiaoZhi AI

Target behavior:

- Use saved WiFi
- Use verified audio capture/playback path
- Match the upstream XiaoZhi protocol exactly
- Add wake/listen/speaking state to UI
- Add Opus encode/decode after basic transport is stable

## Phase 6: Daily Quote

Target behavior:

- Local fallback quote
- Optional network quote sync
- Long quote wrapping on the home page
- NVS cache for last successful quote

## Phase 7: Calendar

Target behavior:

- Local date/week/month view
- Optional online holiday/lunar/calendar sync
- Reminder/event placeholders

## Phase 8: Photo Album

Target behavior:

- Local image storage first
- JPEG decode and scaled display
- Swipe slideshow
- Optional phone upload or network sync later

## Phase 9: Repository Hygiene

Keep this repository useful for future sessions:

- Update docs when pins or runtime behavior are confirmed
- Keep `sdkconfig` private/local
- Commit small working milestones
- Build/flash before marking a hardware feature done
