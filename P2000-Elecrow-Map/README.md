# P2000 Elecrow Map

Afzonderlijk PlatformIO-project voor het Elecrow ESP32-S3 5-inch display. Na het aantikken van een melding opent een detailpagina met locatie en OpenStreetMap-kaart.

## Werking

1. Tik op een meldingskaart, of op de melding in het infoscherm.
2. De firmware combineert meldingstekst, plaats/regio en Nederland tot een zoekopdracht.
3. Nominatim zet deze gebruikersgestuurde zoekopdracht om naar coördinaten.
4. De ESP32 laadt uitsluitend de negen kaarttegels rondom het gevonden punt en tekent een marker.
5. Tik linksboven op **Terug** om naar de meldingen te gaan.

Een FAT-geformatteerde SD-kaart is verplicht. Geocoderingen staan in `/geocache.tsv`; kaarttegels in `/maptiles`. Bestaande resultaten worden hergebruikt.

## Bouwen en uploaden

Open uitsluitend de map `P2000-Elecrow-Map` in Visual Studio Code en PlatformIO. Gebruik:

```bash
pio run -e elecrow_esp32s3_5in_map
pio run -e elecrow_esp32s3_5in_map -t upload
```

Het project gebruikt Arduino_GFX 1.6.0, ArduinoJson 7 en PNGdec. De eerste kaart kan langer duren doordat PNG-tegels via HTTPS worden geladen. Een eerder bekeken gebied komt vanaf SD.

## Beperkingen

- P2000 bevat niet altijd een gestructureerd adres. Bij onvolledige of met operationele codes vervuilde tekst kan `Locatie niet gevonden` verschijnen of de marker onnauwkeurig zijn.
- De kaart is statisch: er is geen zoomen of verschuiven.
- HTTPS-certificaten worden, net als in het basisproject, niet gevalideerd.
- De firmware compileert, maar geocodering, tegelkleuren, SD-cache en touch moeten op het echte paneel worden gevalideerd.

## Gebruiksvoorwaarden

De kaart toont `© OpenStreetMap contributors` (het ©-teken wordt grafisch getekend). De firmware gebruikt een herkenbare User-Agent, geocodeert uitsluitend na aanraking, houdt minimaal 1,1 seconde tussen nieuwe geocode-oproepen en cachet resultaten. Tegels worden alleen voor de zichtbare kaart geladen en blijvend op SD gecachet. Controleer bij distributie opnieuw de actuele [Nominatim Usage Policy](https://operations.osmfoundation.org/policies/nominatim/) en [Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/).
