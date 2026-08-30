#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Arduino_GFX_Library.h>

// Elecrow DIS07050H / CrowPanel ESP32-S3 5 inch (800x480 RGB) pinout.
#define GFX_BL  2
Arduino_ESP32RGBPanel *rgbbus = new Arduino_ESP32RGBPanel(
  40, 41, 39, 0,
  45, 48, 47, 21, 14, 5, 6, 7, 15, 16, 4, 8, 3, 46, 9, 1,
  // Balanced refresh rate while leaving bus time for the Wi-Fi radio.
  0, 8, 1, 32, 0, 8, 1, 8, 1, 10000000);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, rgbbus, 0, true);

struct Settings {
  String ssid, password, apiUrl, regions[3], capcodes;
  bool ticker = false;
  uint32_t intervalSec = 60;
} cfg;

struct Alarm {
  String id;
  String time;
  String caps;
  String service;
  String region;
  String place;
  String text;
};
constexpr uint8_t MAX_ALARMS = 8;
const char *ALARMRINGDROID_URL = "https://beta.alarmeringdroid.nl/api2/find/";
constexpr uint8_t TOUCH_SDA = 19, TOUCH_SCL = 20;
TAMC_GT911 gt911(TOUCH_SDA, TOUCH_SCL, -1, -1, 800, 480);
Alarm alarms[MAX_ALARMS];
uint8_t alarmCount = 0;
uint8_t firstVisibleAlarm = 0;
uint8_t infoAlarmIndex = 0;
String newestInfoId;
uint8_t touchAddress = 0;
bool touchPressed = false, swipeHandled = false;
int touchStartY = 0;
int touchStartX = 0;
unsigned long nextPoll = 0;
unsigned long nextWifiRetry = 0;
String statusLine = "Configuratie laden...";
bool configurationMode = false;
enum ScreenMode { MESSAGES, CONFIG };
ScreenMode screenMode = MESSAGES;
Preferences prefs;
WebServer server(80);

// Compact 32 x 32 RGB565 icons.  They are drawn from a RGB565 pixel buffer,
// avoiding image files and keeping the display responsive.
enum ServiceIcon : uint8_t { ICON_NONE, ICON_FIRE, ICON_POLICE, ICON_AMBULANCE, ICON_HELICOPTER };
constexpr uint8_t ICON_SIZE = 32;
constexpr uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_RED = 0xF800;
constexpr uint16_t C_BLUE = 0x001F, C_YELLOW = 0xFFE0, C_GREY = 0x8410;
uint16_t iconPixels[ICON_SIZE * ICON_SIZE];

