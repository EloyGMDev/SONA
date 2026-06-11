#include "ntp_sync.h"
#include "config.h"
#include "utils.h"
#include <RTC.h>
#include "WiFiS3.h"

// Sincroniza el RTC usando WiFi.getTime() (SNTP del firmware del módulo)
bool ntpSync() {
  if (WiFi.status() != WL_CONNECTED) {
    addLog("NTP", "Sin WiFi - no se puede sincronizar", LOG_WARN);
    return false;
  }

  addLog("NTP", "Sincronizando hora...", LOG_INFO);

  unsigned long epoch = 0;
  // Reintentar hasta 5 veces con 1s de espera
  for (int i = 0; i < 5; i++) {
    epoch = WiFi.getTime();
    if (epoch > 1000000000UL) break;
    delay(1000);
  }

  if (epoch == 0) {
    addLog("NTP", "Fallo al obtener hora", LOG_ERROR);
    return false;
  }

  // Aplicar offset UTC
  epoch += (unsigned long)(systemConfig.utcOffset * 3600L);

  RTCTime newTime(epoch);
  RTC.setTime(newTime);
  ntpLastSync = millis();

  char buf[40];
  RTCTime now;
  RTC.getTime(now);
  snprintf(buf, sizeof(buf), "Hora: %02d:%02d:%02d UTC%+d",
           (int)now.getHour(), (int)now.getMinutes(), (int)now.getSeconds(),
           systemConfig.utcOffset);
  addLog("NTP", buf, LOG_INFO);
  return true;
}

// Llamar desde loop(): re-sincroniza cada NTP_SYNC_INTERVAL ms
void ntpPeriodicCheck() {
  if (ntpLastSync == 0) {
    // Si no se ha sincronizado todavia, intentarlo cada 60 segundos (60000 ms)
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry >= 60000) {
      lastRetry = millis();
      ntpSync();
    }
    return;
  }
  if (millis() - ntpLastSync >= NTP_SYNC_INTERVAL) {
    ntpSync();
  }
}
