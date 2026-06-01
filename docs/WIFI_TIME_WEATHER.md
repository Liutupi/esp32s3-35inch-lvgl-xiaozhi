# WiFi, Time, and Weather Sync

## WiFi Setup Model

The board starts a phone setup hotspot:

- SSID: `xiaozhi-setup`
- Auth: open, no password
- Setup IP: `192.168.4.1`
- DNS hijack: all A-record queries point to `192.168.4.1`
- Captive portal URL: `http://192.168.4.1/`

The phone connects to `xiaozhi-setup`, then the portal page is opened automatically when the phone OS supports captive portal detection.

If the phone does not pop up the browser, open this URL manually:

`http://192.168.4.1/`

## Scan and Save Flow

The portal does not auto-scan on first load. This is intentional because automatic scanning can destabilize the setup AP on some phones.

Flow:

1. Phone joins `xiaozhi-setup`.
2. Open `http://192.168.4.1/`.
3. Press `Scan WiFi`.
4. Select nearby SSID.
5. Enter WiFi password.
6. Save.

The firmware stores up to 3 credential sets in NVS under namespace `wifi_cfg`:

- `ssid0` / `pass0`
- `ssid1` / `pass1`
- `ssid2` / `pass2`

Latest successful credential moves to slot 0.

## System-Wide WiFi

The STA connection is shared by later features:

- Time sync
- Weather sync
- Network radio
- XiaoZhi AI
- Daily quote download
- Calendar sync
- Photo album cloud/local transfer

## Time Sync

`main/app_time_weather.cpp` starts SNTP after WiFi is available.

NTP servers:

- `ntp.aliyun.com`
- `pool.ntp.org`

Timezone:

- `CST-8`

The UI is updated through:

- `app_ui_set_time(...)`
- `app_ui_set_network_status(...)`

## Weather Sync

Weather is currently fetched from Open-Meteo without an API key.

Current location is hardcoded to Shanghai:

```text
latitude=31.2304
longitude=121.4737
timezone=Asia/Shanghai
```

Current endpoint shape:

```text
http://api.open-meteo.com/v1/forecast?latitude=31.2304&longitude=121.4737&current=temperature_2m,weather_code&timezone=Asia%2FShanghai
```

The UI is updated through:

- `app_ui_set_weather(...)`

Next improvement:

- Add location setting in the phone setup portal
- Store location in NVS
- Add weather animation assets for sunny/cloudy/rain/thunder/fog
- Refresh weather on a timer and after WiFi reconnect
