/*
 * SONA SYSTEM v3.5 — Arduino R4 WiFi
 * Centralized Navigation & Management + mDNS + BLE Orientation.
 */

#include "config.h"
#include "utils.h"
#include "database.h"
#include "hardware_io.h"
#include "web_server.h"
#include "ntp_sync.h"
#include <RTC.h>
#include <ArduinoBLE.h>
#include <ArduinoMDNS.h>

WiFiUDP udp_mdns;
MDNS mdns(udp_mdns);

// ════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);
  delay(500);
  
  RTC.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  earconBoot();

  hardwareInit();

  // Reset por hardware (botón presionado al arrancar)
  pinMode(BTN_REGISTER, INPUT_PULLUP);
  delay(10);
  if (digitalRead(BTN_REGISTER) == LOW) {
    eepromResetAll();
    tone(BUZZER_PIN, 1000, 1500);
    delay(1600);
    tone(BUZZER_PIN, 1500, 200);
    delay(250);
  }

  eepromLoadAulas();

  bool valid = true;
  for (int i = 0; i < MAX_AULAS; i++) {
    if (baseDatos[i].patronID < 0 || baseDatos[i].patronID > 10) { 
      valid = false; 
      break; 
    }
  }
  
  if (!valid) {
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
  }
  
  eepromLoadHist();
  eepromLoadPwd();
  
  bool pwdValid = (cfgPwd.signature == 0x5057) && (strlen(cfgPwd.adminHash) == 8);
  if (pwdValid) {
    for (int i = 0; i < 8; i++) {
      char c = cfgPwd.adminHash[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
        pwdValid = false;
        break;
      }
    }
  }
  
  if (!pwdValid) {
    crc32hex("admin").toCharArray(cfgPwd.adminHash, 9);
    cfgPwd.signature = 0x5057;
    eepromSavePwd();
    addLog("SISTEMA", "Contrasena reestablecida a 'admin' por hash o firma invalida", LOG_WARN);
  }

  // Cargar y validar configuración de sistema
  if (!eepromValidate()) {
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
    systemConfig.latitude  = 0.0;
    systemConfig.longitude = 0.0;
    systemConfig.crc = 0;
    eepromSaveConfig();
    Serial.println("EEPROM: config invalida, defaults escritos");
    
    // Si la configuración del sistema era inválida (p. ej. primer arranque con nueva estructura),
    // también forzamos el restablecimiento de la contraseña a "admin"
    crc32hex("admin").toCharArray(cfgPwd.adminHash, 9);
    cfgPwd.signature = 0x5057;
    eepromSavePwd();
    addLog("SISTEMA", "Contrasena restablecida a 'admin' por configuracion invalida", LOG_WARN);
  } else {
    eepromLoadConfig();
    addLog("SISTEMA", "Configuracion cargada de EEPROM", LOG_INFO);
    String currentHost = String(systemConfig.hostname);
    currentHost.trim();
    currentHost.toLowerCase();
    if (currentHost == "rfid" || currentHost == "" || currentHost.indexOf("rfid") != -1) {
      strncpy(systemConfig.hostname, "sona", 24);
      eepromSaveConfig();
      addLog("SISTEMA", "Hostname migrado de 'rfid' a 'sona' en EEPROM", LOG_INFO);
    }
  }

  webServerInit();

  if (mdns.begin(WiFi.localIP(), systemConfig.hostname)) {
    addLog("SISTEMA", "mDNS activo: http://" + String(systemConfig.hostname) + ".local");
  }
}

// ════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════
void loop() { 
  handleRFID(); 
  handleWebAdmin(); 
  handleDiscovery();
  wifiWatchdog(); 
  BLE.poll();
  mdns.run();
  ntpPeriodicCheck();
}