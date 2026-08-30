# P2000 on an Elecrow ESP32-S3 5-inch Display

This PlatformIO project displays the eight most recent P2000 alerts on the 800×480 screen and provides a settings page hosted on the ESP32.

## Building and flashing

1. Open this folder with PlatformIO in VS Code.
2. Connect the ESP32-S3 via USB and select **Upload**.
3. After the first startup, connect to the `P2000-display` Wi-Fi network and open `http://192.168.77.1`.
4. Save the Wi-Fi settings, regions, and capcodes. The API URL uses Alarmeringdroid by default. The page will then be accessible through the IP address assigned to the ESP32 by your router.

## Alarmeringdroid API

By default, the firmware uses:

```text
https://beta.alarmeringdroid.nl/api2/find/
```

The endpoint returns an object containing a `meldingen` array. There are three region filters. An alert is displayed when its `regioid` matches at least one selected region. The **None** option disables only that particular selection. If all three filters are set to **None**, no alerts are displayed. Capcodes are matched against each alert’s `capcodes` array.

An alert includes fields such as `datum`, `tijd`, `tekstmelding`, `regioid`, `capstring`, and a `capcodes` array. The firmware can also read the alternative field names `time`, `text`, `body`, and `description`.

## Controls

Swipe up on the screen to view older alerts and swipe down to return. The counter in the top-right corner shows the current position within the list.

Tap **CONFIG** in the top-right corner to select the three regions directly on the screen. Tap `<` or `>` next to a region. In the same menu, you can switch **Display mode** between the alert list and **Information screen – 1 channel**.

This mode displays one alert per screen, including the region, location, and full alert text. Swipe left or right to move between alerts. The selected alert remains on screen when the data is refreshed and changes automatically only when the API returns a new alert. Tap **Save** when finished.

The GT911 touch controller uses I²C on GPIO19 (SDA) and GPIO20 (SCL).

The display configuration is designed for the Elecrow **DIS07050H/CrowPanel 5.0"** (ESP32-S3-WROOM-1-N4R8, 800×480). The RGB pin configuration matches the Arduino_GFX configuration confirmed by Elecrow users. However, you should still compare it with the pinout if your board is a different revision.

HTTPS certificates are deliberately not validated (`setInsecure()`), allowing public endpoints to work without certificate management. Preferably, use a trusted API and only a network you control.Arduino_GFX-configuratie. Vergelijk dit alsnog met de pinout als uw print een andere revisie heeft.


