# Configuratiescherm

De firmware opent vier tegels: **Meldingen**, **Weergave**, **WiFi** en **SD-kaart & archief**.

- Meldingen: tabs voor drie regio's, vijf diensten en capcodes. Tik een regio aan en veeg of gebruik Vorige/Volgende. Capcodes hebben een numeriek toetsenbord met komma en wissen.
- Weergave: selecteer de lijst of het infoscherm en wijzig het API-interval in stappen van 15 seconden.
- WiFi: verbindingsstatus, IP en signaal, netwerken zoeken of handmatige invoer. Het wachtwoord kan worden getoond of verborgen. Rond de invoer af en tik daarna op **Opslaan / Verbinden**.
- SD-kaart: status en vrije ruimte, logging, direct Archief openen en formatteren met bevestiging. Een leeg of onbereikbaar archief geeft nu een zichtbare melding in dit menu.

Wijzigingen worden als concept bewaard en pas na **Opslaan** toegepast. Terug navigeert naar het bovenliggende menu. Annuleren of het startmenu verlaten waarschuwt bij niet-opgeslagen wijzigingen. Formatteren is een afzonderlijke, onomkeerbare actie na bevestiging; Annuleren kan een format niet terugdraaien.

De interface wordt rechtstreeks met Arduino_GFX getekend in `include/config_ui.h`. De bestaande SquareLine-bestanden bevatten nog de oude configuratiepagina en zijn **geen export van dit nieuwe menu**.

## Testen na upload

1. Open Config en controleer de vier tegels.
2. Wijzig een regio/dienst, verlaat via Annuleren en verwerp. Controleer dat de oude instelling behouden is.
3. Wijzig opnieuw, sla op en herstart. Controleer dat de instelling bewaard is.
4. Test capcodes invoeren/wissen en de lijst-/infoselectie.
5. Test WiFi-scan, handmatige invoer en zichtbaar/verborgen wachtwoord; verbind via Opslaan / Verbinden.
6. Open een bestaand SD-archief vanuit Config; test ook zonder kaart en met een leeg log. Formatteer alleen een testkaart waarvan de inhoud mag verdwijnen.
