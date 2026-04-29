#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <Arduino.h>

// Sincroniza el RTC con NTP. Devuelve true si tuvo éxito.
bool ntpSync();

// Llama a ntpSync() cada NTP_SYNC_INTERVAL ms. Colocar en loop().
void ntpPeriodicCheck();

#endif
