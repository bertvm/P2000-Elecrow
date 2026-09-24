#pragma once

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

bool sameAlarm(const Alarm &a, const Alarm &b) {
  if (a.id.length() && b.id.length()) return a.id == b.id;
  return a.text.length() && a.time == b.time && a.text == b.text;
}

bool haveAlarm(const Alarm &a) {
  for (uint8_t i = 0; i < alarmCount; ++i) if (sameAlarm(alarms[i], a)) return true;
  return false;
}

void prependAlarm(const Alarm &incoming) {
  if (haveAlarm(incoming)) return;
  if (alarmCount < MAX_ALARMS) ++alarmCount;
  for (uint8_t i = alarmCount - 1; i > 0; --i) alarms[i] = alarms[i - 1];
  alarms[0] = incoming;
  appendAlarmLog(incoming);
}
