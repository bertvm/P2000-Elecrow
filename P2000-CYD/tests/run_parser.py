#!/usr/bin/env python3
"""Exercise the production feed parser with native ArduinoJson, without WiFi."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1];out=root/'tests/.build';out.mkdir(exist_ok=True)
src=(root/'src/main.cpp').read_text()
model=src[src.index('struct Settings'):src.index('const char PAGE')]
parser=src[src.index('String field('):src.index('#include "compact_ui.h"')]
test=r'''
std::vector<String> logged;
void appendAlarmLog(const Alarm&a){logged.push_back(a.id);}
'''+parser+r'''
bool parse(const std::string&s){std::istringstream body(s);return readAlarms(body);}
int main(int argc,char **argv){
 cfg.regions[0]="1";cfg.capcodes="0012345";
 const std::string row=R"({"id":42,"regioid":1,"datum":"2026-09-05","tijd":"11:30","tekstmelding":"Brandweer test","dienst":"brandweer","capcodes":[{"capcode":"012345"}]})";
 assert(parse("{\"meldingen\":["+row+"]}"));assert(alarmCount==1&&alarms[0].id=="42");
 assert(logged.size()==1);assert(parse("["+row+"]"));assert(logged.size()==1);
  cfg.capcodes="999";assert(parse("["+row+"]"));assert(alarmCount==1&&alarms[0].id=="42");
 cfg.capcodes="";cfg.regions[0]="";assert(parse("["+row+"]"));assert(alarmCount==1);
 cfg.regions[1]="1";cfg.services[FILTER_FIRE]=false;assert(parse("["+row+"]"));assert(alarmCount==1);
 cfg.services[FILTER_FIRE]=true;assert(parse("["+row+"]"));assert(alarmCount==1);
 assert(!parse("{\"meldingen\":["));assert(alarmCount==1&&alarms[0].id=="42");
 assert(!parse("{\"status\":\"unavailable\"}"));assert(alarmCount==1);
 assert(!parse("[{\"tekstmelding\":\""+std::string(100000,'x')+"\"}]"));assert(alarmCount==1);
 assert(parse("[]"));assert(alarmCount==1);
 // The archive must receive oldest first when the response is newest first.
 logged.clear();assert(parse(R"([{"id":3,"regioid":1,"text":"nieuw"},{"id":2,"regioid":1,"text":"oud"}])"));
 assert(logged.size()==2&&logged[0]=="2"&&logged[1]=="3");
 assert(alarms[0].id=="3"&&alarms[1].id=="2"&&alarms[2].id=="42");
 // Allocator growth/shrink/failure preserves existing allocation accounting.
 BoundedJsonAllocator a(128);void*p=a.allocate(100);assert(p);assert(!a.allocate(40));
 assert(!a.reallocate(p,200));p=a.reallocate(p,40);assert(p);void*q=a.allocate(80);assert(q);
 a.deallocate(p);a.deallocate(q);p=a.allocate(128);assert(p);a.deallocate(p);
 if(argc>1){std::ifstream f(argv[1]);assert(f.good());std::string live((std::istreambuf_iterator<char>(f)),{});assert(parse(live));std::cout<<"Live API response parsed within 64 KiB allocation budget\n";}
 std::cout<<"Parser: numeric fields, capcodes, regions, services, malformed/oversized JSON, log order and allocator passed\n";
}
'''
(out/'parser.cpp').write_text('#include "host_ui.h"\n#include <sstream>\n#include "bounded_json.h"\nusing Stream=std::istream;\n'+model+test)
lib=root/'.pio/libdeps/cyd_2432s032r'
subprocess.run(['c++','-std=c++17','-I'+str(root/'tests'),'-I'+str(root/'include'),'-I'+str(lib/'ArduinoJson/src'),'-I'+str(lib/'GFX Library for Arduino/src'),str(out/'parser.cpp'),'-o',str(out/'parser')],check=True)
subprocess.run([str(out/'parser')]+sys.argv[1:],check=True)
