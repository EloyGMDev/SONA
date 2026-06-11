#include "config.h"

// ── RED ──────────────────────────────────────
IPAddress local_IP(192, 168, 67, 67);
IPAddress gateway(192, 168, 67, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);
const char* ssid     = "kaskiskus";
const char* password = "nomeacuerdo";

// ── BASES DE DATOS ───────────────────────────
Aula         baseDatos[MAX_AULAS];
HistEntry    historial[MAX_HIST];
ConfigPwd    cfgPwd;
SystemConfig systemConfig;

// ── ESTADÍSTICAS ─────────────────────────────
unsigned long totalAccesos   = 0;
unsigned long accessesToday  = 0;
unsigned long dailyResetTs   = 0;
bool          lockdownMode   = false;

// ── ÚLTIMO TAG ───────────────────────────────
String        ultimoTagUID    = "---";
String        ultimoTagNombre = "---";
unsigned long ultimoTagTs     = 0;

// ── FILTRO ANTI-BOUNCE ───────────────────────
String        ultimoUID       = "";
unsigned long tiempoBloqueo   = 0;
const unsigned long INTERVALO_FILTRO = 5000;

// ── WATCHDOG WIFI ────────────────────────────
unsigned long wifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;

// ── NTP ──────────────────────────────────────
unsigned long ntpLastSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 86400000UL; // 24h en ms

// ── MODO REGISTRO ────────────────────────────
bool registerMode = false;

// ── MODO PUNTO DE ACCESO (AP) ────────────────
bool isAPMode = false;

