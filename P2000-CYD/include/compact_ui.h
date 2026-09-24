#pragma once
#include <vector>

// Every button is drawn and hit-tested from the same rectangle.
enum Action { BACK, CONFIGURE, OPEN_PAGE, SAVE, CANCEL_EDIT, REGION_PICK, REGION_SET,
  REGION_PREV, REGION_NEXT, SERVICE, DISPLAY_MODE, INTERVAL, SD_TOGGLE,
  ARCHIVE, FORMAT_REQUEST, FORMAT_EXECUTE, SCAN, MANUAL, SSID_PICK,
  EDIT_SSID, EDIT_PASS, EDIT_CAPS, EDIT_HOST, EDIT_TOPIC, FEED_MODE, API_PORT, KEY, ERASE, SHIFT, SYMBOLS, SPACE,
  KEY_DONE, PREVIOUS, NEXT, DETAIL, DETAIL_PAGE, DISCARD, CALIBRATE };
struct Button { int x,y,w,h; Action action; int value; };
Button buttons[48]; uint8_t buttonCount = 0;
Settings configDraft;
enum ConfigPage { HOME_PAGE, ALERT_PAGE, DISPLAY_PAGE, WIFI_PAGE, SD_PAGE,
  REGION_PAGE, CAPS_PAGE, LEAVE_PAGE };
ConfigPage configPage = HOME_PAGE;
uint8_t alertTab = 0, regionSlot = 0, regionStart = 0;
String configNotice;
int editField = 0, wifiPage = 0, detailLine = 0;
bool detailOpen = false;
uint8_t detailIndex = 0;
void drawConfigScreen();
void drawWifiScanScreen();
void drawWifiInputScreen();
void drawSdFormatConfirmScreen();
void drawScreen();
void startConfigurationAp();
void connectWifi();
void renderCurrent();

