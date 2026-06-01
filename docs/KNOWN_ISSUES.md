# Known Issues

## Captive Portal Popup

Some phones may not automatically open the captive portal page.

Fallback:

`http://192.168.4.1/`

The current firmware handles common phone probes and DNS hijack, but captive portal UI is ultimately controlled by the phone OS.

## WiFi Scan Stability

Automatic WiFi scan on page load was avoided because it can destabilize the setup AP.

Use the explicit `Scan WiFi` button on the portal page.

## HTTP Server Stack

The captive portal previously rebooted because the HTTP server task stack was too small.

Current fix:

- `max_uri_handlers = 16`
- `stack_size = 8192`

Keep this setting unless a later memory audit proves it can be reduced.

## Chinese Fonts

The UI currently uses English text only.

Reason:

- Earlier Chinese labels rendered as garbled text
- Current LVGL font path is stable for English

Future Chinese UI should add a known-good CJK font and verify memory usage.

## Windows Path Sensitivity

The vendor folder contains non-ASCII path segments.

Recommended development clone path:

`C:\espbuild\esp32s3-35inch-lvgl-xiaozhi`

## Weather Location

Weather location is currently hardcoded to Shanghai.

Future work:

- Add portal field for city/location
- Store latitude/longitude in NVS
- Allow weather refresh after setting changes
