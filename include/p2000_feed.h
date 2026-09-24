#pragma once

// Feed sources for bertvm/P2000-server (local HTTP API and MQTT) plus the
// public Alarmeringdroid cloud endpoint.
enum FeedMode : uint8_t { FEED_CLOUD = 0, FEED_LOCAL_API = 1, FEED_MQTT = 2 };
constexpr uint16_t DEFAULT_LOCAL_API_PORT = 8080;
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
constexpr const char *DEFAULT_MQTT_TOPIC = "p2000/alerts";

inline const char *feedModeName(uint8_t mode) {
  if (mode == FEED_LOCAL_API) return "Lokale API";
  if (mode == FEED_MQTT) return "MQTT";
  return "Alarmeringdroid";
}

inline uint8_t parseFeedMode(const String &value) {
  if (value == "local" || value == "1") return FEED_LOCAL_API;
  if (value == "mqtt" || value == "2") return FEED_MQTT;
  return FEED_CLOUD;
}

inline uint16_t parsePort(const String &value, uint16_t fallback) {
  long port = value.toInt();
  if (port < 1 || port > 65535) return fallback;
  return (uint16_t)port;
}

inline String sanitizeHost(String host) {
  host.trim();
  if (host.startsWith("http://")) host.remove(0, 7);
  else if (host.startsWith("https://")) host.remove(0, 8);
  int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  host.trim();
  return host;
}

inline String localApiUrl(const String &host, uint16_t port) {
  String clean = sanitizeHost(host);
  if (!clean.length()) return "";
  return "http://" + clean + ":" + String(port ? port : DEFAULT_LOCAL_API_PORT) + "/api2/find/";
}

inline String activeApiUrl(uint8_t mode, const String &cloudUrl, const String &host, uint16_t port) {
  if (mode == FEED_LOCAL_API) return localApiUrl(host, port);
  return cloudUrl;
}
