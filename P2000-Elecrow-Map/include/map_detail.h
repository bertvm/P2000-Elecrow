#pragma once
#include <math.h>
#include "bounded_json.h"

constexpr int MAP_X=24,MAP_Y=190,MAP_W=752,MAP_H=250,MAP_ZOOM=15;
const char *MAP_USER_AGENT="P2000-Elecrow-Map/1.0 (+https://github.com/bertvm/P2000-Elecrow)";
Alarm mapAlarm; PNG mapPng; File mapPngFile;
int mapTileOriginX=0,mapTileOriginY=0; uint16_t mapLine[256];

enum MapLoadPhase : uint8_t { MAP_LOAD_IDLE, MAP_LOAD_PREP, MAP_LOAD_GEOCODE, MAP_LOAD_TILES };
enum GeocodeResult : uint8_t { GEO_HIT, GEO_MISS, GEO_WAIT };
struct MapJob {
  MapLoadPhase phase = MAP_LOAD_IDLE;
  String candidates[5];
  uint8_t candidateCount = 0;
  uint8_t candidateIndex = 0;
  double lat = 0, lon = 0;
  int tx = 0, ty = 0, firstX = 0, firstY = 0, cx = 0, cy = 0;
  int tileIndex = 0;
  bool anyTile = false;
} mapJob;

