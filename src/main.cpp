#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
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
constexpr uint8_t SD_CS = 10, SD_MOSI = 11, SD_CLK = 12, SD_MISO = 13;
SPIClass sdSpi(FSPI);

struct Settings {
  String ssid, password, apiUrl, regions[3], capcodes;
  bool ticker = false;
  bool sdLogging = false;
  bool services[5] = {true, true, true, true, true};
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
unsigned long nextSdRetry = 0;
String statusLine = "Configuratie laden...";
bool sdReady = false;
bool configurationMode = false;
enum ScreenMode { MESSAGES, CONFIG, SERVICE_FILTER, WIFI_SCAN, WIFI_INPUT, SD_FORMAT_CONFIRM };
ScreenMode screenMode = MESSAGES;
constexpr uint8_t MAX_WIFI_NETWORKS = 8;
String scannedWifiSsids[MAX_WIFI_NETWORKS];
int32_t scannedWifiRssi[MAX_WIFI_NETWORKS];
uint8_t scannedWifiCount = 0;
bool wifiScanning = false;
int wifiScanResult = 0;
String wifiCandidateSsid;
bool manualWifiEntry = false;
enum WifiInputField : uint8_t { WIFI_SSID_FIELD, WIFI_PASSWORD_FIELD };
WifiInputField activeWifiField = WIFI_SSID_FIELD;
String wifiDraftSsid, wifiDraftPassword;
bool keyboardSymbols = false, keyboardUppercase = false;
Preferences prefs;
WebServer server(80);
TaskHandle_t webServerTaskHandle = nullptr;
void connectWifi();
String selectedRegionsLabel(uint8_t maxChars);
int8_t renderedMessageLayout = -1; // -1=invalid, 0=list, 1=info
String lastHeaderSignature;
String lastListCardSignature[3];
String lastInfoSignature;
String lastFooterSignature;

void invalidateMessageUi() {
  renderedMessageLayout = -1;
  lastHeaderSignature = "";
  lastInfoSignature = "";
  lastFooterSignature = "";
  for (String &signature : lastListCardSignature) signature = "";
}

// Compact 32 x 32 RGB565 icons.  They are drawn from a RGB565 pixel buffer,
// avoiding image files and keeping the display responsive.
enum ServiceIcon : uint8_t { ICON_NONE, ICON_FIRE, ICON_POLICE, ICON_AMBULANCE, ICON_HELICOPTER };
enum ServiceFilter : uint8_t { FILTER_FIRE, FILTER_POLICE, FILTER_AMBULANCE, FILTER_HELICOPTER, FILTER_OTHER };
constexpr uint8_t ICON_SIZE = 32;
constexpr uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_RED = 0xF800;
constexpr uint16_t C_BLUE = 0x001F, C_YELLOW = 0xFFE0, C_GREY = 0x8410;
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t UI_BG = rgb565(7, 17, 29);
constexpr uint16_t UI_SURFACE = rgb565(16, 29, 44);
constexpr uint16_t UI_SURFACE_2 = rgb565(23, 40, 59);
constexpr uint16_t UI_ACCENT = rgb565(0, 203, 234);
constexpr uint16_t UI_TEXT = rgb565(241, 245, 249);
constexpr uint16_t UI_MUTED = rgb565(164, 178, 195);
constexpr uint16_t UI_FIRE = rgb565(229, 72, 77);
constexpr uint16_t UI_POLICE = rgb565(59, 130, 246);
constexpr uint16_t UI_AMBULANCE = rgb565(34, 197, 94);
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
ServiceIcon classifyService(String source) {
  source.toLowerCase();
  if (source.indexOf("trauma") >= 0 || source.indexOf("lifeliner") >= 0 ||
      source.indexOf("mmt") >= 0 || source.indexOf("0120901") >= 0) return ICON_HELICOPTER;
  if (source.indexOf("brandweer") >= 0 || source.indexOf("brw") >= 0) return ICON_FIRE;
  if (source.indexOf("politie") >= 0) return ICON_POLICE;
  if (source.indexOf("ambulance") >= 0 || source.indexOf("rav") >= 0 || source.indexOf("mka") >= 0) return ICON_AMBULANCE;
  return ICON_NONE;
}
ServiceIcon serviceIcon(const Alarm &a) {
  // Alarmeringdroid links the service to its capcode. Keywords handle APIs
  // which expose only the capcode description instead of `dienst`.
  String source = a.service + " " + a.caps + " " + a.text;
  return classifyService(source);
}
uint8_t serviceFilterIndex(ServiceIcon icon) {
  switch (icon) {
    case ICON_FIRE: return FILTER_FIRE;
    case ICON_POLICE: return FILTER_POLICE;
    case ICON_AMBULANCE: return FILTER_AMBULANCE;
    case ICON_HELICOPTER: return FILTER_HELICOPTER;
    default: return FILTER_OTHER;
  }
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
String fitText(String value, size_t maxChars) {
  value.replace("\r", " "); value.replace("\n", " | ");
  while (value.indexOf("  ") >= 0) value.replace("  ", " ");
  if (value.length() > maxChars) value = value.substring(0, maxChars > 3 ? maxChars - 3 : maxChars) + "...";
  return value;
}

uint16_t alarmAccent(const Alarm &alarm) {
  switch (serviceIcon(alarm)) {
    case ICON_FIRE: return UI_FIRE;
    case ICON_POLICE: return UI_POLICE;
    case ICON_AMBULANCE: return UI_AMBULANCE;
    case ICON_HELICOPTER: return C_YELLOW;
    default: return UI_ACCENT;
  }
}

void drawUiHeader(const char *title, uint8_t regionMaxChars = 40) {
  gfx->fillRect(0, 0, 800, 70, UI_SURFACE);
  gfx->drawFastHLine(0, 67, 800, UI_ACCENT);
  gfx->drawFastHLine(0, 68, 800, UI_ACCENT);
  gfx->drawFastHLine(0, 69, 800, UI_ACCENT);
  // Strong title block copied from the approved SquareLine header design.
  gfx->fillRoundRect(16, 10, 6, 43, 3, UI_ACCENT);
  gfx->setTextColor(UI_TEXT); gfx->setTextSize(3); gfx->setCursor(32, 8); gfx->print(title);
  gfx->setTextColor(UI_MUTED); gfx->setTextSize(1); gfx->setCursor(33, 47);
  gfx->print(fitText(selectedRegionsLabel(regionMaxChars), regionMaxChars));
  if (WiFi.status() == WL_CONNECTED) gfx->print("  |  WiFi verbonden");
  else gfx->print("  |  WiFi offline");
  gfx->fillRoundRect(654, 12, 118, 44, 9, UI_SURFACE_2);
  gfx->drawRoundRect(654, 12, 118, 44, 9, UI_ACCENT);
  gfx->setTextColor(UI_ACCENT); gfx->setTextSize(1); gfx->setCursor(689, 30); gfx->print("CONFIG");
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<style>body{font:16px system-ui;max-width:640px;margin:2em auto;padding:0 1em}input,select{width:100%;box-sizing:border-box;padding:.6em;margin:.2em 0 1em}input[type=checkbox]{width:auto;margin:.5em}fieldset{margin:0 0 1em}fieldset label{display:block}button{padding:.7em 1.2em}small{color:#555}</style>
<h1>P2000 display</h1><form method=post action=/save>
<label>Wifi-naam</label><input name=ssid required value="%SSID%">
<label>Wifi-wachtwoord</label><input name=password type=password placeholder="ongewijzigd laten om te bewaren">
<label>P2000 API URL</label><input name=apiUrl required value="%URL%"><small>Standaard: Alarmeringdroid API v2. Regio- en capcodefilters gebeuren op de ESP32.</small>
<label>Regio 1</label><select name=region1>%REGION1_OPTIONS%</select>
<label>Regio 2</label><select name=region2>%REGION2_OPTIONS%</select>
<label>Regio 3</label><select name=region3>%REGION3_OPTIONS%</select>
<label>Weergave</label><select name=display><option value="list" %LIST_SELECTED%>Meldingenlijst</option><option value="ticker" %TICKER_SELECTED%>Infoscherm - 1 kanaal</option></select>
<fieldset><legend>Diensten weergeven</legend>
<label><input type=checkbox name=fire %FIRE_CHECKED%> Brandweer</label>
<label><input type=checkbox name=police %POLICE_CHECKED%> Politie</label>
<label><input type=checkbox name=ambulance %AMBULANCE_CHECKED%> Ambulance</label>
<label><input type=checkbox name=helicopter %HELICOPTER_CHECKED%> Lifeliner / traumaheli</label>
<label><input type=checkbox name=other %OTHER_CHECKED%> Overig</label></fieldset>
<label>SD-kaart logging</label><select name=sdlog><option value="on" %SDLOG_ON%>Aan</option><option value="off" %SDLOG_OFF%>Uit</option></select>
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
  String formSsid = manualWifiEntry ? "" : (wifiCandidateSsid.length() ? wifiCandidateSsid : cfg.ssid);
  page.replace("%SSID%", htmlEscape(formSsid)); page.replace("%URL%", tpl("URL"));
  page.replace("%REGION1_OPTIONS%", regionOptions(cfg.regions[0]));
  page.replace("%REGION2_OPTIONS%", regionOptions(cfg.regions[1]));
  page.replace("%REGION3_OPTIONS%", regionOptions(cfg.regions[2])); page.replace("%CAPS%", tpl("CAPS"));
  page.replace("%LIST_SELECTED%", cfg.ticker ? "" : "selected");
  page.replace("%TICKER_SELECTED%", cfg.ticker ? "selected" : "");
  const char *serviceTokens[] = {"%FIRE_CHECKED%", "%POLICE_CHECKED%", "%AMBULANCE_CHECKED%", "%HELICOPTER_CHECKED%", "%OTHER_CHECKED%"};
  for (uint8_t i = 0; i < 5; ++i) page.replace(serviceTokens[i], cfg.services[i] ? "checked" : "");
  page.replace("%SDLOG_ON%", cfg.sdLogging ? "selected" : "");
  page.replace("%SDLOG_OFF%", cfg.sdLogging ? "" : "selected");
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
  cfg.sdLogging = prefs.isKey("sdlog") ? prefs.getBool("sdlog") : false;
  const char *serviceKeys[] = {"svcFire", "svcPolice", "svcAmb", "svcHeli", "svcOther"};
  for (uint8_t i = 0; i < 5; ++i) cfg.services[i] = prefs.isKey(serviceKeys[i]) ? prefs.getBool(serviceKeys[i]) : true;
  cfg.intervalSec = prefs.isKey("int") ? prefs.getUInt("int") : 60;
  prefs.end();
}
void saveSettings() {
  prefs.begin("p2000", false);
  prefs.putString("ssid", cfg.ssid); prefs.putString("pass", cfg.password); prefs.putString("url", cfg.apiUrl);
  prefs.putString("reg1", cfg.regions[0]); prefs.putString("reg2", cfg.regions[1]); prefs.putString("reg3", cfg.regions[2]);
  prefs.putString("caps", cfg.capcodes); prefs.putBool("ticker", cfg.ticker); prefs.putBool("sdlog", cfg.sdLogging); prefs.putUInt("int", cfg.intervalSec);
  const char *serviceKeys[] = {"svcFire", "svcPolice", "svcAmb", "svcHeli", "svcOther"};
  for (uint8_t i = 0; i < 5; ++i) prefs.putBool(serviceKeys[i], cfg.services[i]);
  prefs.end();
}

bool initSdCard(bool formatIfEmpty = false) {
  if (sdReady) return true;
  sdSpi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS, sdSpi, 20000000, "/sd", 5, formatIfEmpty);
  Serial.printf("SD-kaart: %s\n", sdReady ? "gereed" : "niet gevonden");
  return sdReady;
}
bool formatSdCard() {
  // A new/exFAT/corrupt card cannot be mounted yet. Arduino-ESP32 can create
  // the FAT filesystem directly when format_if_empty is enabled.
  if (!sdReady) {
    if (!initSdCard(true)) { statusLine = "SD-kaart niet bereikbaar"; return false; }
    statusLine = "SD-kaart geformatteerd";
    return true;
  }
  // Arduino-ESP32's SD wrapper formats a card when a mount fails and
  // format_if_empty is true. Clearing the boot sector deliberately triggers
  // that supported formatter, which recreates a fresh FAT filesystem.
  uint8_t emptyBootSector[512] = {};
  if (!SD.writeRAW(emptyBootSector, 0)) { statusLine = "SD formatteren mislukt"; return false; }
  SD.end(); sdReady = false; delay(100);
  if (!initSdCard(true)) { statusLine = "SD formatteren mislukt"; return false; }
  statusLine = "SD-kaart geformatteerd";
  return true;
}
String csvValue(String value) {
  value.replace("\r", " "); value.replace("\n", " "); value.replace("\"", "\"\"");
  return "\"" + value + "\"";
}
void appendAlarmLog(const Alarm &alarm) {
  if (!cfg.sdLogging || !sdReady) return;
  bool newFile = !SD.exists("/p2000.csv");
  File log = SD.open("/p2000.csv", FILE_APPEND);
  if (!log) { sdReady = false; Serial.println("SD-logbestand openen mislukt"); return; }
  if (newFile) log.println("id,tijd,regio,plaats,capcodes,melding");
  log.println(csvValue(alarm.id) + "," + csvValue(alarm.time) + "," + csvValue(alarm.region) + "," +
              csvValue(alarm.place) + "," + csvValue(alarm.caps) + "," + csvValue(alarm.text));
  log.close();
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
bool serviceMatches(const Alarm &alarm) {
  return cfg.services[serviceFilterIndex(serviceIcon(alarm))];
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
  String previousIds[MAX_ALARMS];
  uint8_t previousCount = alarmCount;
  for (uint8_t i = 0; i < previousCount; ++i) previousIds[i] = alarms[i].id;
  alarmCount = 0;
  for (JsonObject item : list) {
    if (!matchesFilters(item)) continue;
    if (alarmCount >= MAX_ALARMS) break;
    Alarm &a = alarms[alarmCount];
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
    if (!serviceMatches(a)) continue;
    ++alarmCount;
    bool alreadyListed = false;
    for (uint8_t i = 0; i < previousCount; ++i) if (a.id.length() && a.id == previousIds[i]) { alreadyListed = true; break; }
    if (!alreadyListed) appendAlarmLog(a);
  }
  statusLine = String(alarmCount) + " berichten bijgewerkt";
  if (alarmCount) {
    bool newMessage = newestInfoId.length() && alarms[0].id != newestInfoId;
    if (!newestInfoId.length() || newMessage) {
      infoAlarmIndex = 0;
      firstVisibleAlarm = 0;
    } else {
      if (firstVisibleAlarm >= alarmCount) firstVisibleAlarm = alarmCount - 1;
      for (uint8_t i = 0; i < alarmCount; ++i)
        if (alarms[i].id == currentlyShownId) { infoAlarmIndex = i; break; }
    }
    newestInfoId = alarms[0].id;
  } else firstVisibleAlarm = 0;
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

String alarmSignature(const Alarm &alarm) {
  return alarm.id + "\x1f" + alarm.time + "\x1f" + alarm.caps + "\x1f" + alarm.service +
         "\x1f" + alarm.region + "\x1f" + alarm.place + "\x1f" + alarm.text;
}

String headerSignature(const char *title, uint8_t regionChars) {
  return String(title) + "\x1f" + selectedRegionsLabel(regionChars) + "\x1f" +
         String((int)WiFi.status());
}

void drawScreen() {
  if (cfg.ticker) {
    bool fullRedraw = renderedMessageLayout != 1;
    gfx->setTextWrap(false);
    if (fullRedraw) {
      gfx->fillScreen(UI_BG);
      lastHeaderSignature = ""; lastInfoSignature = ""; lastFooterSignature = "";
      renderedMessageLayout = 1;
    }
    String currentHeader = headerSignature("P2000 INFO", 38);
    if (fullRedraw || currentHeader != lastHeaderSignature) {
      drawUiHeader("P2000 INFO", 38);
      lastHeaderSignature = currentHeader;
    }
    String currentInfo = statusLine + "\x1f" + String(alarmCount) + "\x1f" + String(infoAlarmIndex);
    if (alarmCount && infoAlarmIndex < alarmCount) currentInfo += "\x1f" + alarmSignature(alarms[infoAlarmIndex]);
    if (fullRedraw || currentInfo != lastInfoSignature) {
      // Clear only the content area. The RGB framebuffer and header remain visible.
      gfx->fillRect(0, 70, 800, 370, UI_BG);
      gfx->setTextColor(UI_MUTED); gfx->setTextSize(1); gfx->setCursor(24, 78); gfx->print(fitText(statusLine, 70));
      if (!alarmCount) {
        gfx->fillRoundRect(24, 118, 752, 210, 12, UI_SURFACE);
        gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(294, 208); gfx->print("Geen meldingen");
      } else {
      if (infoAlarmIndex >= alarmCount) infoAlarmIndex = 0;
      Alarm &a = alarms[infoAlarmIndex];
      uint16_t accent = alarmAccent(a);
      gfx->setTextColor(UI_MUTED); gfx->setTextSize(1); gfx->setCursor(720, 78); gfx->printf("%u/%u", infoAlarmIndex + 1, alarmCount);
      gfx->fillRoundRect(24, 104, 752, 106, 12, UI_SURFACE);
      gfx->fillRoundRect(24, 104, 8, 106, 4, accent);
      drawServiceIcon(serviceIcon(a), 48, 128);
      gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(98, 121);
      gfx->print(fitText(a.place.length() ? a.place : a.region, 48));
      gfx->setTextColor(accent); gfx->setTextSize(1); gfx->setCursor(98, 153);
      gfx->print(fitText(a.time + "  |  " + a.caps, 86));
      gfx->setTextColor(UI_MUTED); gfx->setCursor(98, 178); gfx->print(fitText(a.region, 75));
      gfx->fillRoundRect(24, 224, 752, 202, 12, UI_SURFACE);
      int y = 246;
      gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(44, y);
      drawWrapped(a.text, 44, y, 710, 28, 12);
      }
      lastInfoSignature = currentInfo;
    }
    String footer = String(alarmCount) + "\x1f" + String(infoAlarmIndex);
    if (fullRedraw || footer != lastFooterSignature) {
      gfx->fillRect(0, 440, 800, 40, UI_BG);
      if (alarmCount) {
        gfx->setTextColor(UI_MUTED); gfx->setTextSize(1); gfx->setCursor(24, 452);
        gfx->print("Veeg links/rechts voor vorige/volgende melding");
      }
      lastFooterSignature = footer;
    }
    return;
  }
  bool fullRedraw = renderedMessageLayout != 0;
  gfx->setTextWrap(false);
  if (fullRedraw) {
    gfx->fillScreen(UI_BG);
    lastHeaderSignature = ""; lastFooterSignature = "";
    for (String &signature : lastListCardSignature) signature = "";
    renderedMessageLayout = 0;
    gfx->setTextColor(UI_ACCENT); gfx->setTextSize(1); gfx->setCursor(24, 82); gfx->print("LAATSTE MELDINGEN");
  }
  String currentHeader = headerSignature("P2000", 42);
  if (fullRedraw || currentHeader != lastHeaderSignature) {
    drawUiHeader("P2000", 42);
    lastHeaderSignature = currentHeader;
  }
  for (uint8_t visible = 0; visible < 3; ++visible) {
    uint8_t index = firstVisibleAlarm + visible;
    String currentCard = index < alarmCount ? alarmSignature(alarms[index]) : "<leeg>";
    if (!fullRedraw && currentCard == lastListCardSignature[visible]) continue;
    int y = 106 + visible * 110;
    // This rectangle fully covers the old rounded card without touching neighbours.
    gfx->fillRect(20, y - 2, 760, 104, UI_BG);
    if (index < alarmCount) {
      Alarm &a = alarms[index];
      uint16_t accent = alarmAccent(a);
      gfx->fillRoundRect(24, y, 752, 100, 12, UI_SURFACE);
      gfx->fillRoundRect(24, y, 8, 100, 4, accent);
      gfx->fillCircle(66, y + 50, 27, accent);
      drawServiceIcon(serviceIcon(a), 50, y + 34);
      gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(106, y + 13);
      gfx->print(fitText(a.place.length() ? a.place : a.region, 48));
      gfx->setTextColor(UI_ACCENT); gfx->setTextSize(1); gfx->setCursor(106, y + 44);
      gfx->print(fitText(a.time + "  |  Capcode " + a.caps, 86));
      gfx->setTextColor(UI_MUTED); gfx->setCursor(106, y + 69); gfx->print(fitText(a.text, 94));
    }
    lastListCardSignature[visible] = currentCard;
  }
  String footer = String(alarmCount) + "\x1f" + String(firstVisibleAlarm);
  if (fullRedraw || footer != lastFooterSignature) {
    gfx->fillRect(0, 438, 800, 42, UI_BG);
    if (!alarmCount) {
    gfx->fillRoundRect(24, 118, 752, 118, 12, UI_SURFACE);
    gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(258, 164); gfx->print("Nog geen P2000-berichten");
    } else {
      gfx->setTextSize(1); gfx->setTextColor(UI_MUTED); gfx->setCursor(620, 452);
      gfx->printf("Veeg omhoog  |  %u/%u", firstVisibleAlarm + 1, alarmCount);
    }
    lastFooterSignature = footer;
  }
}


void drawConfigScreen() {
  invalidateMessageUi();
  gfx->fillScreen(UI_BG); gfx->setTextWrap(false);
  gfx->fillRect(0, 0, 800, 70, UI_SURFACE); gfx->fillRect(0, 67, 800, 3, UI_ACCENT);
  gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(20, 11); gfx->print("Configuratie");
  gfx->fillRoundRect(485, 8, 140, 45, 9, UI_SURFACE_2); gfx->drawRoundRect(485, 8, 140, 45, 9, UI_ACCENT);
  gfx->setTextColor(UI_ACCENT); gfx->setTextSize(1); gfx->setCursor(505, 25); gfx->print("Scan WiFi");
  gfx->fillRoundRect(635, 8, 150, 45, 9, UI_SURFACE_2); gfx->drawRoundRect(635, 8, 150, 45, 9, UI_ACCENT);
  gfx->setCursor(655, 25); gfx->print("Handmatig");
  gfx->setTextSize(1); gfx->setTextColor(UI_MUTED); gfx->setCursor(20, 47);
  if (wifiCandidateSsid.length()) gfx->print("Gekozen: " + wifiCandidateSsid + "  |  AP: 192.168.77.1");
  else if (manualWifiEntry) gfx->print("Handmatig: vul SSID en wachtwoord op het scherm in");
  else gfx->print("Tik < of > om een regio te kiezen");
  for (uint8_t i = 0; i < 3; ++i) {
    int y = 85 + i * 75;
    gfx->fillRoundRect(15, y, 80, 52, 9, UI_SURFACE_2); gfx->drawRoundRect(15, y, 80, 52, 9, UI_ACCENT);
    gfx->setTextColor(UI_ACCENT); gfx->setTextSize(3); gfx->setCursor(42, y + 13); gfx->print("<");
    gfx->fillRoundRect(105, y, 590, 52, 9, UI_SURFACE); gfx->setTextColor(UI_TEXT); gfx->setTextSize(1); gfx->setCursor(120, y + 19);
    gfx->print("Regio " + String(i + 1) + ": " + regionName(cfg.regions[i]));
    gfx->fillRoundRect(705, y, 80, 52, 9, UI_SURFACE_2); gfx->drawRoundRect(705, y, 80, 52, 9, UI_ACCENT);
    gfx->setTextColor(UI_ACCENT); gfx->setTextSize(3); gfx->setCursor(732, y + 13); gfx->print(">");
  }
  gfx->fillRoundRect(105, 300, 590, 34, 8, UI_SURFACE); gfx->setTextColor(UI_TEXT); gfx->setTextSize(1); gfx->setCursor(120, 312);
  gfx->print("Dienstenfilter: tik om te wijzigen");
  gfx->fillRoundRect(105, 340, 590, 34, 8, UI_SURFACE); gfx->setTextColor(UI_TEXT); gfx->setCursor(120, 352);
  gfx->print(cfg.ticker ? "Weergave: Infoscherm - 1 kanaal" : "Weergave: Meldingenlijst");
  gfx->fillRoundRect(105, 380, 590, 34, 8, UI_SURFACE); gfx->setTextColor(cfg.sdLogging ? UI_AMBULANCE : UI_MUTED); gfx->setCursor(120, 392);
  gfx->print(cfg.sdLogging ? "SD-kaart logging: Aan" : "SD-kaart logging: Uit");
  gfx->fillRoundRect(20, 425, 185, 45, 10, UI_SURFACE_2); gfx->drawRoundRect(20, 425, 185, 45, 10, UI_ACCENT);
  gfx->setTextColor(UI_ACCENT); gfx->setTextSize(1); gfx->setCursor(90, 442); gfx->print("Terug");
  gfx->fillRoundRect(220, 425, 230, 45, 10, UI_SURFACE_2); gfx->drawRoundRect(220, 425, 230, 45, 10, UI_FIRE);
  gfx->setTextColor(UI_FIRE); gfx->setCursor(285, 442); gfx->print("Formatteer SD");
  gfx->fillRoundRect(470, 425, 310, 45, 10, UI_ACCENT); gfx->setTextColor(UI_BG); gfx->setTextSize(1); gfx->setCursor(600, 442); gfx->print("Opslaan");
}
void drawServiceFilterScreen() {
  invalidateMessageUi();
  static const char *names[] = {"Brandweer", "Politie", "Ambulance", "Lifeliner / traumaheli", "Overig"};
  gfx->fillScreen(UI_BG); gfx->setTextWrap(false);
  gfx->fillRect(0, 0, 800, 70, UI_SURFACE); gfx->fillRect(0, 67, 800, 3, UI_ACCENT);
  gfx->setTextColor(UI_TEXT); gfx->setTextSize(2); gfx->setCursor(20, 20); gfx->print("Dienstenfilter");
  for (uint8_t i = 0; i < 5; ++i) {
    int y = 85 + i * 62;
    gfx->fillRoundRect(45, y, 710, 48, 9, UI_SURFACE);
    gfx->fillRoundRect(60, y + 9, 30, 30, 6, cfg.services[i] ? UI_ACCENT : UI_SURFACE_2);
    gfx->drawRoundRect(60, y + 9, 30, 30, 6, UI_ACCENT);
    if (cfg.services[i]) { gfx->setTextColor(UI_BG); gfx->setTextSize(2); gfx->setCursor(68, y + 15); gfx->print("X"); }
    gfx->setTextColor(cfg.services[i] ? UI_TEXT : UI_MUTED); gfx->setTextSize(2); gfx->setCursor(115, y + 15); gfx->print(names[i]);
  }
  gfx->fillRoundRect(45, 415, 710, 50, 9, UI_ACCENT); gfx->setTextColor(UI_BG); gfx->setTextSize(2); gfx->setCursor(345, 432); gfx->print("Gereed");
}
void drawSdFormatConfirmScreen() {
  invalidateMessageUi();
  gfx->fillScreen(BLACK); gfx->setTextWrap(false); gfx->setTextColor(RED); gfx->setTextSize(2);
  gfx->setCursor(18, 20); gfx->print("SD-kaart formatteren");
  gfx->setTextColor(WHITE); gfx->setCursor(18, 90); gfx->print("Alle bestanden op de SD-kaart");
  gfx->setCursor(18, 120); gfx->print("worden permanent gewist.");
  gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(18, 165); gfx->print("De kaart wordt opnieuw als FAT geformatteerd voor P2000-logging.");
  gfx->drawRect(25, 265, 330, 90, LIGHTGREY); gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(135, 295); gfx->print("Annuleren");
  gfx->drawRect(445, 265, 330, 90, RED); gfx->setTextColor(RED); gfx->setCursor(500, 295); gfx->print("JA, WISSEN");
}
void drawWifiScanScreen() {
  invalidateMessageUi();
  gfx->fillScreen(BLACK); gfx->setTextWrap(false); gfx->setTextColor(WHITE); gfx->setTextSize(2);
  gfx->setCursor(18, 15); gfx->print("WiFi-netwerken");
  gfx->setTextSize(1); gfx->setTextColor(LIGHTGREY); gfx->setCursor(18, 45);
  if (wifiScanning) gfx->print("Zoeken naar netwerken...");
  else if (wifiScanResult < 0) gfx->printf("WiFi-scan fout (%d); gebruik Handmatig", wifiScanResult);
  else if (!scannedWifiCount) gfx->print("Geen netwerken gevonden");
  else gfx->print("Tik een netwerk aan; het SSID-veld wordt ingevuld");
  for (uint8_t i = 0; i < scannedWifiCount; ++i) {
    int y = 65 + i * 44;
    gfx->drawRect(15, y, 770, 36, CYAN); gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(30, y + 10);
    String ssid = scannedWifiSsids[i]; if (ssid.length() > 34) ssid = ssid.substring(0, 31) + "...";
    gfx->print(ssid);
    gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(680, y + 13); gfx->printf("%ld dBm", (long)scannedWifiRssi[i]);
  }
  gfx->drawRect(20, 425, 300, 45, LIGHTGREY); gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(115, 438); gfx->print("Terug");
}
const char *keyboardRow(uint8_t row) {
  static const char *alpha[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890" };
  static const char *symbols[] = { "!@#$%^&*()", "_-+=:;,.?", "[]{}<>/\\|", "0123456789" };
  return keyboardSymbols ? symbols[row] : alpha[row];
}
String visiblePassword() {
  // The local touchscreen is used for initial setup, so show exactly what was
  // entered to make uppercase letters, digits and symbols easy to verify.
  return wifiDraftPassword;
}
void drawWifiInputScreen() {
  invalidateMessageUi();
  gfx->fillScreen(BLACK); gfx->setTextWrap(false); gfx->setTextColor(WHITE); gfx->setTextSize(2);
  gfx->setCursor(18, 12); gfx->print("WiFi instellen");
  gfx->setTextColor(CYAN); gfx->setCursor(680, 12); gfx->print("Terug");
  gfx->setTextSize(1); gfx->setTextColor(LIGHTGREY); gfx->setCursor(18, 38); gfx->print("Tik een veld aan en gebruik het toetsenbord");
  gfx->drawRect(15, 52, 770, 40, activeWifiField == WIFI_SSID_FIELD ? CYAN : DARKGREY);
  gfx->setTextColor(LIGHTGREY); gfx->setCursor(25, 58); gfx->print("SSID");
  gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(95, 63); gfx->print(wifiDraftSsid);
  gfx->drawRect(15, 105, 770, 40, activeWifiField == WIFI_PASSWORD_FIELD ? CYAN : DARKGREY);
  gfx->setTextColor(LIGHTGREY); gfx->setTextSize(1); gfx->setCursor(25, 111); gfx->print("Wachtwoord");
  gfx->setTextColor(WHITE); gfx->setTextSize(2); gfx->setCursor(145, 116); gfx->print(visiblePassword());
  for (uint8_t row = 0; row < 4; ++row) {
    String keys = keyboardRow(row); int width = 760 / keys.length(); int y = 160 + row * 43;
    for (uint8_t key = 0; key < keys.length(); ++key) {
      int x = 20 + key * width;
      gfx->drawRect(x, y, width - 3, 37, DARKGREY); gfx->setTextColor(WHITE); gfx->setTextSize(2);
      gfx->setCursor(x + width / 2 - 6, y + 10); gfx->print(keys[key]);
    }
  }
  gfx->setTextSize(1);
  gfx->drawRect(15, 350, 85, 75, LIGHTGREY); gfx->setTextColor(WHITE); gfx->setCursor(28, 380); gfx->print("Spatie");
  gfx->drawRect(110, 350, 75, 75, CYAN); gfx->setTextColor(CYAN); gfx->setCursor(130, 380); gfx->print("aA");
  gfx->drawRect(195, 350, 105, 75, CYAN); gfx->setCursor(215, 380); gfx->print("?123");
  gfx->drawRect(310, 350, 115, 75, YELLOW); gfx->setTextColor(YELLOW); gfx->setCursor(342, 380); gfx->print("Wis");
  gfx->drawRect(435, 350, 350, 75, GREEN); gfx->setTextColor(GREEN); gfx->setTextSize(2); gfx->setCursor(535, 375); gfx->print("Opslaan");
}
void openWifiInput(const String &ssid) {
  wifiDraftSsid = ssid; wifiDraftPassword = ""; activeWifiField = WIFI_SSID_FIELD;
  keyboardSymbols = false; keyboardUppercase = false; screenMode = WIFI_INPUT; drawWifiInputScreen();
}
void saveWifiInput() {
  if (!wifiDraftSsid.length()) { statusLine = "WiFi-naam ontbreekt"; drawWifiInputScreen(); return; }
  cfg.ssid = wifiDraftSsid; cfg.password = wifiDraftPassword; wifiCandidateSsid = ""; manualWifiEntry = false;
  saveSettings(); WiFi.softAPdisconnect(true); WiFi.disconnect(); connectWifi();
  statusLine = "Wifi opnieuw verbinden..."; screenMode = MESSAGES; drawScreen();
}
void startConfigurationAp() {
  IPAddress apIp(192, 168, 77, 1), gateway(192, 168, 77, 1), mask(255, 255, 255, 0);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, gateway, mask);
  if (WiFi.softAP("P2000-display")) {
    statusLine = "Config AP: " + WiFi.softAPIP().toString();
    Serial.println("Wifi opnieuw instellen: verbind met P2000-display, http://" + WiFi.softAPIP().toString());
  } else statusLine = "Config-AP starten mislukt";
}
void scanWifiNetworks() {
  wifiScanning = true; scannedWifiCount = 0; wifiScanResult = 0; drawWifiScanScreen();
  // ESP32 returns -2 (ESP_ERR_WIFI_STATE) when a scan is started while STA is
  // still connecting. Stop that attempt without erasing credentials, scan, then
  // reconnect after the results are copied.
  bool reconnectAfterScan = cfg.ssid.length();
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  delay(300);
  int found = WiFi.scanNetworks(false, true, false, 400);
  wifiScanResult = found;
  Serial.printf("WiFi-scan resultaat: %d netwerken\n", found);
  if (found > 0) {
    for (int i = 0; i < found && scannedWifiCount < MAX_WIFI_NETWORKS; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      scannedWifiSsids[scannedWifiCount] = ssid;
      scannedWifiRssi[scannedWifiCount++] = WiFi.RSSI(i);
      Serial.printf("  %s (%ld dBm)\n", ssid.c_str(), (long)scannedWifiRssi[scannedWifiCount - 1]);
    }
  }
  WiFi.scanDelete(); wifiScanning = false; drawWifiScanScreen();
  if (reconnectAfterScan) connectWifi();
}
void cycleRegion(uint8_t slot, int direction) {
  int index = 0;
  for (uint8_t i = 0; i < REGION_COUNT; ++i) if (cfg.regions[slot] == REGIONS[i].id) { index = i; break; }
  index = (index + direction + REGION_COUNT) % REGION_COUNT;
  cfg.regions[slot] = REGIONS[index].id;
}
void handleTap(int x, int y) {
  if (screenMode == MESSAGES) {
    if (x >= 645 && x <= 785 && y <= 65) { screenMode = CONFIG; drawConfigScreen(); }
    return;
  }
  if (screenMode == WIFI_SCAN) {
    if (y >= 65 && y < 65 + scannedWifiCount * 44) {
      uint8_t index = (y - 65) / 44;
      wifiCandidateSsid = scannedWifiSsids[index]; manualWifiEntry = false;
      openWifiInput(wifiCandidateSsid);
    } else if (y >= 425) { screenMode = CONFIG; drawConfigScreen(); }
    return;
  }
  if (screenMode == WIFI_INPUT) {
    if (x >= 660 && y <= 50) { screenMode = CONFIG; drawConfigScreen(); return; }
    if (y >= 52 && y < 95) { activeWifiField = WIFI_SSID_FIELD; drawWifiInputScreen(); return; }
    if (y >= 105 && y < 148) { activeWifiField = WIFI_PASSWORD_FIELD; drawWifiInputScreen(); return; }
    if (y >= 160 && y < 332 && x >= 20 && x < 780) {
      uint8_t row = (y - 160) / 43;
      String keys = keyboardRow(row); int width = 760 / keys.length(); uint8_t key = (x - 20) / width;
      if (key < keys.length()) {
        char character = keys[key];
        if (!keyboardSymbols && keyboardUppercase && character >= 'a' && character <= 'z') character -= ('a' - 'A');
        if (activeWifiField == WIFI_SSID_FIELD) wifiDraftSsid += character;
        else wifiDraftPassword += character;
        drawWifiInputScreen();
      }
      return;
    }
    if (y >= 350 && y <= 430) {
      if (x < 105) {
        if (activeWifiField == WIFI_SSID_FIELD) wifiDraftSsid += ' '; else wifiDraftPassword += ' ';
      } else if (x < 190) keyboardUppercase = !keyboardUppercase;
      else if (x < 305) keyboardSymbols = !keyboardSymbols;
      else if (x < 430) {
        String &value = activeWifiField == WIFI_SSID_FIELD ? wifiDraftSsid : wifiDraftPassword;
        if (value.length()) value.remove(value.length() - 1);
      } else saveWifiInput();
      if (screenMode == WIFI_INPUT) drawWifiInputScreen();
    }
    return;
  }
  if (screenMode == SD_FORMAT_CONFIRM) {
    if (y >= 265 && y <= 360 && x >= 445) {
      formatSdCard(); screenMode = CONFIG; drawConfigScreen();
    } else if (y >= 265 && y <= 360) { screenMode = CONFIG; drawConfigScreen(); }
    return;
  }
  if (screenMode == SERVICE_FILTER) {
    if (y >= 85 && y < 395) {
      uint8_t service = (y - 85) / 62;
      if (service < 5) { cfg.services[service] = !cfg.services[service]; drawServiceFilterScreen(); }
    } else if (y >= 415) { screenMode = CONFIG; drawConfigScreen(); }
    return;
  }
  if (y <= 65 && x >= 635) {
    wifiCandidateSsid = ""; manualWifiEntry = true; openWifiInput(""); return;
  }
  if (y <= 65 && x >= 485) { screenMode = WIFI_SCAN; scanWifiNetworks(); return; }
  if (y >= 85 && y < 295) {
    uint8_t slot = (y - 85) / 75;
    if (x < 100) { cycleRegion(slot, -1); drawConfigScreen(); }
    else if (x > 700) { cycleRegion(slot, 1); drawConfigScreen(); }
  } else if (y >= 300 && y < 335) {
    screenMode = SERVICE_FILTER; drawServiceFilterScreen();
  } else if (y >= 340 && y < 375) {
    cfg.ticker = !cfg.ticker; drawConfigScreen();
  } else if (y >= 380 && y < 415) {
    cfg.sdLogging = !cfg.sdLogging; drawConfigScreen();
  } else if (y >= 425 && y <= 470) {
    if (x >= 470) { saveSettings(); if (cfg.sdLogging) initSdCard(); screenMode = MESSAGES; drawScreen(); }
    else if (x >= 220) { screenMode = SD_FORMAT_CONFIRM; drawSdFormatConfirmScreen(); }
    else { screenMode = MESSAGES; drawScreen(); }
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
  // Touch packets are read directly below. Do not call TAMC_GT911::begin():
  // its reset code calls pinMode(-1) on this panel's PCA9557-managed pins.
  Serial.println("GT911 I2C gestart op 0x5D");
}
bool readTouch(int &x, int &y) {
  if (!touchAddress) return false;
  // The TAMC driver reads from Wire even after a failed request, which produces
  // "i2cread error return -1" on a busy S3. Read status and point data only
  // when the GT911 has marked a complete touch packet as available.
  static uint8_t readFailures = 0;
  static unsigned long nextRecovery = 0;
  uint8_t status;
  if (!gt911Read(0x814E, &status, 1)) {
    if (++readFailures >= 3 && millis() >= nextRecovery) {
      Serial.println("GT911 I2C-fout; touch opnieuw starten");
      nextRecovery = millis() + 2000;
      initTouch();
      readFailures = 0;
    }
    return false;
  }
  readFailures = 0;
  if (!(status & 0x80)) return false; // no complete packet available
  uint8_t points = status & 0x0F;
  if (!points) { gt911Write(0x814E, 0); return false; }
  uint8_t data[7];
  if (!gt911Read(0x814F, data, sizeof(data))) {
    gt911Write(0x814E, 0);
    return false;
  }
  // The panel is calibrated with ROTATION_INVERTED: raw coordinates match the display.
  x = data[1] | (data[2] << 8);
  y = data[3] | (data[4] << 8);
  gt911Write(0x814E, 0); // acknowledge packet
  return x >= 0 && x < 800 && y >= 0 && y < 480;
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

void webServerTask(void *) {
  // WebServer is synchronous. Running it independently keeps the settings
  // page responsive while DNS or a HTTPS API request is still in progress.
  for (;;) {
    server.handleClient();
    vTaskDelay(1);
  }
}
void startWeb() {
  server.on("/", HTTP_GET, [](){ server.send(200, "text/html; charset=utf-8", settingsPage()); });
  server.on("/health", HTTP_GET, [](){ server.send(200, "text/plain", "P2000-display webserver OK\n"); });
  server.on("/save", HTTP_POST, []() {
    cfg.ssid=server.arg("ssid"); cfg.apiUrl=server.arg("apiUrl");
    cfg.regions[0]=server.arg("region1"); cfg.regions[1]=server.arg("region2"); cfg.regions[2]=server.arg("region3");
    cfg.capcodes=server.arg("capcodes"); cfg.ticker = server.arg("display") == "ticker";
    cfg.sdLogging = server.arg("sdlog") == "on";
    cfg.services[FILTER_FIRE] = server.hasArg("fire");
    cfg.services[FILTER_POLICE] = server.hasArg("police");
    cfg.services[FILTER_AMBULANCE] = server.hasArg("ambulance");
    cfg.services[FILTER_HELICOPTER] = server.hasArg("helicopter");
    cfg.services[FILTER_OTHER] = server.hasArg("other");
    String pass=server.arg("password"); if(pass.length()) cfg.password=pass;
    long requestedInterval = server.arg("interval").toInt();
    cfg.intervalSec = requestedInterval < 15 ? 15 : (uint32_t)requestedInterval;
    saveSettings(); server.send(200,"text/html","Opgeslagen. Herstarten..."); delay(800); ESP.restart();
  });
  server.onNotFound([](){ server.send(404, "text/plain", "Niet gevonden: " + server.uri()); });
  server.begin();
  xTaskCreatePinnedToCore(webServerTask, "p2000-web", 4096, nullptr, 1, &webServerTaskHandle, 0);
  Serial.println("Webserver gestart op poort 80");
}
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
  loadSettings();
  if (cfg.sdLogging) initSdCard();
  // The first boot is deliberately display-free. This keeps the RGB DMA
  // peripheral out of the way while a phone configures Wi-Fi over the AP.
  configurationMode = !cfg.ssid.length();
  // Initialise the TCP/IP stack before AsyncWebServer starts listening.
  if (cfg.ssid.length()) {
    connectWifi();
    statusLine = "Wifi verbinden...";
  } else {
    startConfigurationAp();
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
  if (!configurationMode) handleTouch();
  if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED && millis() > nextWifiRetry) {
    statusLine = "Wifi opnieuw verbinden...";
    if (screenMode == MESSAGES) drawScreen();
    connectWifi();
  }
  if (WiFi.status() == WL_CONNECTED && millis() > nextPoll) { nextPoll = millis() + cfg.intervalSec * 1000UL; pollApi(); }
  if (cfg.sdLogging && !sdReady && millis() > nextSdRetry) { nextSdRetry = millis() + 30000; initSdCard(); }
  delay(10);
}
