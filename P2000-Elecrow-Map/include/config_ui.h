// Touchscreen configuration. Edits live in draft until explicitly saved.
Settings configDraft;
enum ConfigPage { HOME_PAGE, ALERT_PAGE, DISPLAY_PAGE, WIFI_PAGE, SD_PAGE, REGION_PAGE, CAPS_PAGE, LEAVE_PAGE };
ConfigPage configPage = HOME_PAGE;
uint8_t alertTab = 0, regionSlot = 0, regionStart = 0;
String configNotice;
void scanWifiNetworks();
void openWifiInput(const String &ssid);
void drawSdFormatConfirmScreen();
void drawConfigScreen();

bool configDirty() {
  if (configDraft.ssid != cfg.ssid || configDraft.password != cfg.password ||
      configDraft.capcodes != cfg.capcodes || configDraft.intervalSec != cfg.intervalSec ||
      configDraft.ticker != cfg.ticker || configDraft.sdLogging != cfg.sdLogging) return true;
  for (int i=0;i<3;++i) if (configDraft.regions[i] != cfg.regions[i]) return true;
  for (int i=0;i<5;++i) if (configDraft.services[i] != cfg.services[i]) return true;
  return false;
}
void configButton(int x,int y,int w,int h,const String &label,bool selected=false) {
  gfx->fillRoundRect(x,y,w,h,10,selected?UI_SURFACE_2:UI_SURFACE);
  gfx->drawRoundRect(x,y,w,h,10,selected?UI_ACCENT:UI_MUTED);
  drawHeading(label,x+16,y+16,w-32);
}
void configText(const String &text,int x,int y) {
  gfx->setFont(); gfx->setTextSize(1); gfx->setTextColor(UI_MUTED);
  gfx->setCursor(x,y); gfx->print(text);
}
void beginConfig() {
  configDraft=cfg; configPage=HOME_PAGE; configNotice="";
  screenMode=CONFIG; drawConfigScreen();
}
void applyConfig() {
  bool wifiChanged=configDraft.ssid!=cfg.ssid || configDraft.password!=cfg.password;
  cfg=configDraft; saveSettings();
  if (cfg.sdLogging) initSdCard();
  if (wifiChanged && cfg.ssid.length()) { WiFi.disconnect(); connectWifi(); }
  nextPoll=0; archiveMode=false; alarmCount=0; firstVisibleAlarm=0; infoAlarmIndex=0; invalidateMessageUi();
  configNotice=wifiChanged?"Opgeslagen. WiFi verbinden...":"Instellingen opgeslagen";
}
void drawConfigScreen() {
  invalidateMessageUi(); gfx->fillScreen(UI_BG); gfx->setTextWrap(false);
  const char *titles[]={"Configuratie","Meldingen","Weergave","WiFi","SD-kaart & archief","Kies regio","Capcodes","Niet opgeslagen"};
  configButton(16,8,105,50,"Terug"); drawHeading(titles[configPage],145,21,620);
  gfx->fillRect(0,67,800,3,UI_ACCENT);
  if (configPage==HOME_PAGE) {
    uint8_t regions=0, services=0;
    for (auto &r:configDraft.regions) if(r.length()) ++regions;
    for (bool s:configDraft.services) if(s) ++services;
    configButton(24,90,364,130,"Meldingen");
    configText(String(regions)+" regio's | "+String(services)+" diensten",42,158);
    configButton(412,90,364,130,"Weergave");
    configText(configDraft.ticker?"Infoscherm":"Meldingenlijst",430,158);
    configButton(24,240,364,130,"WiFi");
    configText(WiFi.status()==WL_CONNECTED?fitText(WiFi.SSID(),40):"Niet verbonden",42,310);
    configButton(412,240,364,130,"SD-kaart & archief");
    configText(configDraft.sdLogging?"Logging ingeschakeld":"Logging uitgeschakeld",430,310);
  } else if(configPage==ALERT_PAGE) {
    const char *tabs[]={"Regio's","Diensten","Capcodes"};
    for(int i=0;i<3;++i) configButton(24+i*254,82,244,50,tabs[i],alertTab==i);
    if(alertTab==0) for(int i=0;i<3;++i)
      configButton(24,150+i*75,752,60,"Regio "+String(i+1)+": "+(configDraft.regions[i].length()?regionName(configDraft.regions[i]):"Geen"));
    if(alertTab==1) {
      const char *names[]={"BRW - Brandweer","POL - Politie","AMB - Ambulance","Lifeliner / MMT","Overig"};
      for(int i=0;i<5;++i) configButton(24+(i%2)*384,150+(i/2)*76,368,60,String(configDraft.services[i]?"[x] ":"[ ] ")+names[i],configDraft.services[i]);
    }
    if(alertTab==2) {
      configButton(24,155,752,70,configDraft.capcodes.length()?configDraft.capcodes:"Capcodes invoeren");
      configText("Leeg = alle capcodes binnen je regio- en dienstenfilters",40,255);
      configText("Meerdere capcodes scheiden met een komma",40,280);
    }
  } else if(configPage==REGION_PAGE) {
    for(int i=0;i<4 && regionStart+i<REGION_COUNT;++i) {
      int idx=regionStart+i;
      configButton(24,82+i*64,752,56,idx?REGIONS[idx].name:"Geen",configDraft.regions[regionSlot]==REGIONS[idx].id);
    }
    configButton(24,345,364,48,"Vorige"); configButton(412,345,364,48,"Volgende");
  } else if(configPage==CAPS_PAGE) {
    configText(fitText(configDraft.capcodes,110),24,88);
    const char *keys[]={"1","2","3","4","5","6","7","8","9","0",",","Wis"};
    for(int i=0;i<12;++i) configButton(24+(i%6)*128,125+(i/6)*80,112,64,keys[i]);
    configButton(24,300,364,60,"Alles wissen"); configButton(412,300,364,60,"Gereed");
  } else if(configPage==DISPLAY_PAGE) {
    configButton(24,100,364,140,"Meldingenlijst",!configDraft.ticker);
    configText("Drie meldingen tegelijk",42,175);
    configButton(412,100,364,140,"Infoscherm",configDraft.ticker);
    configText("Een melding met meer detail",430,175);
    configText("Verversingsinterval (seconden)",24,275);
    configButton(24,305,120,60,"- 15"); configButton(160,305,480,60,String(configDraft.intervalSec)); configButton(656,305,120,60,"+ 15");
  } else if(configPage==WIFI_PAGE) {
    configText(WiFi.status()==WL_CONNECTED?"Verbonden met "+fitText(WiFi.SSID(),65):
      WiFi.status()==WL_CONNECT_FAILED?"Verbinding mislukt: controleer SSID en wachtwoord":
      WiFi.status()==WL_NO_SSID_AVAIL?"Netwerk niet gevonden":"Niet verbonden / opnieuw verbinden...",24,98);
    configText("IP: "+WiFi.localIP().toString()+" | Signaal: "+String(WiFi.RSSI())+" dBm",24,126);
    configText("Ingesteld SSID: "+fitText(configDraft.ssid,70),24,158);
    configButton(24,205,364,80,"Netwerken zoeken"); configButton(412,205,364,80,"Handmatig instellen");
    configText("WiFi-invoer wordt pas actief na Opslaan / Verbinden.",24,322);
  } else if(configPage==SD_PAGE) {
    configText(sdReady?"SD-kaart gereed":"SD-kaart niet beschikbaar",24,88);
    if(sdReady) configText("Vrij: "+String((unsigned long)((SD.totalBytes()-SD.usedBytes())/1048576))+" MB",420,88);
    configButton(24,120,752,64,configDraft.sdLogging?"[x] Meldingen opslaan":"[ ] Meldingen opslaan",configDraft.sdLogging);
    configButton(24,205,752,64,"Archief openen");
    configText("BEHEER - formatteren wist alle bestanden",24,302);
    configButton(24,328,752,56,"Kaart formatteren");
  } else if(configPage==LEAVE_PAGE) {
    drawHeading("Wijzigingen zijn nog niet opgeslagen",24,145,752);
    configButton(24,240,364,80,"Blijven bewerken"); configButton(412,240,364,80,"Wijzigingen verwerpen");
  }
  configText(fitText(configNotice,120),24,401);
  configButton(24,420,364,52,"Annuleren");
  configButton(412,420,364,52,configPage==WIFI_PAGE?"Opslaan / Verbinden":"Opslaan",true);
}
void configBack() {
  if(configPage==HOME_PAGE) {
    if(configDirty()) configPage=LEAVE_PAGE;
    else {screenMode=MESSAGES; drawScreen(); return;}
  } else if(configPage==REGION_PAGE || configPage==CAPS_PAGE) configPage=ALERT_PAGE;
  else configPage=HOME_PAGE;
  drawConfigScreen();
}
void handleConfigTap(int x,int y) {
  if(y>=420) {
    if(x>=412) { applyConfig(); if(configPage==LEAVE_PAGE) {screenMode=MESSAGES;drawScreen();return;} }
    else {if(configDirty()) configPage=LEAVE_PAGE;else {screenMode=MESSAGES;drawScreen();return;}}
  } else if(y<=65 && x<=125) {configBack();return;}
  else if(configPage==HOME_PAGE && y>=90 && y<370) {
    configPage=y<220?(x<400?ALERT_PAGE:DISPLAY_PAGE):(x<400?WIFI_PAGE:SD_PAGE);
    if(configPage==SD_PAGE) initSdCard();
  } else if(configPage==ALERT_PAGE) {
    if(y>=82 && y<132 && x>=24 && x<776) alertTab=min(2,(x-24)/254);
    else if(alertTab==0 && y>=150 && y<360) {regionSlot=min(2,(y-150)/75);regionStart=0;configPage=REGION_PAGE;}
    else if(alertTab==1 && y>=150 && y<362) {int i=((y-150)/76)*2+(x>=408); if(i<5) configDraft.services[i]=!configDraft.services[i];}
    else if(alertTab==2 && y>=155 && y<225) configPage=CAPS_PAGE;
  } else if(configPage==REGION_PAGE) {
    if(y>=82 && y<338) {int i=regionStart+(y-82)/64; if(i<REGION_COUNT) {configDraft.regions[regionSlot]=REGIONS[i].id;configPage=ALERT_PAGE;}}
    else if(y>=345 && y<393) {if(x<400) regionStart=regionStart>=4?regionStart-4:0;else if(regionStart+4<REGION_COUNT) regionStart+=4;}
  } else if(configPage==CAPS_PAGE) {
    if(y>=125 && y<285 && x>=24 && x<776) {int i=((y-125)/80)*6+(x-24)/128;if(i<10) configDraft.capcodes+=(i==9?'0':char('1'+i));else if(i==10) configDraft.capcodes+=',';else if(configDraft.capcodes.length()) configDraft.capcodes.remove(configDraft.capcodes.length()-1);}
    else if(y>=300 && y<360) {if(x<400) configDraft.capcodes="";else configPage=ALERT_PAGE;}
  } else if(configPage==DISPLAY_PAGE) {
    if(y>=100 && y<240) configDraft.ticker=x>=400;
    else if(y>=305 && y<365) {if(x<144 && configDraft.intervalSec>=30) configDraft.intervalSec-=15;else if(x>=656 && configDraft.intervalSec<3600) configDraft.intervalSec+=15;}
  } else if(configPage==WIFI_PAGE && y>=205 && y<285) {
    if(x<400) {screenMode=WIFI_SCAN;scanWifiNetworks();} else openWifiInput(configDraft.ssid); return;
  } else if(configPage==SD_PAGE) {
    if(y>=120 && y<184) configDraft.sdLogging=!configDraft.sdLogging;
    else if(y>=205 && y<269) {
      if(configDirty()) configNotice="Sla wijzigingen eerst op voordat je het archief opent.";
      else if(loadArchivePage(0)) {archiveMode=true;screenMode=MESSAGES;invalidateMessageUi();drawScreen();return;}
      else configNotice=statusLine;
    } else if(y>=328 && y<384) {screenMode=SD_FORMAT_CONFIRM;drawSdFormatConfirmScreen();return;}
  } else if(configPage==LEAVE_PAGE && y>=240 && y<320) {
    if(x<400) configPage=HOME_PAGE;else {configDraft=cfg;screenMode=MESSAGES;drawScreen();return;}
  }
  drawConfigScreen();
}
