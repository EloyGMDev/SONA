/*
 * SONA SYSTEM v3.5 — Arduino R4 WiFi
 * Centralized Navigation & Management + mDNS + BLE Orientation.
 */

#include "config.h"
#include "utils.h"
#include "database.h"
#include "hardware_io.h"
#include "web_server.h"
#include "mdns_responder.h"
#include "ntp_sync.h"
#include <RTC.h>
#include <ArduinoBLE.h>

// ════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════
void setup() {
  delay(1000); 
  pinMode(BUZZER_PIN, OUTPUT);
  earconBoot(); // <-- Lo movemos aquí para saber si la placa arranca
  
  delay(1500); // Dar tiempo al coprocesador ESP32 (WiFi) a iniciar cuando se arranca con batería
  Serial.begin(115200);
  delay(500);
  
  RTC.begin();

  hardwareInit();

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
      strcpy(baseDatos[i].uid, "00000000");
      strcpy(baseDatos[i].nombre, "Sin Asignar");
      baseDatos[i].patronID = 0;
      baseDatos[i].accesos  = 0;
    }
    eepromSaveAulas();
  }
  
  eepromLoadHist();
  eepromLoadPwd();
  {
    // Validar que el hash sea exactamente 8 caracteres hex válidos
    bool hashOk = (strlen(cfgPwd.adminHash) == 8);
    for (int i = 0; hashOk && i < 8; i++) {
      char c = cfgPwd.adminHash[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) hashOk = false;
    }
    if (!hashOk) {
      if (Serial) Serial.println("EEPROM: Hash inválido, restaurando contraseña 'admin'");
      crc32hex("admin").toCharArray(cfgPwd.adminHash, 9);
      eepromSavePwd();
    }
    if (Serial) { Serial.print("Hash de seguridad actual: "); Serial.println(cfgPwd.adminHash); }
  }
  
  eepromLoadConfig();
  // Si el hostname está vacío o es inválido, forzar "sona"
  if (strlen(systemConfig.hostname) < 2 || !eepromValidate()) {
    strncpy(systemConfig.hostname, "sona", 24);
    if (strlen(systemConfig.deviceName) < 2) strncpy(systemConfig.deviceName, "SONA - RFID", 24);
    eepromSaveConfig();
  }

  webServerInit(); // ← conecta WiFi y levanta el servidor HTTP

  // Iniciar mDNS Custom solo si WiFi está conectado
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
    mdnsResponderInit();
    addLog("SISTEMA", "mDNS Configurado: http://" + String(systemConfig.hostname) + ".local");
    // Mostrar IP siempre por Serial por si mDNS falla
    if (Serial) {
      Serial.println("\n===================================");
      Serial.print("  Panel de control: http://");
      Serial.println(WiFi.localIP());
      Serial.print("  Alternativa: http://");
      Serial.print(systemConfig.hostname);
      Serial.println(".local");
      Serial.println("===================================\n");
    }
  } else {
    addLog("ERROR", "WiFi no conectado - mDNS desactivado");
  }

  if (WiFi.status() == WL_CONNECTED) {
    ntpSync(); // Primera sincronización
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
  meshDiscoveryRun();
  ntpPeriodicCheck();
  BLE.poll();
  mdnsResponderRun();
}