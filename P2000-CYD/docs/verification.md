# Verificatie — firmware 1.0.1, 6 september 2026

- Beide PlatformIO-omgevingen bouwen succesvol.
- UI-hosttests geslaagd: schermgrenzen, knopoverlap, opslaan/annuleren, tekstomloop, archiefgrens en wifi-scanstatussen, inclusief vertraagde start en retries.
- Parser-hosttests geslaagd: regio-, capcode- en dienstenfilters, numerieke velden, foutieve/te grote JSON, behoud van meldingen, logvolgorde en geheugenlimiet.
- Een openbaar API-antwoord met 30 meldingen is door de productieparser verwerkt binnen de limiet van 64 KiB.
- Gerenderde lijst-, detail-, configuratie- en toetsenbordschermen visueel bekeken. De hosttekenlaag benadert afgeronde hoeken als rechthoeken.

## Hardwaretest: C-variant met GT911

- Firmware via USB geflasht; flashverificatie geslaagd.
- GT911 gedetecteerd op adres `0x5d`; touch is 180 graden gedraaid.
- Wifi-scan geslaagd: 21 accesspoints gevonden, maximaal 8 zichtbaar in de lijst.
- Routerverbinding hersteld na de scan.
- API: HTTP 200 in 1.495 ms, JSON OK, twee gefilterde meldingen.
- Eén eerdere aanvraag gaf een read-time-out (-11); automatische herhaling is ingeschakeld.
- Normale herstart: HTTP 200 in 1.519 ms. Wifi en API na 20 en 50 seconden nog actief.
- De gebruiker heeft bevestigd dat de firmware werkt.

De R-variant is alleen bouwgetest. SD-logging en formatteren zijn niet afzonderlijk op hardware geverifieerd.