String mapUrlEncode(const String &value){const char hex[]="0123456789ABCDEF";String out;for(size_t i=0;i<value.length();++i){uint8_t c=value[i];if(isalnum(c)||c=='-'||c=='_'||c=='.')out+=(char)c;else if(c==' ')out+='+';else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
bool readGeocodeCache(const String &key,double &lat,double &lon){if(!sdReady||!SD.exists("/geocache.tsv"))return false;File f=SD.open("/geocache.tsv",FILE_READ);while(f&&f.available()){String line=f.readStringUntil('\n');int a=line.indexOf('\t'),b=a<0?-1:line.indexOf('\t',a+1);if(a>0&&b>a&&line.substring(0,a)==key){lat=line.substring(a+1,b).toDouble();lon=line.substring(b+1).toDouble();f.close();return true;}}if(f)f.close();return false;}
void saveGeocodeCache(const String &key,double lat,double lon){if(!sdReady)return;File f=SD.open("/geocache.tsv",FILE_APPEND);if(f){f.printf("%s\t%.7f\t%.7f\n",key.c_str(),lat,lon);f.close();}}
bool allDigits(const String &s) {
  if (!s.length()) return false;
  for (char c : s) if (c < '0' || c > '9') return false;
  return true;
}
bool allLetters(const String &s) {
  if (!s.length()) return false;
  for (char c : s) if (!isalpha((uint8_t)c)) return false;
  return true;
}
String locationToken(String token) {
  String clean;
  for (char c : token)
    if (isalnum((uint8_t)c) || c == '-' || c == '\'' ) clean += c;
  return clean;
}
bool streetWord(const String &word) {
  String lower = word; lower.toLowerCase();
  const char *suffixes[] = {"straat","weg","laan","plein","gracht","kade","dijk","singel",
                            "steeg","hof","pad","park","boulevard","markt","dreef","wal","erf"};
  for (const char *suffix : suffixes) if (lower.endsWith(suffix)) return true;
  return false;
}
GeocodeResult requestGeocode(String candidate, double &lat, double &lon) {
  while (candidate.indexOf("  ") >= 0) candidate.replace("  ", " ");
  candidate.trim();
  if (!candidate.length()) return GEO_MISS;
  String key = mapUrlEncode(candidate);
  if (readGeocodeCache(key, lat, lon)) return GEO_HIT;
  if (WiFi.status() != WL_CONNECTED) return GEO_MISS;
  static unsigned long lastRequest = 0;
  unsigned long elapsed = millis() - lastRequest;
  if (lastRequest && elapsed < 1100) return GEO_WAIT;
  lastRequest = millis();
  WiFiClientSecure secure; secure.setInsecure(); secure.setTimeout(8);
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(8000);
  String url = "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&countrycodes=nl&q=" + key;
  if (!http.begin(secure, url)) return GEO_MISS;
  http.addHeader("User-Agent", MAP_USER_AGENT); http.addHeader("Accept-Language", "nl");
  const char *headers[] = {"Transfer-Encoding"};
  http.collectHeaders(headers, 1);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { Serial.printf("Geocoding fout %d\n", code); http.end(); return GEO_MISS; }
  Stream &raw = http.getStream(); raw.setTimeout(8000);
  ChunkDecodingStream decoded(raw); decoded.setTimeout(8000);
  Stream &body = http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
                     ? static_cast<Stream&>(decoded) : raw;
  BoundedJsonAllocator allocator(8 * 1024);
  JsonDocument doc(&allocator);
  auto error = deserializeJson(doc, body);
  http.end();
  if (error || !doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) return GEO_MISS;
  String latitude = doc[0]["lat"] | "", longitude = doc[0]["lon"] | "";
  lat = latitude.toDouble(); lon = longitude.toDouble();
  if (lat == 0 && lon == 0) return GEO_MISS;
  saveGeocodeCache(key, lat, lon); return GEO_HIT;
}
uint8_t buildMapCandidates(const Alarm &a, String *candidates) {
  String raw = a.text; raw.replace("\r", " "); raw.replace("\n", " ");
  raw.replace(",", " "); raw.replace(";", " "); raw.replace("(", " "); raw.replace(")", " ");
  String tokens[48]; uint8_t count = 0;
  int start = 0;
  while (start < raw.length() && count < 48) {
    while (start < raw.length() && raw[start] == ' ') ++start;
    int end = raw.indexOf(' ', start); if (end < 0) end = raw.length();
    String token = locationToken(raw.substring(start, end));
    if (token.length()) tokens[count++] = token;
    start = end + 1;
  }

  String postcode, houseNumber, street;
  int postcodeIndex = -1;
  for (uint8_t i = 0; i < count; ++i) {
    String upper = tokens[i]; upper.toUpperCase();
    if (upper.length() == 6 && allDigits(upper.substring(0,4)) && upper[0] != '0' && allLetters(upper.substring(4))) {
      postcode = upper.substring(0,4) + " " + upper.substring(4); postcodeIndex = i; break;
    }
    if (upper.length() == 4 && allDigits(upper) && upper[0] != '0' && i + 1 < count) {
      String letters = tokens[i+1]; letters.toUpperCase();
      if (letters.length() == 2 && allLetters(letters)) {
        postcode = upper + " " + letters; postcodeIndex = i; break;
      }
    }
  }
  if (postcodeIndex >= 0) {
    for (int distance = 1; distance <= 6 && !houseNumber.length(); ++distance) {
      int indices[] = {postcodeIndex - distance, postcodeIndex + distance};
      for (int index : indices) if (index >= 0 && index < count) {
        String token = tokens[index];
        if (token.length() <= 7 && isdigit((uint8_t)token[0]) && token != postcode.substring(0,4)) {
          bool plausible = true;
          for (char c : token) if (!isalnum((uint8_t)c) && c != '-') plausible = false;
          if (plausible) { houseNumber = token; break; }
        }
      }
    }
  }
  for (uint8_t i = 0; i < count && !street.length(); ++i) if (streetWord(tokens[i])) {
    int first = max(0, (int)i - 2);
    for (int j = first; j <= i; ++j) { if (street.length()) street += ' '; street += tokens[j]; }
    if (!houseNumber.length() && i + 1 < count && isdigit((uint8_t)tokens[i+1][0])) houseNumber = tokens[i+1];
  }

  String place = a.place.length() ? a.place : a.region;
  uint8_t candidateCount = 0;
  if (street.length() && postcode.length()) candidates[candidateCount++] = street + " " + houseNumber + " " + postcode + " " + place + " Nederland";
  if (postcode.length() && houseNumber.length()) candidates[candidateCount++] = postcode + " " + houseNumber + " Nederland";
  if (postcode.length()) candidates[candidateCount++] = postcode + " " + place + " Nederland";
  if (street.length()) candidates[candidateCount++] = street + " " + houseNumber + " " + place + " Nederland";
  if (place.length()) candidates[candidateCount++] = place + " Nederland";
  return candidateCount;
}

void *mapPngOpen(const char *name,int32_t *size){mapPngFile=SD.open(name,FILE_READ);if(!mapPngFile)return nullptr;*size=mapPngFile.size();return &mapPngFile;}
void mapPngClose(void *){if(mapPngFile)mapPngFile.close();}
int32_t mapPngRead(PNGFILE *,uint8_t *buf,int32_t len){return mapPngFile?mapPngFile.read(buf,len):0;}
int32_t mapPngSeek(PNGFILE *,int32_t pos){return mapPngFile&&mapPngFile.seek(pos)?pos:0;}
int mapPngDraw(PNGDRAW *draw){int sy=mapTileOriginY+draw->y;if(sy<MAP_Y||sy>=MAP_Y+MAP_H)return 1;mapPng.getLineAsRGB565(draw,mapLine,PNG_RGB565_LITTLE_ENDIAN,0xffffffff);int sourceX=max(0,MAP_X-mapTileOriginX),screenX=max(MAP_X,mapTileOriginX);int width=min(draw->iWidth-sourceX,MAP_X+MAP_W-screenX);if(width>0)gfx->draw16bitRGBBitmap(screenX,sy,mapLine+sourceX,width,1);return 1;}
bool downloadTile(int z, int x, int y, const String &path) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (SD.exists(path)) SD.remove(path);
  WiFiClientSecure secure; secure.setInsecure(); secure.setTimeout(8);
  HTTPClient http;
  String url = "https://tile.openstreetmap.org/" + String(z) + "/" + String(x) + "/" + String(y) + ".png";
  if (!http.begin(secure, url)) return false;
  http.addHeader("User-Agent", MAP_USER_AGENT);
  http.setConnectTimeout(8000); http.setTimeout(8000);
  const char *headers[] = {"Transfer-Encoding"};
  http.collectHeaders(headers, 1);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { Serial.printf("Kaarttegel fout %d\n", code); http.end(); return false; }
  File f = SD.open(path, FILE_WRITE);
  if (!f) { http.end(); return false; }
  Stream &raw = http.getStream(); raw.setTimeout(8000);
  ChunkDecodingStream decoded(raw); decoded.setTimeout(8000);
  bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  Stream &in = chunked ? static_cast<Stream&>(decoded) : raw;
  uint8_t buf[1024];
  int contentLength = http.getSize();
  int remaining = contentLength;
  size_t written = 0;
  unsigned long lastData = millis();
  while ((remaining > 0 || contentLength < 0) && millis() - lastData < 5000) {
    yield();
    size_t available = in.available();
    if (available) {
      int count = in.readBytes(buf, min(available, sizeof(buf)));
      if (count > 0) {
        f.write(buf, count);
        written += count;
        if (remaining > 0) remaining -= count;
        lastData = millis();
      }
    } else {
      if (!http.connected() && !in.available()) break;
      delay(1);
    }
  }
  bool timedOut = (millis() - lastData >= 5000) && (remaining > 0 || (contentLength < 0 && http.connected()));
  bool ok = written > 8 && !timedOut && (contentLength < 0 || remaining == 0);
  f.close();
  http.end();
  if (!ok) { SD.remove(path); return false; }
  return SD.exists(path);
}
bool drawMapTile(int z,int x,int y,int ox,int oy){int limit=1<<z;if(x<0)x+=limit;if(x>=limit)x-=limit;if(y<0||y>=limit)return false;String path="/maptiles/"+String(z)+"_"+String(x)+"_"+String(y)+".png";if(!SD.exists(path)&&!downloadTile(z,x,y,path))return false;mapTileOriginX=ox;mapTileOriginY=oy;int rc=mapPng.open(path.c_str(),mapPngOpen,mapPngClose,mapPngRead,mapPngSeek,mapPngDraw);if(rc!=PNG_SUCCESS){SD.remove(path);return false;}rc=mapPng.decode(nullptr,PNG_FAST_PALETTE);mapPng.close();if(rc!=PNG_SUCCESS)SD.remove(path);return rc==PNG_SUCCESS;}

void drawMapError(const String &message){mapJob.phase=MAP_LOAD_IDLE;gfx->fillRoundRect(MAP_X,MAP_Y,MAP_W,MAP_H,12,UI_SURFACE);drawHeading(message,MAP_X+24,MAP_Y+94,MAP_W-48,1,UI_MUTED);}
void drawMapDetailBase(const String &status=""){gfx->fillScreen(UI_BG);gfx->fillRect(0,0,800,70,UI_SURFACE);gfx->fillRect(0,67,800,3,UI_ACCENT);gfx->fillRoundRect(16,10,105,45,9,UI_SURFACE_2);gfx->drawRoundRect(16,10,105,45,9,UI_ACCENT);drawHeading("Terug",34,21,74,1,UI_ACCENT);drawHeading("Locatie",145,18,300);uint16_t accent=alarmAccent(mapAlarm);drawHeading(mapAlarm.place.length()?mapAlarm.place:mapAlarm.region,24,82,752);drawTwoLineHeading(mapAlarm.time+"  |  Capcode "+mapAlarm.caps,24,112,752,accent);drawHeading(mapAlarm.text,24,164,752,1,UI_MUTED);gfx->fillRoundRect(MAP_X,MAP_Y,MAP_W,MAP_H,12,UI_SURFACE);if(status.length())drawHeading(status,MAP_X+24,MAP_Y+94,MAP_W-48,1,UI_MUTED);}
void drawMapAttribution() {
  gfx->fillCircle(mapJob.cx, mapJob.cy, 10, UI_FIRE);
  gfx->drawCircle(mapJob.cx, mapJob.cy, 12, UI_TEXT);
  gfx->fillCircle(mapJob.cx, mapJob.cy, 3, UI_TEXT);
  gfx->fillRect(MAP_X, MAP_Y + MAP_H - 19, MAP_W, 19, UI_SURFACE);
  gfx->setFont(); gfx->setTextSize(1); gfx->setTextColor(UI_TEXT);
  gfx->drawCircle(MAP_X + 13, MAP_Y + MAP_H - 10, 6, UI_TEXT);
  gfx->setCursor(MAP_X + 10, MAP_Y + MAP_H - 14); gfx->print("C");
  gfx->setCursor(MAP_X + 23, MAP_Y + MAP_H - 14); gfx->print("OpenStreetMap contributors");
}
void beginMapTiles() {
  drawMapDetailBase("Kaart laden...");
  double scale = 256.0 * (1 << MAP_ZOOM);
  double worldX = (mapJob.lon + 180.0) / 360.0 * scale;
  double sinLat = sin(mapJob.lat * M_PI / 180.0);
  double worldY = (0.5 - log((1 + sinLat) / (1 - sinLat)) / (4 * M_PI)) * scale;
  mapJob.tx = floor(worldX / 256.0);
  mapJob.ty = floor(worldY / 256.0);
  mapJob.cx = MAP_X + MAP_W / 2;
  mapJob.cy = MAP_Y + MAP_H / 2;
  mapJob.firstX = mapJob.cx - (int)fmod(worldX, 256.0) - 256;
  mapJob.firstY = mapJob.cy - (int)fmod(worldY, 256.0) - 256;
  gfx->fillRect(MAP_X, MAP_Y, MAP_W, MAP_H, rgb565(30, 45, 55));
  mapJob.tileIndex = 0;
  mapJob.anyTile = false;
  mapJob.phase = MAP_LOAD_TILES;
}
void openMapDetail(const Alarm &alarm) {
  mapAlarm = alarm;
  screenMode = MAP_DETAIL;
  mapJob = MapJob{};
  mapJob.phase = MAP_LOAD_PREP;
  drawMapDetailBase("Locatie zoeken...");
}
void pumpMapLoad() {
  if (screenMode != MAP_DETAIL || mapJob.phase == MAP_LOAD_IDLE) return;
  if (mapJob.phase == MAP_LOAD_PREP) {
    if (!initSdCard()) { drawMapError("SD-kaart nodig voor kaartcache"); return; }
    if (!SD.exists("/maptiles")) SD.mkdir("/maptiles");
    mapJob.candidateCount = buildMapCandidates(mapAlarm, mapJob.candidates);
    mapJob.candidateIndex = 0;
    mapJob.phase = MAP_LOAD_GEOCODE;
    return;
  }
  if (mapJob.phase == MAP_LOAD_GEOCODE) {
    if (mapJob.candidateIndex >= mapJob.candidateCount) { drawMapError("Locatie niet gevonden"); return; }
    uint8_t i = mapJob.candidateIndex;
    if (i && mapJob.candidates[i] == mapJob.candidates[i - 1]) { ++mapJob.candidateIndex; return; }
    Serial.println("Locatie zoeken: " + mapJob.candidates[i]);
    GeocodeResult result = requestGeocode(mapJob.candidates[i], mapJob.lat, mapJob.lon);
    if (result == GEO_WAIT) return;
    ++mapJob.candidateIndex;
    if (result == GEO_HIT) beginMapTiles();
    return;
  }
  if (mapJob.phase == MAP_LOAD_TILES) {
    if (mapJob.tileIndex >= 9) {
      if (!mapJob.anyTile) { drawMapError("Kaart kon niet worden geladen"); return; }
      drawMapAttribution();
      mapJob.phase = MAP_LOAD_IDLE;
      return;
    }
    int row = mapJob.tileIndex / 3 - 1;
    int col = mapJob.tileIndex % 3 - 1;
    mapJob.anyTile = drawMapTile(MAP_ZOOM, mapJob.tx + col, mapJob.ty + row,
                                 mapJob.firstX + (col + 1) * 256,
                                 mapJob.firstY + (row + 1) * 256) || mapJob.anyTile;
    ++mapJob.tileIndex;
  }
}
