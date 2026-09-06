# ESP32-2432S032 hardware

De poort gebruikt de fabrikantdemo en het MCU V1.0-schema uit het [ESP32-2432S032-documentatiearchief](https://github.com/kpeeem/3.2inch_ESP32-2432S032). Dit bepaalt de configuratie; het is geen bevestiging dat de aangesloten print exact revisie V1.0 heeft.

| Functie | GPIO |
| --- | --- |
| TFT ST7789 DC / CS | 2 / 15 |
| TFT SPI SCK / MOSI / MISO | 14 / 13 / 12 |
| Achtergrondverlichting | 27, hoog = aan |
| XPT2046 CS / IRQ (R) | 33 / 36 |
| XPT2046 SPI | Gedeeld met TFT via HSPI |
| GT911 SDA / SCL (C) | 33 / 32 |
| SD CS / SCK / MOSI / MISO | 5 / 18 / 23 / 19, VSPI |
| RGB-led (uit bij opstart) | 4 / 16 / 17, actief laag |

De TFT en de resistieve touch gebruiken **dezelfde SPIClass** met afzonderlijke chip-selects en SPI-transacties. Zo herstelt elke gebruiker zijn eigen klokinstelling. De SD-kaart heeft een aparte SPI-bus.

De GT911 wordt zoals in het fabrikantvoorbeeld op de reeds geïnitialiseerde controller benaderd. Adressen `0x5d` en `0x14` worden gecontroleerd; de port herschrijft de GT911-configuratie niet. GPIO25 wordt in het fabrikantvoorbeeld als reset genoemd, maar de meegeleverde Touch_GT911-driver voert in `begin()` geen reset uit. De port volgt dat gedrag.

## Bronnen

De touchcoördinaten worden na de controllermapping 180 graden gedraaid:
`x = 319 - x`, `y = 239 - y`. Dit geldt voor beide touchvarianten.
De resistieve kalibratie houdt rekening met deze eindtransformatie.

- [Displayvoorbeeld: ST7789, rotatie 1, backlight 27](https://github.com/kpeeem/3.2inch_ESP32-2432S032/blob/main/1-Demo/Demo_Arduino/3_3-1_TFT_HelloWorld/HelloWorld/HelloWorld.ino)
- [Resistieve touch: pinnen en kalibratie](https://github.com/kpeeem/3.2inch_ESP32-2432S032/blob/main/1-Demo/Demo_Arduino/3_3-4_TFT-LVGL-Widgets-Resistance%20touch/LvglWidgets-Resistance%20touch/touch.h)
- [Capacitieve touch: pinnen en rotatie](https://github.com/kpeeem/3.2inch_ESP32-2432S032/blob/main/1-Demo/Demo_Arduino/3_3-5_TFT-LVGL-Widgets_Capacitive%20touch-gt911/LvglWidgets_Capacitive_gt911/touch.h)
- [MCU-schema met SD-aansluiting](https://github.com/kpeeem/3.2inch_ESP32-2432S032/blob/main/5-Schematic/ESP32-2432S032-MCU-V1.0%20.jpg)
- [XPT2046-driver v1.4](https://github.com/PaulStoffregen/XPT2046_Touchscreen/tree/d57f64c8b5f2bc5b8d10d121550806eeff7b06d9)

## Overgenomen applicatie

Bron: het [Elecrow-basisproject](../../src/main.cpp) in deze repository. De API, filterlogica, Preferences-configuratie en CSV-archiefcode zijn daarop gebaseerd. Deze map is zelfstandig en bevat geen buildafhankelijkheid van het Elecrow-project.

De RGB-driver, de S3/PSRAM-buildinstellingen en het 800×480-touchmenu zijn vervangen. Nieuwe CYD-code staat in `include/board.h`, `include/compact_ui.h` en `include/bounded_json.h`.