void iconRect(int x, int y, int w, int h, uint16_t colour) {
  for (int py = max(0, y); py < min((int)ICON_SIZE, y + h); ++py)
    for (int px = max(0, x); px < min((int)ICON_SIZE, x + w); ++px)
      iconPixels[py * ICON_SIZE + px] = colour;
}
void iconCircle(int cx, int cy, int radius, uint16_t colour) {
  for (int py = 0; py < ICON_SIZE; ++py) for (int px = 0; px < ICON_SIZE; ++px) {
    int dx = px - cx, dy = py - cy;
    if (dx * dx + dy * dy <= radius * radius) iconPixels[py * ICON_SIZE + px] = colour;
  }
}
ServiceIcon serviceIcon(const Alarm &a) {
  // Alarmeringdroid links the service to its capcode. Keywords handle APIs
  // which expose only the capcode description instead of `dienst`.
  String source = a.service + " " + a.caps + " " + a.text;
  source.toLowerCase();
  if (source.indexOf("trauma") >= 0 || source.indexOf("lifeliner") >= 0 ||
      source.indexOf("mmt") >= 0 || source.indexOf("0120901") >= 0) return ICON_HELICOPTER;
  if (source.indexOf("brandweer") >= 0 || source.indexOf("brw") >= 0) return ICON_FIRE;
  if (source.indexOf("politie") >= 0) return ICON_POLICE;
  if (source.indexOf("ambulance") >= 0 || source.indexOf("rav") >= 0 || source.indexOf("mka") >= 0) return ICON_AMBULANCE;
  return ICON_NONE;
}
void drawServiceIcon(ServiceIcon type, int x, int y) {
  if (type == ICON_NONE) return;
  for (uint16_t &pixel : iconPixels) pixel = C_BLACK;
  if (type == ICON_FIRE) { // red fire engine with ladder and wheels
    iconRect(3, 13, 23, 12, C_RED); iconRect(23, 17, 6, 8, C_RED);
    iconRect(5, 10, 17, 2, C_WHITE); iconRect(7, 7, 2, 8, C_WHITE); iconRect(15, 7, 2, 8, C_WHITE);
    iconCircle(9, 26, 3, C_GREY); iconCircle(24, 26, 3, C_GREY);
  } else if (type == ICON_POLICE) { // blue shield and white P
    iconRect(7, 4, 18, 4, C_BLUE); iconRect(5, 8, 22, 12, C_BLUE); iconRect(8, 20, 16, 6, C_BLUE); iconRect(12, 26, 8, 3, C_BLUE);
    iconRect(11, 10, 3, 12, C_WHITE); iconRect(14, 10, 7, 3, C_WHITE); iconRect(18, 13, 3, 4, C_WHITE); iconRect(14, 17, 6, 3, C_WHITE);
  } else if (type == ICON_AMBULANCE) { // white ambulance with red medical cross
    iconRect(3, 12, 23, 13, C_WHITE); iconRect(24, 16, 5, 9, C_WHITE);
    iconRect(12, 14, 4, 9, C_RED); iconRect(9, 17, 10, 4, C_RED);
    iconCircle(9, 26, 3, C_GREY); iconCircle(24, 26, 3, C_GREY);
  } else { // trauma helicopter
    iconRect(10, 14, 14, 8, C_YELLOW); iconRect(23, 16, 6, 3, C_YELLOW); iconRect(5, 17, 5, 2, C_YELLOW);
    iconRect(15, 8, 2, 6, C_GREY); iconRect(5, 7, 22, 2, C_GREY); iconRect(15, 22, 2, 5, C_GREY); iconRect(8, 27, 16, 2, C_GREY);
    iconCircle(11, 18, 2, C_BLUE);
  }
  gfx->draw16bitRGBBitmap(x, y, iconPixels, ICON_SIZE, ICON_SIZE);
}
void drawConfigSymbol() {
  // Font-independent cog: reliable on Arduino_GFX's built-in font.
  constexpr int cx = 765, cy = 28;
  gfx->drawCircle(cx, cy, 11, GREEN); gfx->drawCircle(cx, cy, 4, GREEN);
  gfx->drawFastVLine(cx, cy - 16, 5, GREEN); gfx->drawFastVLine(cx, cy + 12, 5, GREEN);
  gfx->drawFastHLine(cx - 16, cy, 5, GREEN); gfx->drawFastHLine(cx + 12, cy, 5, GREEN);
  gfx->drawLine(cx - 11, cy - 11, cx - 8, cy - 8, GREEN);
  gfx->drawLine(cx + 11, cy - 11, cx + 8, cy - 8, GREEN);
  gfx->drawLine(cx - 11, cy + 11, cx - 8, cy + 8, GREEN);
  gfx->drawLine(cx + 11, cy + 11, cx + 8, cy + 8, GREEN);
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<style>body{font:16px system-ui;max-width:640px;margin:2em auto;padding:0 1em}input,select{width:100%;box-sizing:border-box;padding:.6em;margin:.2em 0 1em}button{padding:.7em 1.2em}small{color:#555}</style>
<h1>P2000 display</h1><form method=post action=/save>
<label>Wifi-naam</label><input name=ssid required value="%SSID%">
<label>Wifi-wachtwoord</label><input name=password type=password placeholder="ongewijzigd laten om te bewaren">
<label>P2000 API URL</label><input name=apiUrl required value="%URL%"><small>Standaard: Alarmeringdroid API v2. Regio- en capcodefilters gebeuren op de ESP32.</small>
<label>Regio 1</label><select name=region1>%REGION1_OPTIONS%</select>
<label>Regio 2</label><select name=region2>%REGION2_OPTIONS%</select>
<label>Regio 3</label><select name=region3>%REGION3_OPTIONS%</select>
<label>Weergave</label><select name=display><option value="list" %LIST_SELECTED%>Meldingenlijst</option><option value="ticker" %TICKER_SELECTED%>Infoscherm - 1 kanaal</option></select>
<label>Capcodes (komma-gescheiden)</label><input name=capcodes value="%CAPS%" placeholder="bijv. 0123456,0765432">
<label>Verversing (seconden, minimaal 15)</label><input name=interval type=number min=15 value="%INTERVAL%">
<p><button>Opslaan en herstarten</button></p></form><p>Status: %STATUS%</p>
)HTML";

String htmlEscape(String s) { s.replace("&", "&amp;"); s.replace("\"", "&quot;"); s.replace("<", "&lt;"); return s; }
String tpl(const String &key) {
  if (key == "SSID") return htmlEscape(cfg.ssid);
  if (key == "URL") return htmlEscape(cfg.apiUrl);
  if (key == "CAPS") return htmlEscape(cfg.capcodes);
  if (key == "INTERVAL") return String(cfg.intervalSec);
  if (key == "STATUS") return htmlEscape(statusLine);
  return "";
}
struct Region { const char *id; const char *name; };
static const Region REGIONS[] = {
    {"", "Geen (geen regiofilter)"}, {"1", "Amsterdam-Amstelland"}, {"2", "Groningen"},
    {"3", "Noord- en Oost-Gelderland"}, {"4", "Zaanstreek-Waterland"},
    {"5", "Hollands Midden"}, {"6", "Brabant-Noord"}, {"7", "Fryslan"},
    {"8", "Gelderland-Midden"}, {"9", "Kennemerland"}, {"10", "Rotterdam-Rijnmond"},
    {"11", "Brabant-Zuidoost"}, {"12", "Drenthe"}, {"13", "Gelderland-Zuid"},
    {"14", "Zuid-Holland-Zuid"}, {"15", "Limburg-Noord"}, {"17", "IJsselland"},
    {"18", "Utrecht"}, {"19", "Gooi en Vechtstreek"}, {"20", "Zeeland"},
    {"21", "Limburg-Zuid"}, {"23", "Twente"}, {"24", "Noord-Holland Noord"},
    {"25", "Haaglanden"}, {"26", "Midden- en West-Brabant"}, {"27", "Flevoland"}
};
constexpr uint8_t REGION_COUNT = sizeof(REGIONS) / sizeof(REGIONS[0]);
String regionOptions(const String &selected) {
  String options;
  for (const Region &region : REGIONS) {
    options += "<option value=\"" + String(region.id) + "\"";
    if (selected == region.id) options += " selected";
    options += ">" + String(region.name) + "</option>";
  }
  return options;
}
String regionName(const String &id) {
  for (const Region &region : REGIONS) if (id == region.id) return region.name;
  return "Geen (geen regiofilter)";
}
String selectedRegionsLabel(uint8_t maxChars) {
  String label;
  for (const String &region : cfg.regions) {
    if (!region.length()) continue;
    if (label.length()) label += " | ";
    label += regionName(region);
  }
  if (!label.length()) return "Geen regio";
  if (label.length() > maxChars) return label.substring(0, maxChars - 3) + "...";
  return label;
}
String settingsPage() {
  String page = FPSTR(PAGE);
  page.replace("%SSID%", tpl("SSID")); page.replace("%URL%", tpl("URL"));
  page.replace("%REGION1_OPTIONS%", regionOptions(cfg.regions[0]));
  page.replace("%REGION2_OPTIONS%", regionOptions(cfg.regions[1]));
  page.replace("%REGION3_OPTIONS%", regionOptions(cfg.regions[2])); page.replace("%CAPS%", tpl("CAPS"));
  page.replace("%LIST_SELECTED%", cfg.ticker ? "" : "selected");
  page.replace("%TICKER_SELECTED%", cfg.ticker ? "selected" : "");
  page.replace("%INTERVAL%", tpl("INTERVAL")); page.replace("%STATUS%", tpl("STATUS"));
  return page;
}

void loadSettings() {
  // First boot has no namespace yet. Opening read/write creates it; opening
  // read-only would report NVS_NOT_FOUND and prevents the access-point setup.
  prefs.begin("p2000", false);
  cfg.ssid = prefs.isKey("ssid") ? prefs.getString("ssid") : "";
  cfg.password = prefs.isKey("pass") ? prefs.getString("pass") : "";
  cfg.apiUrl = prefs.isKey("url") ? prefs.getString("url") : ALARMRINGDROID_URL;
  cfg.regions[0] = prefs.isKey("reg1") ? prefs.getString("reg1") : (prefs.isKey("region") ? prefs.getString("region") : "");
  cfg.regions[1] = prefs.isKey("reg2") ? prefs.getString("reg2") : "";
  cfg.regions[2] = prefs.isKey("reg3") ? prefs.getString("reg3") : "";
  cfg.capcodes = prefs.isKey("caps") ? prefs.getString("caps") : "";
  cfg.ticker = prefs.isKey("ticker") ? prefs.getBool("ticker") : false;
  cfg.intervalSec = prefs.isKey("int") ? prefs.getUInt("int") : 60;
  prefs.end();
}
void saveSettings() {
  prefs.begin("p2000", false);
  prefs.putString("ssid", cfg.ssid); prefs.putString("pass", cfg.password); prefs.putString("url", cfg.apiUrl);
  prefs.putString("reg1", cfg.regions[0]); prefs.putString("reg2", cfg.regions[1]); prefs.putString("reg3", cfg.regions[2]);
  prefs.putString("caps", cfg.capcodes); prefs.putBool("ticker", cfg.ticker); prefs.putUInt("int", cfg.intervalSec); prefs.end();
}

String field(JsonObject obj, const char *a, const char *b = "") {
  if (obj[a].is<const char*>()) return String(obj[a].as<const char*>());
  if (*b && obj[b].is<const char*>()) return String(obj[b].as<const char*>());
  return "";
}
String normalizedCapcode(String value) {
  value.trim();
  while (value.length() > 1 && value[0] == '0') value.remove(0, 1);
  return value;
}
bool capcodeMatches(JsonObject item) {
  if (!cfg.capcodes.length()) return true;
  String wanted = cfg.capcodes;
  int start = 0;
  while (start < wanted.length()) {
    int comma = wanted.indexOf(',', start); if (comma < 0) comma = wanted.length();
    String cap = normalizedCapcode(wanted.substring(start, comma));
    for (JsonObject found : item["capcodes"].as<JsonArray>())
      if (normalizedCapcode(field(found, "capcode")) == cap) return true;
    start = comma + 1;
  }
  return false;
}
bool matchesFilters(JsonObject item) {
  bool hasRegionFilter = false;
  for (const String &region : cfg.regions) {
    if (!region.length()) continue;
    hasRegionFilter = true;
    if (field(item, "regioid") == region) return capcodeMatches(item);
  }
  if (hasRegionFilter) return false;
  // With no selected region, never fall back to a nationwide feed.
  return false;
}
void readAlarms(const String &body) {
  JsonDocument doc; DeserializationError err = deserializeJson(doc, body);
  if (err) { statusLine = "API geeft geen geldige JSON"; return; }
  JsonArray list;
  if (doc.is<JsonArray>()) list = doc.as<JsonArray>();
  else if (doc["meldingen"].is<JsonArray>()) list = doc["meldingen"].as<JsonArray>();
  else if (doc["messages"].is<JsonArray>()) list = doc["messages"].as<JsonArray>();
  else if (doc["results"].is<JsonArray>()) list = doc["results"].as<JsonArray>();
  else if (doc["data"].is<JsonArray>()) list = doc["data"].as<JsonArray>();
  else { statusLine = "JSON bevat geen berichtenlijst"; return; }
  String currentlyShownId = (alarmCount && infoAlarmIndex < alarmCount) ? alarms[infoAlarmIndex].id : "";
  alarmCount = 0;
  for (JsonObject item : list) {
    if (!matchesFilters(item)) continue;
    if (alarmCount >= MAX_ALARMS) break;
    Alarm &a = alarms[alarmCount++];
    a.id = field(item, "id");
    a.time = field(item, "timestamp", "time");
    if (!a.time.length()) a.time = field(item, "datum") + " " + field(item, "tijd");
    a.caps = field(item, "capcode", "capcodes");
    if (!a.caps.length()) a.caps = field(item, "capstring");
    a.service = field(item, "dienst", "service");
    a.region = field(item, "regio"); a.place = field(item, "plaats");
    a.text = field(item, "message", "text");
    if (!a.text.length()) a.text = field(item, "tekstmelding", "melding");
    if (!a.text.length()) a.text = field(item, "body", "description");
  }
  statusLine = String(alarmCount) + " berichten bijgewerkt";
  firstVisibleAlarm = 0;
  if (alarmCount) {
    bool newMessage = newestInfoId.length() && alarms[0].id != newestInfoId;
    if (!newestInfoId.length() || newMessage) infoAlarmIndex = 0;
    else {
      for (uint8_t i = 0; i < alarmCount; ++i)
        if (alarms[i].id == currentlyShownId) { infoAlarmIndex = i; break; }
    }
    newestInfoId = alarms[0].id;
  }
}

void drawWrapped(const String &s, int x, int &y, int maxWidth, int lineHeight, int charWidth) {
  String line, word;
  for (size_t i = 0; i <= s.length(); ++i) {
    char c = i < s.length() ? s[i] : ' ';
    if (c == ' ' || c == '\n') {
      String candidate = line.length() ? line + " " + word : word;
      // Arduino_GFX does not expose textWidth(); use the built-in font width.
      if ((int)candidate.length() * charWidth > maxWidth && line.length()) { gfx->println(line); y += lineHeight; gfx->setCursor(x, y); line = word; }
      else line = candidate;
      word = "";
      if (c == '\n') { gfx->println(line); y += lineHeight; gfx->setCursor(x, y); line = ""; }
    } else word += c;
  }
  if (line.length()) gfx->print(line);
}
void drawScreen() {
  if (cfg.ticker) {
    gfx->fillScreen(BLACK); gfx->setTextWrap(false); gfx->setTextColor(WHITE); gfx->setTextSize(2);
    gfx->setCursor(18, 15); gfx->print("P2000 INFO  ");
    gfx->setTextColor(CYAN); gfx->print(selectedRegionsLabel(38));
    drawConfigSymbol();
    gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(18, 45); gfx->print(statusLine);
    if (!alarmCount) { gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(18, 115); gfx->print("Geen meldingen"); }
    else {
      if (infoAlarmIndex >= alarmCount) infoAlarmIndex = 0;
      Alarm &a = alarms[infoAlarmIndex];
      gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(710, 45); gfx->printf("%u/%u", infoAlarmIndex + 1, alarmCount);
      gfx->drawFastHLine(12, 70, 776, DARKGREY);
      gfx->setTextColor(CYAN); gfx->setTextSize(2); gfx->setCursor(18, 78); gfx->print(a.region);
      gfx->drawRect(15, 105, 770, 95, CYAN);
      gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(30, 120); gfx->print(a.place);
      drawServiceIcon(serviceIcon(a), 735, 120);
      String caps = a.caps;
      caps.replace("\r", ""); caps.replace("\n", " | ");
      if (caps.length() > 58) caps = caps.substring(0, 55) + "...";
      gfx->setTextColor(YELLOW); gfx->setCursor(30, 155); gfx->print(a.time + "  " + caps);
      gfx->drawRect(15, 215, 770, 205, LIGHTGREY);
      int y = 235;
      gfx->setTextColor(WHITE); gfx->setTextSize(2);
      // drawWrapped only moves the cursor after it has wrapped a line.
      // Set its initial position explicitly so the message never starts in box 1.
      gfx->setCursor(30, y);
      drawWrapped(a.text, 30, y, 730, 28, 12);
      gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(18, 440); gfx->print("Veeg links/rechts voor vorige/volgende melding");
    }
    return;
  }
  gfx->fillScreen(BLACK); gfx->setTextWrap(false);
  gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(18, 15); gfx->print("P2000  ");
  gfx->setTextColor(CYAN); gfx->print(selectedRegionsLabel(44));
  drawConfigSymbol();
  gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(18, 43); gfx->print(statusLine);
  int y = 65;
  for (uint8_t i=firstVisibleAlarm; i<alarmCount && y < 440; ++i) {
    gfx->drawFastHLine(12, y, 776, DARKGREY); y += 7;
    // capstring may contain newlines.  Keep the list heading on exactly one line,
    // otherwise Arduino_GFX advances into the message area.
    String caps = alarms[i].caps;
    caps.replace("\r", ""); caps.replace("\n", " | ");
    if (caps.length() > 48) caps = caps.substring(0, 45) + "...";
    // Time and capcode deliberately occupy separate rows, keeping both readable.
    gfx->setTextSize(2); gfx->setTextColor(YELLOW); gfx->setCursor(18, y);
    gfx->print(alarms[i].time); y += 22;
    gfx->setCursor(18, y); gfx->print("CAP: " + caps); y += 28;
    drawServiceIcon(serviceIcon(alarms[i]), 752, y - 50);
    // Give drawWrapped its own, known cursor start. Its y reference tracks wrapped
    // lines, so the following divider is always placed below the entire message.
    gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(18, y);
    drawWrapped(alarms[i].text, 18, y, 755, 22, 12);
    y += 35;
  }
  if (!alarmCount) { gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(18, 110); gfx->print("Nog geen P2000-berichten"); }
  else { gfx->setTextSize(1); gfx->setTextColor(LIGHTGREY); gfx->setCursor(700, 43); gfx->printf("%u/%u", firstVisibleAlarm + 1, alarmCount); }
}


void drawConfigScreen() {
  gfx->fillScreen(BLACK); gfx->setTextWrap(false); gfx->setTextColor(WHITE); gfx->setTextSize(2);
  gfx->setCursor(18, 15); gfx->print("Configuratie");
  gfx->setTextSize(1); gfx->setTextColor(LIGHTGREY); gfx->setCursor(18, 45); gfx->print("Tik < of > om een regio te kiezen");
  for (uint8_t i = 0; i < 3; ++i) {
    int y = 85 + i * 75;
    gfx->drawRect(15, y, 80, 52, CYAN); gfx->setTextColor(CYAN); gfx->setTextSize(3); gfx->setCursor(42, y + 13); gfx->print("<");
    gfx->drawRect(105, y, 590, 52, DARKGREY); gfx->setTextColor(WHITE); gfx->setTextSize(1); gfx->setCursor(120, y + 19); gfx->print("Regio " + String(i + 1) + ": " + regionName(cfg.regions[i]));
    gfx->drawRect(705, y, 80, 52, CYAN); gfx->setTextColor(CYAN); gfx->setTextSize(3); gfx->setCursor(732, y + 13); gfx->print(">");
  }
  gfx->drawRect(105, 310, 590, 45, DARKGREY); gfx->setTextColor(WHITE); gfx->setTextSize(1); gfx->setCursor(120, 327);
  gfx->print(cfg.ticker ? "Weergave: Infoscherm - 1 kanaal" : "Weergave: Meldingenlijst");
  gfx->drawRect(20, 375, 300, 70, LIGHTGREY); gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(115, 400); gfx->print("Terug");
  gfx->drawRect(470, 375, 310, 70, GREEN); gfx->setTextColor(GREEN); gfx->setCursor(525, 400); gfx->print("Opslaan");
}
void cycleRegion(uint8_t slot, int direction) {
  int index = 0;
  for (uint8_t i = 0; i < REGION_COUNT; ++i) if (cfg.regions[slot] == REGIONS[i].id) { index = i; break; }
  index = (index + direction + REGION_COUNT) % REGION_COUNT;
  cfg.regions[slot] = REGIONS[index].id;
}
void handleTap(int x, int y) {
  if (screenMode == MESSAGES) {
    if (x >= 730 && y <= 65) { screenMode = CONFIG; drawConfigScreen(); }
    return;
  }
  if (y >= 85 && y < 295) {
    uint8_t slot = (y - 85) / 75;
    if (x < 100) { cycleRegion(slot, -1); drawConfigScreen(); }
    else if (x > 700) { cycleRegion(slot, 1); drawConfigScreen(); }
  } else if (y >= 305 && y < 360) {
    cfg.ticker = !cfg.ticker; drawConfigScreen();
  } else if (y >= 375 && y <= 450) {
    if (x >= 470) saveSettings();
    screenMode = MESSAGES; drawScreen();
  }
}

bool gt911Read(uint16_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(touchAddress); Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  // GT911 on this panel expects a STOP here (as in Elecrow's TAMC driver).
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)touchAddress, (int)length) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}
bool pca9557Write(uint8_t reg, uint8_t value) {
  // The board's PCA9557 is at 0x18.  P0 resets GT911; P1 is its interrupt.
  Wire.beginTransmission(0x18); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}
