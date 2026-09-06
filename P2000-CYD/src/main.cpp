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
#include <StreamUtils.h>
#include <Arduino_GFX_Library.h>
#include "board.h"
#include "bounded_json.h"


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

Alarm alarms[MAX_ALARMS];
Alarm archiveAlarms[MAX_ALARMS];
uint8_t alarmCount = 0;
uint8_t firstVisibleAlarm = 0;
uint8_t archiveAlarmCount = 0;
uint8_t archiveFirstVisible = 0;
uint32_t archiveOffset = 0;
bool archiveMode = false;
uint8_t infoAlarmIndex = 0;
String newestInfoId;
unsigned long nextPoll = 0;
unsigned long nextWifiRetry = 0;
unsigned long nextSdRetry = 0;
String statusLine = "Configuratie laden...";
bool apiConnected = false;
bool apiChecked = false;
bool sdReady = false;
enum ScreenMode { MESSAGES, CONFIG, WIFI_SCAN, WIFI_INPUT, SD_FORMAT_CONFIRM };
ScreenMode screenMode = MESSAGES;
constexpr uint8_t MAX_WIFI_NETWORKS = 8;
String scannedWifiSsids[MAX_WIFI_NETWORKS];
int32_t scannedWifiRssi[MAX_WIFI_NETWORKS];
uint8_t scannedWifiCount = 0;
bool wifiScanning = false;
int wifiScanResult = 0;
bool keyboardSymbols = false, keyboardUppercase = false;
Preferences prefs;
WebServer server(80);

void connectWifi();
String wifiStatusText();
String selectedRegionsLabel(uint8_t maxChars);
bool serviceMatches(const Alarm &alarm);
int8_t renderedMessageLayout = -1; // -1=invalid, 0=list, 1=info
String lastHeaderSignature;
constexpr uint8_t VISIBLE_ALARM_CARDS = 2;
String lastListCardSignature[VISIBLE_ALARM_CARDS];
String lastInfoSignature;
String lastFooterSignature;

void invalidateMessageUi() {
  renderedMessageLayout = -1;
  lastHeaderSignature = "";
  lastInfoSignature = "";
  lastFooterSignature = "";
  for (String &signature : lastListCardSignature) signature = "";
}

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
    {"", "Geen"}, {"1", "Amsterdam-Amstelland"}, {"2", "Groningen"},
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
  return "Geen";
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
  String formSsid = cfg.ssid;
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
  cfg.intervalSec = constrain(prefs.isKey("int") ? prefs.getUInt("int") : 60U, 15U, 3600U);
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

bool parseCsvAlarm(const String &line, Alarm &alarm) {
  String values[6], value;
  uint8_t column = 0;
  bool quoted = false;
  for (size_t i = 0; i <= line.length(); ++i) {
    char c = i < line.length() ? line[i] : ',';
    if (c == '"') {
      if (quoted && i + 1 < line.length() && line[i + 1] == '"') { value += '"'; ++i; }
      else quoted = !quoted;
    } else if (c == ',' && !quoted) {
      if (column < 6) values[column++] = value;
      value = "";
    } else if (c != '\r' && c != '\n') value += c;
  }
  if (column < 6 || values[0] == "id") return false;
  alarm.id = values[0]; alarm.time = values[1]; alarm.region = values[2];
  alarm.place = values[3]; alarm.caps = values[4]; alarm.text = values[5];
  alarm.service = "";
  return alarm.id.length() || alarm.text.length();
}
bool previousCsvLine(File &file, uint32_t &cursor, String &line) {
  while (cursor > 0) {
    file.seek(cursor - 1); char c = file.read();
    if (c != '\n' && c != '\r') break;
    --cursor;
  }
  if (!cursor) return false;
  uint32_t end = cursor, start = end;
  while (start > 0) {
    file.seek(start - 1);
    if (file.read() == '\n') break;
    --start;
  }
  line = ""; file.seek(start);
  while (file.position() < end) line += (char)file.read();
  cursor = start;
  return true;
}
bool isCurrentAlarm(const String &id) {
  if (!id.length()) return false;
  for (uint8_t i = 0; i < alarmCount; ++i) if (alarms[i].id == id) return true;
  return false;
}
bool loadArchivePage(uint32_t offset) {
  archiveAlarmCount = 0; archiveFirstVisible = 0;
  if (!sdReady && !initSdCard()) { statusLine = "SD-archief niet beschikbaar"; return false; }
  File file = SD.open("/p2000.csv", FILE_READ);
  if (!file) { statusLine = "Nog geen SD-archief"; return false; }
  uint32_t cursor = file.size(), matched = 0;
  String line; Alarm candidate;
  while (previousCsvLine(file, cursor, line)) {
    if (!parseCsvAlarm(line, candidate) || isCurrentAlarm(candidate.id) || !serviceMatches(candidate)) continue;
    if (matched++ < offset) continue;
    archiveAlarms[archiveAlarmCount++] = candidate;
    if (archiveAlarmCount >= MAX_ALARMS) break;
  }
  file.close(); archiveOffset = offset;
  statusLine = archiveAlarmCount ? "SD-archief" : "Einde van SD-archief";
  return archiveAlarmCount > 0;
}

