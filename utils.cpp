#include "utils.h"
#include "config.h"
#include <RTC.h>

// ── BUFFER DE LOG ────────────────────────────
#define MAX_LOG_LINES 20

struct LogEntry {
  char  text[80];
  uint8_t level;
};

static LogEntry logBuffer[MAX_LOG_LINES];
static int logHead  = 0;
static int logCount = 0;

// ── LOGGING ──────────────────────────────────
void addLog(const String& type, const String& msg, uint8_t level) {
  RTCTime now;
  RTC.getTime(now);
  char ts[12];
  snprintf(ts, sizeof(ts), "%02d:%02d:%02d",
           (int)now.getHour(), (int)now.getMinutes(), (int)now.getSeconds());

  char entry[80];
  snprintf(entry, sizeof(entry), "[%s][%s] %s",
           ts, type.c_str(), msg.c_str());

  logBuffer[logHead].level = level;
  strncpy(logBuffer[logHead].text, entry, 79);
  logBuffer[logHead].text[79] = '\0';
  logHead = (logHead + 1) % MAX_LOG_LINES;
  if (logCount < MAX_LOG_LINES) logCount++;

  Serial.println(entry);
}

void printLogJSON(WiFiClient& client, uint8_t minLevel) {
  client.print("[");
  int total = min(logCount, MAX_LOG_LINES);
  int start = (logCount < MAX_LOG_LINES) ? 0 : logHead;
  bool first = true;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % MAX_LOG_LINES;
    if (logBuffer[idx].level < minLevel) continue;
    
    if (!first) client.print(",");
    client.print("{\"l\":");
    client.print(logBuffer[idx].level);
    client.print(",\"t\":\"");
    
    const char* text = logBuffer[idx].text;
    for (int j = 0; text[j] != '\0'; j++) {
      char c = text[j];
      if (c == '\\') client.print("\\\\");
      else if (c == '"') client.print("\\\"");
      else if (c == '\n') client.print("\\n");
      else if (c == '\r') client.print("\\r");
      else if (c == '\t') client.print("\\t");
      else if ((unsigned char)c >= 32) client.print(c);
    }
    client.print("\"}");
    first = false;
  }
  client.print("]");
}

// ── UPTIME ───────────────────────────────────
String uptimeFormatted() {
  unsigned long s = millis() / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           s / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

// ── RAM LIBRE ────────────────────────────────
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ── CRC32 ────────────────────────────────────
uint32_t crc32str(const String& s) {
  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < (int)s.length(); i++) {
    crc ^= (uint8_t)s[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
  }
  return ~crc;
}

String crc32hex(const String& s) {
  char buf[9];
  sprintf(buf, "%08lX", (unsigned long)crc32str(s));
  return String(buf);
}

// ── TIMESTAMP → STRING ───────────────────────
String tsToString(uint32_t ts, int utcOffset) {
  if (ts == 0) return "--/--/---- --:--:--";
  ts += (uint32_t)(utcOffset * 3600L);
  unsigned long t = ts;
  int sec  = t % 60; t /= 60;
  int min  = t % 60; t /= 60;
  int hour = t % 24; t /= 24;
  // Algoritmo de días → fecha (válido para 1970-2100)
  unsigned long days = t;
  int y = 1970;
  while (true) {
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    unsigned long dy = leap ? 366 : 365;
    if (days < dy) break;
    days -= dy; y++;
  }
  static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap2 = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  int mo = 0;
  for (mo = 0; mo < 12; mo++) {
    int md = (mo == 1 && leap2) ? 29 : mdays[mo];
    if ((int)days < md) break;
    days -= md;
  }
  int day = (int)days + 1;
  char buf[22];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d",
           day, mo + 1, y, hour, min, sec);
  return String(buf);
}

// ── DAY OF WEEK → BITMASK ────────────────────
uint8_t rtcDayToBit(int dayOfWeek) {
  // RTC Arduino R4: 0=domingo, 1=lunes...
  // Nuestro bitmask: bit0=lunes … bit6=domingo
  if (dayOfWeek == 0) return DAY_SUN;
  return (1 << (dayOfWeek - 1));
}