void resetTouchController() {
  // Required timing sequence for DIS07050H hardware revision V3.
  bool ok = pca9557Write(0x03, 0xFF); // reset: every expander pin is input
  ok = pca9557Write(0x01, 0xFF) && ok;
  ok = pca9557Write(0x02, 0x00) && ok;
  ok = pca9557Write(0x03, 0x00) && ok; // official V3 sequence: outputs
  ok = pca9557Write(0x01, 0xFE) && ok; // P0 low
  ok = pca9557Write(0x01, 0xFC) && ok; // P1 low
  delay(20);
  ok = pca9557Write(0x01, 0xFD) && ok; // release reset (P0 high)
  delay(100);
  ok = pca9557Write(0x03, 0xFE) && ok; // P1 becomes GT911 interrupt input
  Serial.printf("PCA9557 touch-reset: %s\n", ok ? "OK" : "niet gevonden");
}
void gt911Write(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(touchAddress); Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(value); Wire.endTransmission();
}
void initTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000); // GT911 on this board uses standard-mode I²C (100 kHz)
  Wire.setTimeOut(50);
  resetTouchController();
  touchAddress = 0x5D;
  Wire.beginTransmission(touchAddress);
  if (Wire.endTransmission() != 0) { touchAddress = 0; Serial.println("GT911 niet gevonden op 0x5D"); return; }
  gt911.begin(touchAddress);
  gt911.setRotation(ROTATION_INVERTED);
  Serial.println("GT911 driver gestart op 0x5D");
}
bool readTouch(int &x, int &y) {
  if (!touchAddress) return false;
  gt911.read();
  if (!gt911.isTouched) return false;
  x = gt911.points[0].x; y = gt911.points[0].y;
  return true;
}
void handleTouch() {
  int x, y;
  static int touchX, touchY;
  if (readTouch(x, y)) {
    touchX = x; touchY = y;
    if (!touchPressed) {
      touchPressed = true; swipeHandled = false; touchStartY = y; touchStartX = x;
      Serial.printf("Touch: x=%d y=%d\n", x, y);
    }
    else if (screenMode == MESSAGES && !swipeHandled) {
      if (!cfg.ticker && abs(y - touchStartY) >= 50) {
        if (y < touchStartY && firstVisibleAlarm + 1 < alarmCount) ++firstVisibleAlarm;
        if (y > touchStartY && firstVisibleAlarm > 0) --firstVisibleAlarm;
        swipeHandled = true; drawScreen();
      } else if (cfg.ticker && abs(x - touchStartX) >= 50) {
        if (x < touchStartX && infoAlarmIndex + 1 < alarmCount) ++infoAlarmIndex;
        if (x > touchStartX && infoAlarmIndex > 0) --infoAlarmIndex;
        swipeHandled = true; drawScreen();
      }
    }
  } else if (touchPressed) {
    if (!swipeHandled) handleTap(touchX, touchY);
    touchPressed = false;
  }
}

