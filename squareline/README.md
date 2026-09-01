# SquareLine Studio-layout

Open `P2000.spj` in SquareLine Studio 1.6.2. Laat `P2000.sll` in dezelfde map staan.

Projectinstellingen:

- Board: DIS07050H - ESP32 5inch HMI Display 800x480 RGB - ESP-IDF
- Resolutie: 800 × 480 landscape
- Kleurformaat: RGB565, 16-bit
- LVGL: 8.3.11

De PlatformIO-firmware gebruikt dezelfde vormgeving rechtstreeks via Arduino_GFX. Wijzigingen in SquareLine worden daarom niet automatisch in `src/main.cpp` verwerkt; neem gewijzigde afmetingen, kleuren en teksten daar eveneens over of exporteer later een volledige LVGL-interface.