String field(JsonObject obj, const char *a, const char *b = "") {
  if (obj[a].is<const char*>()) return String(obj[a].as<const char*>());
  if (obj[a].is<long>()) return String(obj[a].as<long>());
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
bool readAlarms(Stream &body) {
  BoundedJsonAllocator allocator(64 * 1024);
  JsonDocument doc(&allocator);
  DeserializationError err = deserializeJson(doc, body);
  if (err) { statusLine = String("API JSON: ") + err.c_str(); return false; }
  JsonArray list;
  if (doc.is<JsonArray>()) list = doc.as<JsonArray>();
  else if (doc["meldingen"].is<JsonArray>()) list = doc["meldingen"].as<JsonArray>();
  else if (doc["messages"].is<JsonArray>()) list = doc["messages"].as<JsonArray>();
  else if (doc["results"].is<JsonArray>()) list = doc["results"].as<JsonArray>();
  else if (doc["data"].is<JsonArray>()) list = doc["data"].as<JsonArray>();
  else { statusLine = "JSON bevat geen berichtenlijst"; return false; }
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
  }
  // API order is newest first; write oldest first so reverse SD traversal
  // presents the archive newest first as well.
  for (int i = alarmCount - 1; i >= 0; --i) {
    bool alreadyListed = false;
    for (uint8_t j = 0; j < previousCount; ++j)
      if (alarms[i].id.length() && alarms[i].id == previousIds[j]) { alreadyListed = true; break; }
    if (!alreadyListed) appendAlarmLog(alarms[i]);
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
  if (infoAlarmIndex >= alarmCount) infoAlarmIndex = 0;
  return true;
}


#include "compact_ui.h"
void startConfigurationAp() {
  IPAddress apIp(192, 168, 77, 1), gateway(192, 168, 77, 1), mask(255, 255, 255, 0);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, gateway, mask);
  if (WiFi.softAP("P2000-display")) {
    statusLine = "Config AP: " + WiFi.softAPIP().toString();
    Serial.println("Wifi opnieuw instellen: verbind met P2000-display, http://" + WiFi.softAPIP().toString());
  } else statusLine = "Config-AP starten mislukt";
}
volatile uint16_t lastWifiReason=0;
String wifiStatusText() {
  switch(WiFi.status()) {
    case WL_CONNECTED:return "Verbonden: "+WiFi.SSID();
    case WL_NO_SSID_AVAIL:return "Netwerk niet gevonden (2.4 GHz)";
    case WL_CONNECT_FAILED:return "WiFi aanmelden mislukt";
    case WL_CONNECTION_LOST:return "WiFi-verbinding verloren";
    default:return cfg.ssid.length()?"WiFi verbinden; reden "+String(lastWifiReason):"Configuratie-AP actief";
  }
}
void pollApi() {
  if (WiFi.status()!=WL_CONNECTED || !cfg.apiUrl.length()) return;
  const String &url=cfg.apiUrl;
  bool https=url.startsWith("https://");
  statusLine="API ophalen...";drawScreen();
  Serial.printf("API start: wifi=%d IP=%s RSSI=%d heap=%u\n",WiFi.status(),WiFi.localIP().toString().c_str(),WiFi.RSSI(),ESP.getFreeHeap());
  HTTPClient http;WiFiClientSecure secure;WiFiClient plain;
  // Socket/TLS setters use seconds; HTTPClient and Stream use milliseconds.
  secure.setInsecure();secure.setTimeout(8);secure.setHandshakeTimeout(12);
  plain.setTimeout(8);
  http.setConnectTimeout(8000);http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("P2000-CYD/1.0.1");
  const char *headers[]={"Transfer-Encoding"};http.collectHeaders(headers,1);
  bool started=https?http.begin(secure,url):http.begin(plain,url);
  // HTTP/1.1 avoids closing TLS immediately after the response. Decode chunks
  // explicitly, because getStream() bypasses HTTPClient's transfer decoder.
  unsigned long requestStarted=millis();
  int code=started?http.GET():HTTPC_ERROR_CONNECTION_REFUSED;
  apiChecked=true;apiConnected=false;
  Serial.printf("API HTTP=%d na %lu ms; wifi=%d heap=%u\n",code,millis()-requestStarted,WiFi.status(),ESP.getFreeHeap());
  if(code==HTTP_CODE_OK) {
    Stream &raw=http.getStream();raw.setTimeout(8000);
    ChunkDecodingStream decoded(raw);decoded.setTimeout(8000);
    Stream &body=http.header("Transfer-Encoding").equalsIgnoreCase("chunked")?static_cast<Stream&>(decoded):raw;
    String selectedId=detailOpen && !archiveMode && detailIndex<alarmCount?alarms[detailIndex].id:"";
    apiConnected=readAlarms(body);
    if(apiConnected && detailOpen && !archiveMode) {
      bool found=false;
      for(uint8_t i=0;i<alarmCount;++i) if(alarms[i].id==selectedId){detailIndex=i;found=true;break;}
      if(!found){detailOpen=false;detailLine=0;}
    }
    Serial.printf("API JSON: %s; %s\n",apiConnected?"OK":"FOUT",statusLine.c_str());
  } else {
    statusLine=code>0?"API HTTP "+String(code):"API "+String(code)+": "+HTTPClient::errorToString(code);
    if(https) {char error[160]={};int tls=secure.lastError(error,sizeof(error));Serial.printf("TLS=%d %s\n",tls,error);}
    Serial.println(statusLine);
  }
  if(!apiConnected) nextPoll=millis()+15000;
  http.end();if(screenMode==MESSAGES)drawScreen();
}
void connectWifi() {
  if(!cfg.ssid.length())return;
  WiFi.mode(WIFI_STA);
  // Use the router's DHCP address, gateway and DNS. Public DNS can be blocked
  // on a LAN and should not replace the network's supplied configuration.
  WiFi.config(INADDR_NONE,INADDR_NONE,INADDR_NONE);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  wl_status_t result=WiFi.begin(cfg.ssid.c_str(),cfg.password.c_str());
  Serial.printf("WiFi verbinden: begin=%d\n",result);
  nextWifiRetry=millis()+30000;
}
void updateNetworkStatus() {
  static int previousStatus=-1;
  int current=WiFi.status();
  if(current!=previousStatus) {
    previousStatus=current;
    if(current==WL_CONNECTED) {
      Serial.printf("WiFi verbonden IP=%s gateway=%s DNS=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str(),WiFi.dnsIP().toString().c_str(),WiFi.RSSI());
      statusLine="WiFi verbonden";nextPoll=0;
    } else {apiConnected=false;if(!wifiScanning)statusLine=wifiStatusText();}
  }
  static String lastStatus;
  if(screenMode==CONFIG && configPage==WIFI_PAGE) {
    String state=wifiStatusText()+WiFi.localIP().toString()+String(lastWifiReason);
    if(state!=lastStatus){lastStatus=state;drawConfigScreen();}
  }
}

String networkDiagnostics() {
  JsonDocument doc;
  doc["firmware"]="1.0.1";doc["touch"]=CYD_TOUCH_NAME;
  doc["wifi_status"]=(int)WiFi.status();doc["disconnect_reason"]=(int)lastWifiReason;
  doc["ip"]=WiFi.localIP().toString();doc["gateway"]=WiFi.gatewayIP().toString();doc["dns"]=WiFi.dnsIP().toString();
  doc["rssi"]=WiFi.RSSI();doc["api_checked"]=apiChecked;doc["api_ok"]=apiConnected;
  doc["status"]=statusLine;doc["scan_running"]=wifiScanning;doc["scan_result"]=wifiScanResult;
  doc["visible_networks"]=scannedWifiCount;doc["alarms"]=alarmCount;doc["heap"]=ESP.getFreeHeap();
  String result;serializeJson(doc,result);return result;
}
void handleSerialDiagnostics() {
  static String command;
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='\n' || c=='\r') {
      command.trim();
      if(command=="status")Serial.println(networkDiagnostics());
      else if(command=="scan") {beginConfig();scanWifiNetworks();}
      else if(command=="api") {screenMode=MESSAGES;invalidateMessageUi();nextPoll=0;}
      command="";
    } else if(command.length()<32)command+=c;
    else command="";
  }
}
void startWeb() {
  server.on("/diagnostics",HTTP_GET,[](){server.send(200,"application/json",networkDiagnostics());});
  server.on("/", HTTP_GET, [](){ server.send(200, "text/html; charset=utf-8", settingsPage()); });
  server.on("/health", HTTP_GET, [](){ server.send(200, "text/plain", "P2000 CYD ESP32-2432S032 webserver OK\n"); });
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
    cfg.intervalSec = constrain(requestedInterval, 15L, 3600L);
    saveSettings(); server.send(200,"text/html","Opgeslagen. Herstarten..."); delay(800); ESP.restart();
  });
  server.onNotFound([](){ server.send(404, "text/plain", "Niet gevonden: " + server.uri()); });
  server.begin();

  Serial.println("Webserver gestart op poort 80");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("P2000 CYD 1.0.1 | ESP32-2432S032 | %s | ST7789 320x240\n", CYD_TOUCH_NAME);
  loadSettings();
  WiFi.persistent(false);
  WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info) {
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      lastWifiReason=info.wifi_sta_disconnected.reason;
      Serial.printf("WiFi losgekoppeld: reden=%u\n",lastWifiReason);
    } else if(event==ARDUINO_EVENT_WIFI_SCAN_DONE) {
      Serial.printf("WiFi scan: status=%u gevonden=%u\n",info.wifi_scan_done.status,info.wifi_scan_done.number);
    }
  });
  initBoard();
  if (cfg.sdLogging) initSdCard();
  if (cfg.ssid.length()) { connectWifi(); statusLine = "Wifi verbinden..."; }
  else startConfigurationAp();
  startWeb();
  drawScreen();
}
void loop() {
  server.handleClient();
  handleSerialDiagnostics();
  handleTouch();
  updateWifiScan();
  updateNetworkStatus();
  if (cfg.ssid.length() && !wifiScanning && WiFi.status() != WL_CONNECTED &&
      (int32_t)(millis() - nextWifiRetry) >= 0) {
    statusLine = "Wifi opnieuw verbinden...";
    connectWifi();
  }
  // Keep configuration and text entry responsive by polling only on the feed.
  if (screenMode == MESSAGES && WiFi.status() == WL_CONNECTED &&
      (int32_t)(millis() - nextPoll) >= 0) {
    nextPoll = millis() + cfg.intervalSec * 1000UL;
    pollApi();
  }
  if (cfg.sdLogging && !sdReady && (int32_t)(millis() - nextSdRetry) >= 0) {
    nextSdRetry = millis() + 30000; initSdCard();
  }
  static unsigned long nextStatus = 0;
  if (screenMode == MESSAGES && (int32_t)(millis() - nextStatus) >= 0) {
    nextStatus = millis() + 1000; drawScreen();
  }
  delay(10);
}
