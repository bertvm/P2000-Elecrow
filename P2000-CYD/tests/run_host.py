#!/usr/bin/env python3
"""Execute the actual CYD UI on a host raster target and check every screen."""
from pathlib import Path
import subprocess, zlib, struct
root=Path(__file__).resolve().parents[1]
out=root/'tests/.build';out.mkdir(exist_ok=True)
src=(root/'src/main.cpp').read_text()
# Reuse production model, colours and classification, not a duplicate UI.
model=src[src.index('struct Settings'):src.index('const char PAGE')]
regions=src[src.index('struct Region'):src.index('String regionOptions')]
test=r'''
String regionName(const String &id){for(const auto&r:REGIONS)if(id==r.id)return r.name;return "Geen";}
String selectedRegionsLabel(uint8_t){return "Amsterdam-Amstelland | Utrecht";}
bool initSdCard(bool=false){return sdReady;}
bool formatSdCard(){return true;}
bool archiveAvailable=false;
bool loadArchivePage(uint32_t offset){archiveAlarmCount=0;archiveFirstVisible=0;archiveOffset=offset;return archiveAvailable;}
void saveSettings(){}void connectWifi(){WiFi.setAutoReconnect(true);}void startConfigurationAp(){}
String wifiStatusText(){return "Verbonden: Testnetwerk";}
#include "compact_ui.h"
void checkButtons(){
 for(int i=0;i<buttonCount;++i){auto b=buttons[i];assert(b.x>=0&&b.y>=0&&b.x+b.w<=320&&b.y+b.h<=240);
  for(int j=0;j<i;++j){auto a=buttons[j];assert(!(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h));}}
}
int main(){
 cfg.ssid="Testnetwerk";cfg.regions[0]="1";configDraft=cfg;
 for(int p=HOME_PAGE;p<=LEAVE_PAGE;++p){configPage=(ConfigPage)p;
  for(int tab=0;tab<3;++tab){alertTab=tab;drawConfigScreen();checkButtons();}}
 for(int field=0;field<3;++field)for(int symbols=0;symbols<2;++symbols){editField=field;keyboardSymbols=symbols;drawWifiInputScreen();checkButtons();}
 scannedWifiCount=8;for(auto &s:scannedWifiSsids)s="Een lange wifi netwerknaam";
 drawWifiScanScreen();checkButtons();drawSdFormatConfirmScreen();checkButtons();
 screenMode=MESSAGES;invalidateMessageUi();drawScreen();checkButtons();
 alarmCount=2;apiConnected=true;apiChecked=true;statusLine="2 berichten bijgewerkt";
 alarms[0]={"1","05-09-2026 11:42","0123456","brandweer","Amsterdam-Amstelland","Amsterdam","P 1 BR Woningbrand Prins Hendrikkade Amsterdam. Meerdere eenheden onderweg."};
 alarms[1]={"2","05-09-2026 11:39","0765432","ambulance","Utrecht","Utrecht","A1 Ambulance naar Stationsplein Utrecht. Medische hulpverlening."};
 invalidateMessageUi();drawScreen();checkButtons();gfx->save("feed.ppm");
 handleAction(DETAIL,0);checkButtons();gfx->save("detail.ppm");
 beginConfig();checkButtons();gfx->save("config.ppm");
 openWifiInput(1);checkButtons();gfx->save("keyboard.ppm");
 // Draft cancellation cannot modify saved settings.
 configDraft.regions[0]="18";screenMode=CONFIG;handleAction(CANCEL_EDIT,0);assert(configPage==LEAVE_PAGE);
 handleAction(DISCARD,0);assert(cfg.regions[0]=="1");
 // Saving applies filters and resets stale feed indices.
 beginConfig();configDraft.regions[0]="18";handleAction(SAVE,0);assert(cfg.regions[0]=="18"&&alarmCount==0);
 // Returning from SD confirmation must not inherit a previous capcode editor.
 editField=2;screenMode=SD_FORMAT_CONFIRM;configPage=SD_PAGE;handleAction(BACK,0);assert(configPage==SD_PAGE&&screenMode==CONFIG);
 // Page overflow must not erase the last readable archive page.
 screenMode=MESSAGES;cfg.sdLogging=true;archiveMode=true;archiveAlarmCount=1;archiveOffset=8;
 archiveAlarms[0]=alarms[0];String previous=archiveAlarms[0].text;archiveFirstVisible=0;detailOpen=false;
 navigateAlarm(1);assert(archiveAlarmCount==1&&archiveOffset==8&&archiveAlarms[0].text==previous);
 // Long words must wrap without loss, including newline-only paragraphs.
 auto lines=wrapped("abcdefghijklmnopqrstuvwxyz",5);String joined;for(auto &l:lines){assert(l.length()<=5);joined+=l;}assert(joined=="abcdefghijklmnopqrstuvwxyz");
 lines=wrapped("a\n\nb",49);assert(lines.size()==3 && lines[1].empty());
 // Scan waits for disconnect, suppresses reconnect, then restores it.
 scanStartCalls=0;scanStartValue=WIFI_SCAN_RUNNING;scanCompleteValue=WIFI_SCAN_RUNNING;
 scanWifiNetworks();assert(wifiScanning&&!mockAutoReconnect);updateWifiScan();assert(scanStartCalls==0);
 clockMs+=350;updateWifiScan();assert(scanStartCalls==1&&wifiScanning);
 scanCompleteValue=3;updateWifiScan();assert(!wifiScanning&&scannedWifiCount==3&&mockAutoReconnect);
 // A driver failure is not presented as a successful empty scan.
 scanStartCalls=0;scanStartValue=WIFI_SCAN_FAILED;scanWifiNetworks();
 clockMs+=350;updateWifiScan();assert(wifiScanning);clockMs+=500;updateWifiScan();clockMs+=500;updateWifiScan();
 assert(!wifiScanning&&wifiScanResult==WIFI_SCAN_FAILED&&scanStartCalls==3&&mockAutoReconnect);
 // An actually empty scan still completes cleanly.
 scanStartValue=WIFI_SCAN_RUNNING;scanCompleteValue=0;scanWifiNetworks();clockMs+=350;updateWifiScan();
 assert(!wifiScanning&&wifiScanResult==0&&scannedWifiCount==0);
 std::cout<<"UI: screens, button bounds, draft save/cancel, archive boundary and text wrapping passed\n";
}
'''
(out/'ui.cpp').write_text('#include "host_ui.h"\n'+model+regions+test)
gfx=root/'.pio/libdeps/cyd_2432s032r/GFX Library for Arduino/src'
subprocess.run(['c++','-std=c++17','-I'+str(root/'tests'),'-I'+str(root/'include'),'-I'+str(gfx),str(out/'ui.cpp'),'-o',str(out/'ui')],check=True)
subprocess.run([str(out/'ui')],cwd=out,check=True)
# Encode the renderer's raw RGB output to PNG using only the standard library.
for ppm in out.glob('*.ppm'):
 data=ppm.read_bytes().split(b'\n',3)[3]
 def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload))
 png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',320,240,8,2,0,0,0))
 png+=chunk(b'IDAT',zlib.compress(b''.join(b'\0'+data[y*960:(y+1)*960] for y in range(240))))+chunk(b'IEND',b'')
 ppm.with_suffix('.png').write_bytes(png)
