#include "database.h"
#include "config.h"
#include "utils.h"
#include <EEPROM.h>
#include <RTC.h>
#include <stddef.h>  // offsetof

// ══════════════════════════════════════════════
//  EEPROM CRUD BÁSICO
// ══════════════════════════════════════════════
void eepromSaveAulas()  { EEPROM.put(0,           baseDatos);   }
void eepromLoadAulas()  { EEPROM.get(0,           baseDatos);   }
void eepromSaveHist()   { EEPROM.put(HIST_OFFSET, historial);   }
void eepromLoadHist()   { EEPROM.get(HIST_OFFSET, historial);   }
void eepromSavePwd()    { EEPROM.put(PWD_OFFSET,  cfgPwd);      }
void eepromLoadPwd()    { EEPROM.get(PWD_OFFSET,  cfgPwd);      }

void eepromSaveConfig() {
  // Usar offsetof para iterar SOLO sobre los bytes ANTES del campo crc.
  // sizeof(SystemConfig)-4 es INCORRECTO porque en ARM el struct tiene
  // padding al final (double fuerza alineación a 8 bytes), y eso hace que
  // el campo crc caiga DENTRO del rango iterado → el CRC siempre falla.
  uint32_t c = 0;
  const uint8_t* p = (const uint8_t*)&systemConfig;
  for (size_t i = 0; i < offsetof(SystemConfig, crc); i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++)
      c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : (c >> 1);
  }
  systemConfig.crc = ~c;
  EEPROM.put(CFG_OFFSET, systemConfig);
}

void eepromLoadConfig() {
  EEPROM.get(CFG_OFFSET, systemConfig);
}

// ══════════════════════════════════════════════
//  INTEGRIDAD
// ══════════════════════════════════════════════
bool eepromValidate() {
  SystemConfig tmp;
  EEPROM.get(CFG_OFFSET, tmp);
  uint32_t c = 0;
  const uint8_t* p = (const uint8_t*)&tmp;
  // Mismo fix: offsetof en lugar de sizeof-4
  for (size_t i = 0; i < offsetof(SystemConfig, crc); i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++)
      c = (c & 1) ? (c >> 1) ^ 0xEDB88320 : (c >> 1);
  }
  return (~c == tmp.crc);
}

// ══════════════════════════════════════════════
//  RESET NUCLEAR
// ══════════════════════════════════════════════
void eepromResetAll() {
  // Limpiar aulas
  for (int i = 0; i < MAX_AULAS; i++) {
    strncpy(baseDatos[i].uid,    "00000000", 16);
    strncpy(baseDatos[i].nombre, "Sin Asignar", 32);
    strncpy(baseDatos[i].notes,  "", 48);
    baseDatos[i].patronID   = 1;
    baseDatos[i].accesos    = 0;
    baseDatos[i].maxAccesos = 35;
    baseDatos[i].days       = DAY_ALL;
    baseDatos[i].startHour  = 0;
    baseDatos[i].startMin   = 0;
    baseDatos[i].endHour    = 0;
    baseDatos[i].endMin     = 0;
    baseDatos[i].expiryTs   = 0;
    baseDatos[i].enabled    = true;
  }
  eepromSaveAulas();

  // Limpiar historial
  clearHistorial();

  // Resetear contraseña a "admin"
  crc32hex("admin").toCharArray(cfgPwd.adminHash, 9);
  cfgPwd.signature = 0x5057;
  eepromSavePwd();

  // Config por defecto
  strncpy(systemConfig.ntpServer,  "pool.ntp.org", 40);
  systemConfig.utcOffset    = 2;
  systemConfig.quietStart   = 22;
  systemConfig.quietEnd     = 8;
  systemConfig.quietEnabled = false;
  strncpy(systemConfig.hostname, "sona", 24);
  strncpy(systemConfig.wifiSSID, "", 32);
  strncpy(systemConfig.wifiPassword, "", 64);
  strncpy(systemConfig.classRoom, "Clase 1", 32);
  strncpy(systemConfig.classNum, "", 8);
  systemConfig.latitude     = 0.0;
  systemConfig.longitude    = 0.0;
  eepromSaveConfig();

  addLog("SISTEMA", "EEPROM reset a valores por defecto", LOG_WARN);
}

// ══════════════════════════════════════════════
//  HISTORIAL
// ══════════════════════════════════════════════
void pushHistorial(const String& uid, const String& nombre, uint8_t result) {
  // Desplazar entradas hacia adelante (el [0] siempre es el más reciente)
  for (int i = MAX_HIST - 1; i > 0; i--) {
    historial[i] = historial[i - 1];
  }
  uid.toCharArray(historial[0].uid, 16);
  nombre.toCharArray(historial[0].nombre, 32);
  // Usar RTC para timestamp real
  RTCTime now;
  RTC.getTime(now);
  historial[0].timestamp = (uint32_t)(now.getUnixTime());
  historial[0].result    = result;
  eepromSaveHist();
}

void clearHistorial() {
  memset(historial, 0, sizeof(historial));
  eepromSaveHist();
}

// ══════════════════════════════════════════════
//  ESTADÍSTICAS
// ══════════════════════════════════════════════
void resetAccesos() {
  for (int i = 0; i < MAX_AULAS; i++) {
    baseDatos[i].accesos = 0;
  }
  totalAccesos  = 0;
  accessesToday = 0;
  eepromSaveAulas();
  addLog("ADMIN", "Contadores reseteados", LOG_WARN);
}

void resetAccesosSlot(int i) {
  if (i < 0 || i >= MAX_AULAS) return;
  baseDatos[i].accesos = 0;
  eepromSaveAulas();
  addLog("ADMIN", "Contador slot " + String(i) + " reseteado", LOG_INFO);
}
