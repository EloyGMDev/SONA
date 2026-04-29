#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>

// Códigos de resultado del chequeo de acceso
#define ACCESS_OK           0
#define ACCESS_DENY_HOURS   1
#define ACCESS_DENY_DAYS    2
#define ACCESS_DENY_EXPIRED 3
#define ACCESS_DENY_MAX     4
#define ACCESS_DENY_DISABLED 5
#define ACCESS_DENY_LOCKDOWN 6
#define ACCESS_DENY_BLACKLIST 7

// Comprueba si el slot idx tiene acceso permitido ahora mismo.
// Devuelve uno de los códigos ACCESS_* definidos arriba.
uint8_t checkAccess(int idx);

// Chequeo de medianoche: reset de accessesToday
void schedulerDailyReset();

// Gestión de lista negra de UIDs
bool isBlacklisted(const String& uid);
void addBlacklist(const String& uid);
void removeBlacklist(const String& uid);

#endif
