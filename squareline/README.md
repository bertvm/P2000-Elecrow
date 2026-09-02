# SquareLine Studio-layout

Open `P2000.spj` in SquareLine Studio 1.6.2. Laat `P2000.sll` in dezelfde map staan.

Gebruik de bestanden in deze map `squareline/`; de oudere `P2000.spj` in de projecthoofdmap is niet bijgewerkt.

## Nieuwe configuratielayout

Het project opent op `scrConfig` met vier tegels. Er zijn 15 bewerkbare ontwerpschermen:

- `scrMessages`, `scrArchive`: kaarten met afzonderlijke BRW/POL/AMB-labels, plaatsnamen, metadata en meldingstekst.
- `scrConfig`: tegelmenu.
- `scrRegions`, `scrServices`, `scrCapcodes`: de drie meldingentabs.
- `scrRegionPicker`, `scrCapcodeKeyboard`: regioselectie en numerieke invoer.
- `scrDisplay`: weergavemodus en interval.
- `scrWifi`, `scrWifiScan`, `scrWifiInput`: netwerkbeheer en toetsenbord.
- `scrSdArchive`, `scrFormatConfirm`: logging, archief en formatbevestiging.
- `scrUnsaved`: waarschuwing voor niet-opgeslagen wijzigingen.

Dit is een bewerkbaar ontwerp met voorbeeldgegevens, geen werkende LVGL-export van de firmware. Navigatie-events, filterlogica, toetsenbordacties en hardwarekoppelingen zijn niet in het SquareLine-project geïmplementeerd. Selecteer de schermen in de editor om ze te bewerken. De firmware gebruikt FreeSans Bold; hier zijn ingebouwde Montserrat-fonts gebruikt om externe fontafhankelijkheden te vermijden.

JSON, unieke namen/ID's, enkelregelige teksten en schermgrenzen zijn gecontroleerd. Openen en renderen in SquareLine Studio moet nog lokaal worden bevestigd. De beschikbare hoeveelheid schermen/objecten kan afhankelijk zijn van je SquareLine-licentie.

Projectinstellingen:

- Board: DIS07050H - ESP32 5inch HMI Display 800x480 RGB - ESP-IDF
- Resolutie: 800 × 480 landscape
- Kleurformaat: RGB565, 16-bit
- LVGL: 8.3.11

De PlatformIO-firmware gebruikt dezelfde vormgeving rechtstreeks via Arduino_GFX. Wijzigingen in SquareLine worden daarom niet automatisch in `src/main.cpp` verwerkt; neem gewijzigde afmetingen, kleuren en teksten daar eveneens over of exporteer later een volledige LVGL-interface.

De SD-archiefmodus hergebruikt tijdens runtime `scrMessages` en de drie bestaande meldingskaarten. De firmware vervangt daarbij dynamisch de titel door **ARCHIEF**, het sectielabel door **SD-ARCHIEF** en de voettekst door de positie in het logbestand. Hiervoor zijn geen extra SquareLine-objecten nodig en blijven `P2000.spj` en `P2000.sll` compatibel met het eerder geteste project.
