#pragma once
#include <PubSubClient.h>

// Included after Alarm helpers (field, matchesFilters, serviceMatches).
void drawScreen();
WiFiClient mqttWifi;
PubSubClient mqttClient(mqttWifi);
unsigned long nextMqttRetry = 0;
char mqttHostBuf[64];

bool fillAlarmFromItem(JsonObject item, Alarm &a) {
  a.id = field(item, "id");
  a.time = field(item, "tijd", "time");
  if (!a.time.length()) a.time = field(item, "timestamp");
  if (!a.time.length()) a.time = field(item, "datum") + " " + field(item, "tijd");
  a.caps = field(item, "capstring");
  if (!a.caps.length()) a.caps = field(item, "capcode", "capcodes");
  a.service = field(item, "dienst", "service");
  a.region = field(item, "regio", "region");
  a.place = field(item, "plaats", "place");
  a.text = field(item, "tekstmelding", "message");
  if (!a.text.length()) a.text = field(item, "text", "melding");
  if (!a.text.length()) a.text = field(item, "body", "description");
  return a.id.length() || a.text.length();
}

void ingestMqttAlert(JsonObject item) {
  if (!matchesFilters(item)) return;
  Alarm incoming;
  if (!fillAlarmFromItem(item, incoming) || !serviceMatches(incoming)) return;
  for (uint8_t i = 0; i < alarmCount; ++i)
    if (incoming.id.length() && alarms[i].id == incoming.id) return;
  if (alarmCount < MAX_ALARMS) ++alarmCount;
  for (uint8_t i = alarmCount - 1; i > 0; --i) alarms[i] = alarms[i - 1];
  alarms[0] = incoming;
  firstVisibleAlarm = 0;
  infoAlarmIndex = 0;
  newestInfoId = incoming.id;
  appendAlarmLog(incoming);
  statusLine = "MQTT: nieuwe melding";
  if (screenMode == MESSAGES) drawScreen();
}

void onMqttMessage(char *, byte *payload, unsigned int length) {
  BoundedJsonAllocator allocator(4 * 1024);
  JsonDocument doc(&allocator);
  if (deserializeJson(doc, payload, length) || !doc.is<JsonObject>()) return;
  ingestMqttAlert(doc.as<JsonObject>());
}

void pumpMqtt() {
  if (cfg.feedMode != FEED_MQTT) {
    if (mqttClient.connected()) mqttClient.disconnect();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  String host = sanitizeHost(cfg.serverHost);
  if (!host.length()) {
    apiChecked = true;
    apiConnected = false;
    statusLine = "MQTT: stel het Pi-IP in";
    return;
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }
  if ((long)(millis() - nextMqttRetry) < 0) return;
  nextMqttRetry = millis() + 4000;
  host.toCharArray(mqttHostBuf, sizeof(mqttHostBuf));
  mqttClient.setServer(mqttHostBuf, cfg.mqttPort ? cfg.mqttPort : DEFAULT_MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setBufferSize(2048);
  String clientId = "p2000-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = cfg.mqttUser.length()
                ? mqttClient.connect(clientId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str())
                : mqttClient.connect(clientId.c_str());
  apiChecked = true;
  apiConnected = ok;
  if (ok) {
    String topic = cfg.mqttTopic.length() ? cfg.mqttTopic : DEFAULT_MQTT_TOPIC;
    mqttClient.subscribe(topic.c_str());
    statusLine = "MQTT verbonden";
    Serial.printf("MQTT subscribed %s @ %s\n", topic.c_str(), host.c_str());
  } else {
    statusLine = "MQTT fout rc=" + String(mqttClient.state());
    Serial.println(statusLine);
  }
  if (screenMode == MESSAGES) drawScreen();
}
