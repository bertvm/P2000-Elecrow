# P2000 on an Elecrow ESP32-S3 5-inch display

[Nederlands](README.md) | **English**

This PlatformIO project displays the eight latest Dutch P2000 emergency alerts on an 800×480 Elecrow screen and hosts a configuration page on the ESP32.

The screen uses a dark SquareLine-inspired design rendered directly with Arduino_GFX. The P2000 API, Wi-Fi, capacitive touch and SD-card logging therefore remain available without requiring an LVGL export for the firmware build.

![P2000 display layout](docs/lvgl-layout.svg)

To reduce visible flicker, the renderer tracks the contents of the header, each alert card and the footer. An unchanged API response causes no redraw; when data changes, only the affected area is refreshed. A full redraw occurs only when switching screens.

## Build and flash

1. Open this repository in Visual Studio Code with PlatformIO.
2. Connect the ESP32-S3 over USB.
3. Select the `elecrow_esp32s3_5in` environment and choose **Upload**.
4. On first boot, connect to the `P2000-display` Wi-Fi network and open `http://192.168.77.1`.
5. Save the Wi-Fi credentials, regions and optional capcode filters.

## Alarmeringdroid API

The default endpoint is:

```text
https://beta.alarmeringdroid.nl/api2/find/
```

The endpoint returns an object containing a `meldingen` array. An alert is displayed when its `regioid` matches at least one of the three selected regions. Selecting **None** disables that particular region slot. If all three slots are **None**, no nationwide fallback feed is shown. Optional capcodes are matched against each alert's `capcodes` array.

The firmware reads fields including `datum`, `tijd`, `tekstmelding`, `regioid`, `capstring` and `capcodes`. It also accepts alternative names such as `time`, `text`, `body` and `description`.

## Controls

Swipe up to view older alerts and down to return. Tap **CONFIG** in the top-right corner to change the three regions, display mode, Wi-Fi and SD-card logging.

Use **Scan WiFi** to search for a network, or **Manual** to enter the SSID and password using the touchscreen keyboard. The password remains visible while typing so uppercase letters, digits and symbols can be checked. The initial access point remains available at `192.168.77.1` when no saved Wi-Fi configuration exists.

The display mode can be switched between the alert list and the single-alert information screen. In information mode, swipe left or right to move between alerts. The selected alert remains visible after a refresh and changes automatically only when a new alert arrives.

When SD-card logging is enabled, new alerts are appended to `/p2000.csv`. **Format SD** erases the card after an additional confirmation and creates a FAT filesystem suitable for logging.

The GT911 touch controller uses GPIO19 for SDA and GPIO20 for SCL.

## SquareLine Studio

The editable 800×480 layout is available at [`squareline/P2000.spj`](squareline/P2000.spj). Keep `P2000.sll` in the same directory when opening the project in SquareLine Studio 1.6.2.

The firmware currently reproduces this design directly in Arduino_GFX. SquareLine changes are not automatically transferred to `src/main.cpp`; update the matching sizes, colors and text in the firmware, or migrate to a complete LVGL export later.

## Hardware notes

The configuration targets the Elecrow **DIS07050H/CrowPanel 5.0-inch** with ESP32-S3-WROOM-1-N4R8 and an 800×480 RGB display. Verify the pinout when using another hardware revision.

HTTPS certificate validation is disabled with `setInsecure()` to avoid certificate maintenance on the embedded device. Use a trusted API endpoint and a network you control.
