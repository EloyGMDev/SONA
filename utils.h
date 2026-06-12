#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// ── NIVELES DE LOG ───────────────────────────
#define LOG_DEBUG  0
#define LOG_INFO   1
#define LOG_WARN   2
#define LOG_ERROR  3

class WiFiClient;
void   addLog(const String& type, const String& msg, uint8_t level = LOG_INFO);
void   printLogJSON(WiFiClient& client, uint8_t minLevel = LOG_DEBUG);
String uptimeFormatted();
int    freeRam();

uint32_t crc32str(const String& s);
String   crc32hex(const String& s);

// Formatea un UNIX timestamp como "DD/MM/YYYY HH:MM:SS"
String tsToString(uint32_t ts, int utcOffset = 0);

// Convierte un dayOfWeek del RTC (0=Domingo, 1=Lunes...) al bitmask DAY_*
uint8_t rtcDayToBit(int dayOfWeek);

#endif