void pollApi() {
  if (WiFi.status() != WL_CONNECTED || !cfg.apiUrl.length()) return;
  // Alarmeringdroid /api2/find returns the latest batch. Filters are applied
  // locally in readAlarms(), preserving its documented response shape.
  String url = cfg.apiUrl;
  IPAddress apiIp;
  if (url.indexOf("beta.alarmeringdroid.nl") >= 0 && WiFi.hostByName("beta.alarmeringdroid.nl", apiIp) != 1) {
    statusLine = "API: DNS-fout"; Serial.println("DNS-fout: beta.alarmeringdroid.nl");
    nextPoll = millis() + 10000; if (screenMode == MESSAGES) drawScreen(); return;
  }
  Serial.printf("API ophalen; IP=%s RSSI=%d server=%s URL=%s\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(), apiIp.toString().c_str(), url.c_str());
  HTTPClient http; WiFiClientSecure secure; int code;
  http.setConnectTimeout(15000); http.setTimeout(20000);
  if (url.startsWith("https://")) {
    secure.setInsecure(); secure.setTimeout(15000);
    code = http.begin(secure, url) ? http.GET() : HTTPC_ERROR_CONNECTION_REFUSED;
  } else code = http.begin(url) ? http.GET() : HTTPC_ERROR_CONNECTION_REFUSED;
  if (code == HTTP_CODE_OK) readAlarms(http.getString());
  else {
    statusLine = "API: " + HTTPClient::errorToString(code);
    Serial.printf("API fout %d: %s\n", code, HTTPClient::errorToString(code).c_str());
    nextPoll = millis() + 10000; // network may just be reconnecting
  }
  http.end(); if (screenMode == MESSAGES) drawScreen();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  IPAddress dns1(1, 1, 1, 1), dns2(8, 8, 8, 8);
  // Some routers advertise a DNS server that does not answer ESP32 requests.
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);
  WiFi.setSleep(false);             // prevents missed packets on some S3 RGB boards
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  nextWifiRetry = millis() + 15000;
}

