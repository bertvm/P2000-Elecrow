#pragma once
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <type_traits>
using std::min;using std::max;
class String:public std::string {
public:
 using std::string::string;
 String():std::string(){}
 String(const std::string &s):std::string(s){}
 String(char c):std::string(1,c){}
 template<class T, typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>
 String(T n):std::string(std::to_string(n)){}
 unsigned length()const{return size();}
 int indexOf(char c,unsigned start=0)const{auto n=find(c,start);return n==npos?-1:n;}
 int indexOf(const char*s,unsigned start=0)const{auto n=find(s,start);return n==npos?-1:n;}
 int lastIndexOf(char c,unsigned start)const{auto n=rfind(c,start);return n==npos?-1:n;}
 String substring(unsigned s,unsigned e=~0U)const{return s>=size()?String():String(substr(s,e==~0U?npos:e-s));}
 void remove(unsigned s,unsigned n=~0U){if(s<size())erase(s,n==~0U?npos:n);}
 void replace(const char*a,const char*b){size_t p=0;while((p=find(a,p))!=npos){std::string::replace(p,std::char_traits<char>::length(a),b);p+=std::char_traits<char>::length(b);}}
 bool startsWith(const char*s)const{return rfind(s,0)==0;}
 void trim(){auto start=find_first_not_of(" \t\r\n"),end=find_last_not_of(" \t\r\n");if(start==npos)clear();else *this=substr(start,end-start+1);}
 void toLowerCase(){for(char &c:*this)c=std::tolower((unsigned char)c);}
 void toUpperCase(){for(char &c:*this)c=std::toupper((unsigned char)c);}
};
template<class T>T constrain(T x,T lo,T hi){return std::clamp(x,lo,hi);}
unsigned long clockMs=0;
unsigned long millis(){return clockMs;}
void delay(unsigned long n){clockMs+=n;}
#define PROGMEM
#include <font/glcdfont.h>
struct Gfx {
 uint16_t pixels[240][320]{};int cx=0,cy=0,scale=1;uint16_t color=0;
 void rectCheck(int x,int y,int w,int h){assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=320 && y+h<=240);}
 void fillRect(int x,int y,int w,int h,uint16_t c){rectCheck(x,y,w,h);for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)pixels[j][i]=c;}
 void fillScreen(uint16_t c){fillRect(0,0,320,240,c);}
 void drawFastHLine(int x,int y,int w,uint16_t c){fillRect(x,y,w,1,c);}
 void drawFastVLine(int x,int y,int h,uint16_t c){fillRect(x,y,1,h,c);}
 void fillRoundRect(int x,int y,int w,int h,int,uint16_t c){fillRect(x,y,w,h,c);}
 void drawRoundRect(int x,int y,int w,int h,int,uint16_t c){drawFastHLine(x,y,w,c);drawFastHLine(x,y+h-1,w,c);drawFastVLine(x,y,h,c);drawFastVLine(x+w-1,y,h,c);}
 void fillCircle(int x,int y,int r,uint16_t c){for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx)if(dx*dx+dy*dy<=r*r)fillRect(x+dx,y+dy,1,1,c);}
 void setFont(){}void setTextSize(int s){scale=s;}void setTextColor(uint16_t c){color=c;}void setTextWrap(bool){}void setCursor(int x,int y){cx=x;cy=y;}
 void print(const String&s){for(unsigned char c:s){rectCheck(cx,cy,6*scale,8*scale);for(int x=0;x<5;++x)for(int y=0;y<8;++y)if(font[c*5+x]&(1<<y))fillRect(cx+x*scale,cy+y*scale,scale,scale,color);cx+=6*scale;}}
 void save(const char*name){std::ofstream f(name,std::ios::binary);f<<"P6\n320 240\n255\n";for(auto &row:pixels)for(auto c:row){char b[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};f.write(b,3);}}
} gfxInstance,*gfx=&gfxInstance;
int scanStartValue=-1,scanCompleteValue=0,scanStartCalls=0;
bool mockAutoReconnect=true;
constexpr int WL_CONNECTED=3,WIFI_SCAN_FAILED=-2,WIFI_SCAN_RUNNING=-1;
struct Wifi {
 int status(){return WL_CONNECTED;}String SSID(int=0){return "Testnetwerk";}
 struct IP{String toString(){return "192.168.1.42";}};IP localIP(){return {};}
 void disconnect(bool=false,bool=false){}void scanDelete(){}void setAutoReconnect(bool v){mockAutoReconnect=v;}int scanNetworks(bool,bool,bool,int){++scanStartCalls;return scanStartValue;}int scanComplete(){return scanCompleteValue;}int RSSI(int=0){return -50;}
} WiFi;
struct Preferences{};
struct WebServer{WebServer(int){}void handleClient(){}};
bool readTouch(int&,int&){return false;}
#include "p2000_feed.h"
struct MqttStub{bool connected(){return false;}void disconnect(){}} mqttClient;
unsigned long nextMqttRetry=0;
