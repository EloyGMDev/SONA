#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "WiFiS3.h"

// ── FIRMWARE ─────────────────────────────────
#include "version.h"

// ── PINES ────────────────────────────────────
#define RST_PIN         9
#define SS_PIN          10
#define BUZZER_PIN      5
#define BTN_REGISTER    7    // Botón físico registro rápido (INPUT_PULLUP)

// ── RED ──────────────────────────────────────
extern IPAddress local_IP;
extern IPAddress gateway;
extern IPAddress subnet;
extern IPAddress dns;
extern const char* ssid;
extern const char* password;

// ── LÍMITES ──────────────────────────────────
#define MAX_AULAS   15
#define MAX_HIST    10

// ── BITMASK DÍAS (Lunes=bit0 … Domingo=bit6) ─
#define DAY_MON  0x01
#define DAY_TUE  0x02
#define DAY_WED  0x04
#define DAY_THU  0x08
#define DAY_FRI  0x10
#define DAY_SAT  0x20
#define DAY_SUN  0x40
#define DAY_ALL  0x7F

// ── STRUCT AULA ──────────────────────────────
struct Aula {
  char  uid[16];        // UID RFID
  char  nombre[20];     // Nombre del aula / persona
  uint8_t patronID;     // ID de earcon (1-10)
  uint16_t accesos;     // Contador total de accesos
  uint16_t maxAccesos;  // 0 = sin límite
  uint8_t days;         // Bitmask de días permitidos (DAY_*)
  int8_t  startHour;
  int8_t  startMin;
  int8_t  endHour;
  int8_t  endMin;
  uint32_t expiryTs;    // UNIX timestamp de expiración (0 = sin caducidad)
  bool  enabled;        // Slot habilitado
};

// ── STRUCT HISTORIAL ─────────────────────────
struct HistEntry {
  char  uid[16];
  char  nombre[20];
  uint32_t timestamp;   // UNIX timestamp real (del RTC)
  uint8_t  result;      // 0=OK, 1=fuera horario, 2=desconocido, 3=lockdown, 4=max
};

// ── STRUCT CONTRASEÑA ────────────────────────
struct ConfigPwd {
  char adminHash[9];    // CRC32 hex de la contraseña
};

// ── STRUCT CONFIGURACIÓN DEL SISTEMA ─────────
struct SystemConfig {
  char  ntpServer[40];  // e.g. "pool.ntp.org"
  int   utcOffset;      // Offset en horas (e.g. 2 para CEST)
  int   quietStart;     // Hora inicio modo silencioso (e.g. 22)
  int   quietEnd;       // Hora fin modo silencioso (e.g. 8)
  bool  quietEnabled;   // Modo silencioso activo
  char  hostname[24];   // hostname mDNS (siempre "sona")
  char  deviceName[24]; // Label de aula (e.g. "Aula 202")
  uint32_t crc;         // Checksum de integridad
};

// ── OFFSETS EEPROM ───────────────────────────
//   0                      → baseDatos[MAX_AULAS]
//   HIST_OFFSET            → historial[MAX_HIST]
//   PWD_OFFSET             → cfgPwd
//   CFG_OFFSET             → systemConfig
#define HIST_OFFSET  (MAX_AULAS * sizeof(Aula))
#define PWD_OFFSET   (HIST_OFFSET + MAX_HIST * sizeof(HistEntry))
#define CFG_OFFSET   (PWD_OFFSET + sizeof(ConfigPwd))

// ── GLOBALES COMPARTIDAS ─────────────────────
extern Aula         baseDatos[MAX_AULAS];
extern HistEntry    historial[MAX_HIST];
extern ConfigPwd    cfgPwd;
extern SystemConfig systemConfig;

extern unsigned long totalAccesos;
extern unsigned long accessesToday;
extern unsigned long dailyResetTs;
extern bool          lockdownMode;

extern String        ultimoTagUID;
extern String        ultimoTagNombre;
extern unsigned long ultimoTagTs;

extern String        ultimoUID;
extern unsigned long tiempoBloqueo;
extern const unsigned long INTERVALO_FILTRO;

extern unsigned long wifiCheck;
extern const unsigned long WIFI_CHECK_INTERVAL;

extern unsigned long ntpLastSync;
extern const unsigned long NTP_SYNC_INTERVAL;

extern unsigned long meshLastCheck;
extern const unsigned long MESH_DISCOVERY_INTERVAL;

extern bool          registerMode;    // Modo registro rápido activo

#endif