void startWeb() {
  server.on("/", HTTP_GET, [](){ server.send(200, "text/html; charset=utf-8", settingsPage()); });
  server.on("/health", HTTP_GET, [](){ server.send(200, "text/plain", "P2000-display webserver OK\n"); });
  server.on("/save", HTTP_POST, []() {
    cfg.ssid=server.arg("ssid"); cfg.apiUrl=server.arg("apiUrl");
    cfg.regions[0]=server.arg("region1"); cfg.regions[1]=server.arg("region2"); cfg.regions[2]=server.arg("region3");
    cfg.capcodes=server.arg("capcodes"); cfg.ticker = server.arg("display") == "ticker";
    String pass=server.arg("password"); if(pass.length()) cfg.password=pass;
    long requestedInterval = server.arg("interval").toInt();
    cfg.intervalSec = requestedInterval < 15 ? 15 : (uint32_t)requestedInterval;
    saveSettings(); server.send(200,"text/html","Opgeslagen. Herstarten..."); delay(800); ESP.restart();
  });
  server.onNotFound([](){ server.send(404, "text/plain", "Niet gevonden: " + server.uri()); });
  server.begin();
  Serial.println("Webserver gestart op poort 80");
}
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
  loadSettings();
  // The first boot is deliberately display-free. This keeps the RGB DMA
  // peripheral out of the way while a phone configures Wi-Fi over the AP.
  configurationMode = !cfg.ssid.length();
  // Initialise the TCP/IP stack before AsyncWebServer starts listening.
  if (cfg.ssid.length()) {
    connectWifi();
    statusLine = "Wifi verbinden...";
  } else {
    // Avoid 192.168.4.0/24: many consumer routers and ESP devices use it.
    IPAddress apIp(192, 168, 77, 1), gateway(192, 168, 77, 1), mask(255, 255, 255, 0);
    WiFi.mode(WIFI_AP); WiFi.softAPConfig(apIp, gateway, mask); delay(100);
    if (WiFi.softAP("P2000-display")) {
      statusLine = "AP: " + WiFi.softAPIP().toString();
      Serial.println("Configuratie: http://" + WiFi.softAPIP().toString());
    } else statusLine = "AP starten mislukt";
  }
  startWeb();
  if (!configurationMode) {
    // Establish the radio link before enabling the RGB DMA peripheral.
    unsigned long connectDeadline = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED && millis() < connectDeadline) delay(100);
    pinMode(GFX_BL, OUTPUT); digitalWrite(GFX_BL, HIGH);
    gfx->begin();
    initTouch();
    drawScreen();
  } else {
    Serial.println("Configuratiemodus: display uit, open de URL hierboven.");
  }
}
void loop() {
  server.handleClient();
  if (!configurationMode) handleTouch();
  if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED && millis() > nextWifiRetry) {
    statusLine = "Wifi opnieuw verbinden...";
    if (screenMode == MESSAGES) drawScreen();
    connectWifi();
  }
  if (WiFi.status() == WL_CONNECTED && millis() > nextPoll) { nextPoll = millis() + cfg.intervalSec * 1000UL; pollApi(); }
  delay(100);
}