String cleanText(String s) {
  // Built-in font is single-byte. Transliterate common Dutch UTF-8 accents.
  const char *from[] = {"ë","é","è","ê","ï","í","ö","ó","ü","ú","ä","á","à","Ë","É","Ï","Ö","Ü","–","—","’","‘","“","”"};
  const char *to[] = {"e","e","e","e","i","i","o","o","u","u","a","a","a","E","E","I","O","U","-","-","'","'","\"","\""};
  for (size_t i=0; i<sizeof(from)/sizeof(from[0]); ++i) s.replace(from[i],to[i]);
  s.replace("\r", "");
  return s;
}
void label(String text, int x, int y, int width, uint8_t scale=1, uint16_t color=UI_TEXT) {
  gfx->setFont(); gfx->setTextSize(scale); gfx->setTextColor(color); gfx->setTextWrap(false);
  gfx->setCursor(x,y); gfx->print(fitText(cleanText(text),width/(6*scale)));
}
void button(int x,int y,int w,int h,const String &text,Action action,int value=0,bool selected=false) {
  if (buttonCount < sizeof(buttons)/sizeof(buttons[0])) buttons[buttonCount++]={x,y,w,h,action,value};
  gfx->fillRoundRect(x,y,w,h,5,selected?UI_SURFACE_2:UI_SURFACE);
  gfx->drawRoundRect(x,y,w,h,5,selected?UI_ACCENT:UI_MUTED);
  String t=fitText(cleanText(text),(w-8)/6);
  label(t,x+(w-t.length()*6)/2,y+(h-8)/2,w-8,1,selected?UI_ACCENT:UI_TEXT);
}
std::vector<String> wrapped(String text, unsigned columns) {
  text=cleanText(text);
  std::vector<String> lines;
  while (text.length()) {
    unsigned n=min(columns,text.length());
    int newline=text.indexOf('\n');
    if (newline>=0 && (unsigned)newline<n) n=newline;
    else if (n<text.length()) {
      int space=text.lastIndexOf(' ',n);
      if (space>0) n=space;
    }
    lines.push_back(text.substring(0,n));
    text.remove(0,n);
    if (text.startsWith(" ") || text.startsWith("\n")) text.remove(0,1);
  }
  if(lines.empty()) lines.push_back("");
  return lines;
}
void pageHeader(const String &title) {
  buttonCount=0; gfx->fillScreen(UI_BG);
  button(4,4,52,26,"Terug",BACK);
  label(title,64,10,252,1,UI_ACCENT);
  gfx->drawFastHLine(0,34,320,UI_ACCENT);
}
void configFooter() {
  label(configNotice,6,195,308,1,UI_MUTED);
  button(4,210,152,28,"Annuleren",CANCEL_EDIT);
  button(164,210,152,28,"Opslaan",SAVE,0,true);
}
bool configDirty() {
  if(configDraft.ssid!=cfg.ssid || configDraft.password!=cfg.password ||
     configDraft.capcodes!=cfg.capcodes || configDraft.ticker!=cfg.ticker ||
     configDraft.sdLogging!=cfg.sdLogging || configDraft.intervalSec!=cfg.intervalSec ||
     configDraft.feedMode!=cfg.feedMode || configDraft.serverHost!=cfg.serverHost ||
     configDraft.apiPort!=cfg.apiPort || configDraft.mqttPort!=cfg.mqttPort ||
     configDraft.mqttTopic!=cfg.mqttTopic || configDraft.mqttUser!=cfg.mqttUser ||
     configDraft.mqttPass!=cfg.mqttPass) return true;
  for(int i=0;i<3;++i) if(configDraft.regions[i]!=cfg.regions[i]) return true;
  for(int i=0;i<5;++i) if(configDraft.services[i]!=cfg.services[i]) return true;
  return false;
}
void beginConfig() {
  configDraft=cfg; configPage=HOME_PAGE; configNotice="";
  screenMode=CONFIG; drawConfigScreen();
}
void applyConfig() {
  bool wifiChanged=cfg.ssid!=configDraft.ssid || cfg.password!=configDraft.password;
  cfg=configDraft; saveSettings();
  if(cfg.sdLogging) initSdCard();
  if(wifiChanged) {
    WiFi.disconnect(false,false);
    if(cfg.ssid.length()) connectWifi(); else startConfigurationAp();
  }
  alarmCount=0; infoAlarmIndex=0; firstVisibleAlarm=0; archiveMode=false; detailOpen=false;
  detailLine=0; nextPoll=0; nextMqttRetry=0;
  if(cfg.feedMode!=FEED_MQTT && mqttClient.connected()) mqttClient.disconnect();
  invalidateMessageUi();
  configNotice="Instellingen opgeslagen";
}
void drawConfigScreen() {
  invalidateMessageUi();
  const char *titles[]={"Configuratie","Meldingen","Weergave","WiFi","SD-kaart / archief","Kies regio","Capcodes","Niet opgeslagen"};
  pageHeader(titles[configPage]);
  if(configPage==HOME_PAGE) {
    button(6,44,150,48,"Meldingen",OPEN_PAGE,ALERT_PAGE);
    button(164,44,150,48,"Weergave",OPEN_PAGE,DISPLAY_PAGE);
    button(6,100,150,48,"WiFi",OPEN_PAGE,WIFI_PAGE);
    button(164,100,150,48,"SD / archief",OPEN_PAGE,SD_PAGE);
#if defined(CYD_TOUCH_RESISTIVE)
    button(6,157,308,29,"Touch kalibreren",CALIBRATE);
#else
    label("ESP32-2432S032C | GT911",8,165,304,1,UI_MUTED);
#endif
  } else if(configPage==ALERT_PAGE) {
    button(4,40,74,28,"Regio's",OPEN_PAGE,100,alertTab==0);
    button(82,40,74,28,"Dienst",OPEN_PAGE,101,alertTab==1);
    button(160,40,74,28,"Caps",OPEN_PAGE,102,alertTab==2);
    button(238,40,78,28,"Bron",OPEN_PAGE,103,alertTab==3);
    if(alertTab==0) for(int i=0;i<3;++i)
      button(6,76+i*38,308,32,String(i+1)+": "+regionName(configDraft.regions[i]),REGION_PICK,i);
    if(alertTab==1) {
      const char *names[]={"Brandweer","Politie","Ambulance","Lifeliner / MMT","Overig"};
      for(int i=0;i<5;++i) button(6+(i%2)*158,76+(i/2)*38,150,32,String(configDraft.services[i]?"[x] ":"[ ] ")+names[i],SERVICE,i,configDraft.services[i]);
    }
    if(alertTab==2) {
      button(6,82,308,40,configDraft.capcodes.length()?configDraft.capcodes:"Capcodes invoeren",EDIT_CAPS);
      label("Leeg: alle capcodes in gekozen regio's.",8,140,304,1,UI_MUTED);
      label("Meerdere codes scheiden met een komma.",8,156,304,1,UI_MUTED);
    }
    if(alertTab==3) {
      button(6,76,100,32,"Cloud",FEED_MODE,FEED_CLOUD,configDraft.feedMode==FEED_CLOUD);
      button(110,76,100,32,"Lok. API",FEED_MODE,FEED_LOCAL_API,configDraft.feedMode==FEED_LOCAL_API);
      button(214,76,100,32,"MQTT",FEED_MODE,FEED_MQTT,configDraft.feedMode==FEED_MQTT);
      button(6,114,308,32,configDraft.serverHost.length()?configDraft.serverHost:"Pi-IP invoeren",EDIT_HOST);
      if(configDraft.feedMode==FEED_CLOUD) label("Internet: Alarmeringdroid-API",8,154,304,1,UI_MUTED);
      else if(configDraft.feedMode==FEED_LOCAL_API) {
        label("P2000-server /api2/find/",8,150,304,1,UI_MUTED);
        button(6,166,70,28,"-10",API_PORT,-10);
        label(String(configDraft.apiPort),124,174,80,1);
        button(244,166,70,28,"+10",API_PORT,10);
      } else {
        button(6,152,308,32,"Topic: "+(configDraft.mqttTopic.length()?configDraft.mqttTopic:DEFAULT_MQTT_TOPIC),EDIT_TOPIC);
      }
    }
  } else if(configPage==REGION_PAGE) {
    for(int i=0;i<4 && regionStart+i<REGION_COUNT;++i) {
      int idx=regionStart+i;
      button(6,40+i*30,308,27,REGIONS[idx].name,REGION_SET,idx,configDraft.regions[regionSlot]==REGIONS[idx].id);
    }
    button(6,164,150,26,"Vorige",REGION_PREV);
    button(164,164,150,26,"Volgende",REGION_NEXT);
  } else if(configPage==DISPLAY_PAGE) {
    button(6,47,150,48,"Meldingenlijst",DISPLAY_MODE,0,!configDraft.ticker);
    button(164,47,150,48,"Infoscherm",DISPLAY_MODE,1,configDraft.ticker);
    label("Verversing (seconden)",8,112,300,1,UI_MUTED);
    button(6,134,70,38,"-15",INTERVAL,-15);
    label(String(configDraft.intervalSec),124,145,90,2);
    button(244,134,70,38,"+15",INTERVAL,15);
  } else if(configPage==WIFI_PAGE) {
    label(wifiStatusText(),8,44,304);
    label("IP: "+WiFi.localIP().toString(),8,61,304,1,UI_MUTED);
    label("SSID: "+configDraft.ssid,8,81,304);
    button(6,105,150,36,"Netwerken zoeken",SCAN);
    button(164,105,150,36,"Handmatig",MANUAL);
    label("2.4 GHz | "+String(WiFi.RSSI())+" dBm",8,155,300,1,UI_MUTED);
    label("Wijzigingen pas actief na Opslaan.",8,174,300,1,UI_MUTED);
  } else if(configPage==SD_PAGE) {
    label(sdReady?"SD-kaart gereed":"SD-kaart niet beschikbaar",8,44,300);
    button(6,64,308,34,configDraft.sdLogging?"[x] Meldingen opslaan":"[ ] Meldingen opslaan",SD_TOGGLE,0,configDraft.sdLogging);
    button(6,107,308,34,"Archief openen",ARCHIVE);
    button(6,150,308,34,"SD formatteren (wist alles)",FORMAT_REQUEST);
  } else if(configPage==LEAVE_PAGE) {
    label("Wijzigingen zijn niet opgeslagen.",8,69,304);
    button(6,107,308,34,"Verder bewerken",OPEN_PAGE,HOME_PAGE);
    button(6,150,308,34,"Wijzigingen verwerpen",DISCARD);
  }
  configFooter();
}
void drawWifiScanScreen() {
  pageHeader("WiFi-netwerken");
  if(wifiScanning) label("Zoeken...",8,50,304);
  else if(wifiScanResult==WIFI_SCAN_FAILED) label("Scan mislukt; probeer opnieuw",8,50,304,1,UI_FIRE);
  else if(!scannedWifiCount) label("Geen 2.4 GHz-netwerken gevonden",8,50,304);
  else for(int i=0;i<4 && wifiPage+i<scannedWifiCount;++i)
    button(6,40+i*34,308,30,scannedWifiSsids[wifiPage+i],SSID_PICK,wifiPage+i);
  button(6,183,96,30,"Vorige",REGION_PREV);
  button(112,183,96,30,"Handmatig",MANUAL);
  button(218,183,96,30,"Volgende",REGION_NEXT);
}
bool wifiScanStarting=false;
uint8_t wifiScanAttempts=0;
unsigned long wifiScanStartAt=0;
void finishWifiScan(int found) {
  wifiScanResult=found;scannedWifiCount=0;
  if(found>0) for(int i=0;i<found && scannedWifiCount<MAX_WIFI_NETWORKS;++i) {
    String ssid=WiFi.SSID(i); if(!ssid.length()) continue;
    scannedWifiSsids[scannedWifiCount]=ssid;
    scannedWifiRssi[scannedWifiCount++]=WiFi.RSSI(i);
  }
  WiFi.scanDelete();wifiScanning=false;wifiScanStarting=false;
  if(cfg.ssid.length()) connectWifi();
  if(screenMode==WIFI_SCAN) drawWifiScanScreen();
}
void scanWifiNetworks() {
  if(wifiScanning) return;
  screenMode=WIFI_SCAN;wifiScanning=true;scannedWifiCount=0;wifiPage=0;
  wifiScanResult=WIFI_SCAN_RUNNING;wifiScanAttempts=0;wifiScanStarting=true;
  // Disconnect completion is asynchronous. Suppress automatic reconnection
  // and let the driver settle before starting an all-channel scan.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false,false);WiFi.scanDelete();
  wifiScanStartAt=millis()+350;
  drawWifiScanScreen();
}
void updateWifiScan() {
  if(!wifiScanning) return;
  if(wifiScanStarting) {
    if((int32_t)(millis()-wifiScanStartAt)<0) return;
    ++wifiScanAttempts;
    wifiScanResult=WiFi.scanNetworks(true,true,false,300);
    if(wifiScanResult==WIFI_SCAN_FAILED) {
      if(wifiScanAttempts<3) {WiFi.disconnect(false,false);wifiScanStartAt=millis()+500;return;}
      finishWifiScan(WIFI_SCAN_FAILED);return;
    }
    wifiScanStarting=false;
  }
  int found=WiFi.scanComplete();
  if(found!=WIFI_SCAN_RUNNING) finishWifiScan(found);
}
String &editingValue() {
  if(editField==2) return configDraft.capcodes;
  if(editField==3) return configDraft.serverHost;
  if(editField==4) return configDraft.mqttTopic;
  return editField==0?configDraft.ssid:configDraft.password;
}
void drawWifiInputScreen() {
  pageHeader(editField==2?"Capcodes":editField==3?"Lokale server":editField==4?"MQTT-topic":editField==0?"WiFi-naam":"WiFi-wachtwoord");
  String visible=editingValue();
  if(editField==1) {visible=""; for(unsigned i=0;i<editingValue().length();++i) visible+='*';}
  if(visible.length()>49) visible=visible.substring(visible.length()-49);
  label(visible+"_",6,40,308,1,UI_ACCENT);
  const char *normal[]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm"};
  const char *symbols[]={"!@#$%^&*()","-_=+[]{}\\|",";:'\",.<>/?","`~"};
  for(int row=0;row<4;++row) {
    String keys=keyboardSymbols?symbols[row]:normal[row];
    if(keyboardUppercase && !keyboardSymbols) keys.toUpperCase();
    for(unsigned col=0;col<keys.length();++col)
      button(2+col*32,56+row*28,29,25,String(keys[col]),KEY,keys[col]);
  }
  button(3,173,74,28,"Aa",SHIFT);
  button(83,173,74,28,"!?#",SYMBOLS);
  button(163,173,74,28,"Spatie",SPACE);
  button(243,173,74,28,"Wis",ERASE);
  button(3,209,154,28,"Terug",BACK);
  button(163,209,154,28,editField==0?"Wachtwoord >":"Gereed",KEY_DONE,0,true);
}
void openWifiInput(int field) {
  editField=field; keyboardSymbols=field==3; keyboardUppercase=false;
  screenMode=WIFI_INPUT; drawWifiInputScreen();
}
void drawSdFormatConfirmScreen() {
  pageHeader("SD-kaart formatteren");
  label("Alle bestanden op de SD-kaart",8,65,304);
  label("worden definitief gewist.",8,82,304,1,UI_FIRE);
  label("Wil je doorgaan?",8,110,304);
  button(6,156,150,46,"Annuleren",BACK);
  button(164,156,150,46,"Ja, wis alles",FORMAT_EXECUTE,0,true);
}
String alarmSignature(const Alarm &a) {
  return a.id+'\x1f'+a.time+'\x1f'+a.text+'\x1f'+a.place+'\x1f'+a.region+'\x1f'+a.caps;
}
void drawScreen() {
  if(screenMode!=MESSAGES) return;
  Alarm *items=archiveMode?archiveAlarms:alarms;
  int count=archiveMode?archiveAlarmCount:alarmCount;
  int first=archiveMode?archiveFirstVisible:firstVisibleAlarm;
  bool single=detailOpen || (cfg.ticker && !archiveMode);
  int index=detailOpen?detailIndex:cfg.ticker?infoAlarmIndex:first;
  index=count?constrain(index,0,count-1):0;
  String signature=String(single)+String(archiveMode)+String(index)+String(first)+String(detailLine)+
    String((int)WiFi.status())+WiFi.localIP().toString()+String(apiConnected)+statusLine;
  for(int i=0;i<count;++i) signature+=alarmSignature(items[i]);
  if(lastInfoSignature==signature && renderedMessageLayout>=0) return;
  lastInfoSignature=signature; renderedMessageLayout=single?1:0;
  buttonCount=0; gfx->fillScreen(UI_BG);
  label(archiveMode?"ARCHIEF":"P2000",6,7,130,2,UI_ACCENT);
  label(WiFi.status()==WL_CONNECTED?"WiFi":"--",147,10,30,1,UI_MUTED);
  gfx->fillCircle(191,14,4,!apiChecked?UI_MUTED:apiConnected?UI_AMBULANCE:UI_FIRE);
  button(221,3,95,25,detailOpen?"Terug":"Config",detailOpen?BACK:CONFIGURE);
  label(selectedRegionsLabel(51),6,32,308,1,UI_MUTED);
  if(!count) {
    label("Nog geen meldingen",12,72,296,2);
    label(cfg.ssid.length()?statusLine:"Verbind met wifi: P2000-display",12,108,296);
    label(cfg.ssid.length()?"Kies je regio's via Config.":"Open http://192.168.77.1",12,129,296);
    label("Instellingen ook via het touchscreen.",12,159,296,1,UI_MUTED);
  } else if(single) {
    const Alarm &a=items[index];
    gfx->fillRoundRect(4,49,312,146,5,UI_SURFACE);
    label(a.time,10,55,300,1,alarmAccent(a));
    String body=a.text+"\n\nRegio: "+a.region+"\nPlaats: "+a.place+"\nCapcodes: "+a.caps;
    auto lines=wrapped(body,24);
    const int pageLines=6;
    if(detailLine >= (int)lines.size()) detailLine=0;
    for(int row=0;row<pageLines && detailLine+row<(int)lines.size();++row)
      label(lines[detailLine+row],10,72+row*20,300,2);
    button(122,210,76,28,String(detailLine/pageLines+1)+"/"+String((lines.size()+pageLines-1)/pageLines),DETAIL_PAGE,lines.size());
  } else {
    for(int slot=0;slot<2 && first+slot<count;++slot) {
      const Alarm &a=items[first+slot]; int y=49+slot*74;
      gfx->fillRoundRect(4,y,312,70,5,UI_SURFACE);
      gfx->fillRect(4,y+5,3,60,alarmAccent(a));
      label(a.time,12,y+5,222,1,alarmAccent(a));
      const char *services[]={"OVR","BRW","POL","AMB","MMT"};
      label(services[serviceIcon(a)],274,y+5,36,1,alarmAccent(a));
      auto lines=wrapped(a.text,48);
      for(int row=0;row<3 && row<(int)lines.size();++row)
        label(row==2 && lines.size()>3?fitText(lines[row],44)+" ...":lines[row],12,y+20+row*13,294);
      if(buttonCount<48) buttons[buttonCount++]={4,y,312,70,DETAIL,first+slot};
    }
    label(String(first+1)+"/"+String(count),138,219,70,1,UI_MUTED);
  }
  label(statusLine,6,199,308,1,UI_MUTED);
  button(4,210,108,28,"< Nieuwer",PREVIOUS);
  button(208,210,108,28,"Ouder >",NEXT);
}
void navigateAlarm(int direction) {
  detailLine=0;
  int count=archiveMode?archiveAlarmCount:alarmCount;
  int current=detailOpen?detailIndex:cfg.ticker && !archiveMode?infoAlarmIndex:archiveMode?archiveFirstVisible:firstVisibleAlarm;
  int next=current+direction;
  if(next>=0 && next<count) {
    if(detailOpen) detailIndex=next;
    else if(cfg.ticker && !archiveMode) infoAlarmIndex=next;
    else if(archiveMode) archiveFirstVisible=next;
    else firstVisibleAlarm=next;
  } else if(direction>0 && cfg.sdLogging) {
    uint32_t offset=archiveMode?archiveOffset+archiveAlarmCount:0;
    // Preserve the visible page when no older records exist.
    Alarm saved[MAX_ALARMS]; int savedCount=archiveAlarmCount;
    for(int i=0;i<savedCount;++i) saved[i]=archiveAlarms[i];
    uint32_t oldOffset=archiveOffset; uint8_t oldFirst=archiveFirstVisible;
    if(loadArchivePage(offset)) {archiveMode=true;detailIndex=0;}
    else {
      archiveAlarmCount=savedCount;archiveOffset=oldOffset;archiveFirstVisible=oldFirst;
      for(int i=0;i<savedCount;++i) archiveAlarms[i]=saved[i];
    }
  } else if(direction<0 && archiveMode) {
    if(archiveOffset) {
      loadArchivePage(archiveOffset>=MAX_ALARMS?archiveOffset-MAX_ALARMS:0);
      archiveFirstVisible=archiveAlarmCount?archiveAlarmCount-1:0;
      detailIndex=archiveFirstVisible;
    } else { archiveMode=false; detailOpen=false; }
  }
  invalidateMessageUi();drawScreen();
}
#if defined(CYD_TOUCH_RESISTIVE)
void calibrateTouch() {
  buttonCount=0; int rx[2],ry[2];
  const int tx[]={20,299},ty[]={20,219};
  for(int point=0;point<2;++point) {
    gfx->fillScreen(UI_BG);
    label("Raak het kruis aan",48,97,264);
    label("Annuleert na 20 seconden",48,114,264,1,UI_MUTED);
    gfx->drawFastHLine(tx[point]-10,ty[point],21,UI_ACCENT);
    gfx->drawFastVLine(tx[point],ty[point]-10,21,UI_ACCENT);
    unsigned long start=millis();int x,y;bool released=false,ok=false;
    while(millis()-start<20000) {
      server.handleClient();
      bool down=readRawTouch(x,y);
      if(!down) released=true;
      if(released && down) {rx[point]=x;ry[point]=y;ok=true;break;}
      delay(15);
    }
    if(!ok) {configNotice="Kalibratie geannuleerd";drawConfigScreen();return;}
    delay(250);
  }
  if(abs(rx[1]-rx[0])<500 || abs(ry[1]-ry[0])<500) configNotice="Ongeldige meting; probeer opnieuw";
  else {
    touchX0=rx[0]-(rx[1]-rx[0])*20/279;
    touchX1=rx[0]+(rx[1]-rx[0])*299/279;
    touchY0=ry[0]-(ry[1]-ry[0])*20/199;
    touchY1=ry[0]+(ry[1]-ry[0])*219/199;
    // Calibration targets use screen coordinates. Store the inverse of the
    // final 180-degree transform applied by readTouch().
    int endpoint=touchX0;touchX0=touchX1;touchX1=endpoint;
    endpoint=touchY0;touchY0=touchY1;touchY1=endpoint;
    Preferences cal;cal.begin("cyd-touch",false);
    cal.putInt("x0",touchX0);cal.putInt("x1",touchX1);
    cal.putInt("y0",touchY0);cal.putInt("y1",touchY1);cal.end();
    configNotice="Touchkalibratie opgeslagen";
  }
  drawConfigScreen();
}
#endif
void renderCurrent() {
  if(screenMode==MESSAGES) drawScreen();
  else if(screenMode==WIFI_SCAN) drawWifiScanScreen();
  else if(screenMode==WIFI_INPUT) drawWifiInputScreen();
  else if(screenMode==SD_FORMAT_CONFIRM) drawSdFormatConfirmScreen();
  else drawConfigScreen();
}
void handleAction(Action action,int value) {
  switch(action) {
    case CONFIGURE:beginConfig();return;
    case BACK:
      if(screenMode==MESSAGES) {detailOpen=false;detailLine=0;invalidateMessageUi();}
      else if(screenMode!=CONFIG) {if(screenMode==WIFI_INPUT){configPage=editField==2||editField>=3?ALERT_PAGE:WIFI_PAGE;if(editField>=3)alertTab=3;}screenMode=CONFIG;}
      else if(configPage==HOME_PAGE) {if(configDirty())configPage=LEAVE_PAGE;else screenMode=MESSAGES;}
      else configPage=configPage==REGION_PAGE?ALERT_PAGE:HOME_PAGE;
      break;
    case OPEN_PAGE:if(value>=100)alertTab=value-100;else configPage=(ConfigPage)value;
      if(configPage==SD_PAGE)initSdCard();break;
    case SAVE:applyConfig();break;
    case CANCEL_EDIT:if(configDirty())configPage=LEAVE_PAGE;else screenMode=MESSAGES;break;
    case DISCARD:configDraft=cfg;screenMode=MESSAGES;break;
    case REGION_PICK:regionSlot=value;regionStart=0;configPage=REGION_PAGE;break;
    case REGION_SET:configDraft.regions[regionSlot]=REGIONS[value].id;configPage=ALERT_PAGE;break;
    case REGION_PREV:if(screenMode==WIFI_SCAN)wifiPage=max(0,wifiPage-4);else regionStart=regionStart>=4?regionStart-4:0;break;
    case REGION_NEXT:if(screenMode==WIFI_SCAN){if(wifiPage+4<scannedWifiCount)wifiPage+=4;}else if(regionStart+4<REGION_COUNT)regionStart+=4;break;
    case SERVICE:configDraft.services[value]=!configDraft.services[value];break;
    case FEED_MODE:configDraft.feedMode=(uint8_t)value;break;
    case API_PORT:configDraft.apiPort=constrain((int)configDraft.apiPort+value,1,65535);break;
    case DISPLAY_MODE:configDraft.ticker=value;break;
    case INTERVAL:configDraft.intervalSec=constrain((int)configDraft.intervalSec+value,15,3600);break;
    case SD_TOGGLE:configDraft.sdLogging=!configDraft.sdLogging;break;
    case ARCHIVE:
      if(configDirty())configNotice="Sla wijzigingen eerst op";
      else if(loadArchivePage(0)){archiveMode=true;detailOpen=false;screenMode=MESSAGES;invalidateMessageUi();}
      else configNotice=statusLine;break;
    case FORMAT_REQUEST:screenMode=SD_FORMAT_CONFIRM;break;
    case FORMAT_EXECUTE:formatSdCard();configNotice=statusLine;screenMode=CONFIG;configPage=SD_PAGE;archiveMode=false;archiveAlarmCount=0;break;
    case SCAN:scanWifiNetworks();return;
    case MANUAL:openWifiInput(0);return;
    case SSID_PICK:configDraft.ssid=scannedWifiSsids[value];openWifiInput(1);return;
    case EDIT_SSID:openWifiInput(0);return;
    case EDIT_PASS:openWifiInput(1);return;
    case EDIT_CAPS:openWifiInput(2);return;
    case EDIT_HOST:openWifiInput(3);return;
    case EDIT_TOPIC:openWifiInput(4);return;
    case KEY:if(editingValue().length()<(editField==2?256U:editField>=3?48U:editField==0?32U:63U))editingValue()+=char(value);break;
    case SPACE:if(editingValue().length()<(editField==2?256U:editField>=3?48U:editField==0?32U:63U))editingValue()+=' ';break;
    case ERASE:if(editingValue().length())editingValue().remove(editingValue().length()-1);break;
    case SHIFT:keyboardUppercase=!keyboardUppercase;break;
    case SYMBOLS:keyboardSymbols=!keyboardSymbols;break;
    case KEY_DONE:
      if(editField==3){configDraft.serverHost=sanitizeHost(configDraft.serverHost);screenMode=CONFIG;configPage=ALERT_PAGE;alertTab=3;break;}
      if(editField==4){if(!configDraft.mqttTopic.length())configDraft.mqttTopic=DEFAULT_MQTT_TOPIC;screenMode=CONFIG;configPage=ALERT_PAGE;alertTab=3;break;}
      if(editField==0){openWifiInput(1);return;}
      screenMode=CONFIG;configPage=editField==2?ALERT_PAGE:WIFI_PAGE;break;
    case PREVIOUS:navigateAlarm(-1);return;
    case NEXT:navigateAlarm(1);return;
    case DETAIL:detailOpen=true;detailIndex=value;detailLine=0;invalidateMessageUi();break;
    case DETAIL_PAGE:detailLine+=6;if(detailLine>=value)detailLine=0;invalidateMessageUi();break;
    case CALIBRATE:
#if defined(CYD_TOUCH_RESISTIVE)
      calibrateTouch();
#endif
      return;
  }
  renderCurrent();
}
void handleTouch() {
  static bool pressed=false,moved=false;
  static int startX=0,startY=0,lastX=0,lastY=0;
  static unsigned long releasedAt=0;
  int x,y;
  if(readTouch(x,y)) {
    if(!pressed) {pressed=true;moved=false;startX=x;startY=y;}
    lastX=x;lastY=y;
    if(!moved && screenMode==MESSAGES && (abs(x-startX)>35 || abs(y-startY)>35)) {
      moved=true;
      navigateAlarm(abs(x-startX)>abs(y-startY)?(x<startX?1:-1):(y<startY?1:-1));
    }
    releasedAt=millis();
  } else if(pressed && millis()-releasedAt>70) {
    pressed=false;
    if(moved || abs(lastX-startX)>20 || abs(lastY-startY)>20) return;
    for(int i=buttonCount-1;i>=0;--i) {
      Button b=buttons[i];
      if(startX>=b.x && startX<b.x+b.w && startY>=b.y && startY<b.y+b.h) {
        handleAction(b.action,b.value);return;
      }
    }
  }
}
