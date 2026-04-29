#include "hardware_io.h"
#include "config.h"
#include "utils.h"
#include "database.h"
#include <SPI.h>
#include <RTC.h>
#include <ArduinoBLE.h>

MFRC522 mfrc522(SS_PIN, RST_PIN);

void hardwareInit() {
  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  addLog("SISTEMA", "RFID listo");
  
  if (!BLE.begin()) {
    addLog("ERROR", "BLE no inicializado");
  } else {
    BLE.setLocalName(systemConfig.deviceName);
    BLE.setDeviceName(systemConfig.deviceName);
    BLE.setAdvertisedService(BLEService("FD00")); 
    BLE.advertise();
    addLog("SISTEMA", "BLE Sona Activo: " + String(systemConfig.deviceName));
  }
}

void handleRFID() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
  
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  if (uid == ultimoUID && (millis() - tiempoBloqueo < INTERVALO_FILTRO)) {
    mfrc522.PICC_HaltA();
    return;
  }

  addLog("RFID", "TAG: " + uid);
  ultimoTagUID = uid; 
  ultimoTagTs = millis()/1000;
  
  bool found = false;
  for (int i = 0; i < MAX_AULAS; i++) {
    if (uid == String(baseDatos[i].uid)) {
      String nom = String(baseDatos[i].nombre);
      
      // VALIDACIÓN DE HORARIO
      bool inSchedule = true;
      if (baseDatos[i].startHour != 0 || baseDatos[i].endHour != 0) {
        RTCTime now;
        RTC.getTime(now);
        int cur = now.getHour() * 60 + now.getMinutes();
        int start = baseDatos[i].startHour * 60 + baseDatos[i].startMin;
        int end = baseDatos[i].endHour * 60 + baseDatos[i].endMin;
        
        if (cur < start || cur > end) inSchedule = false;
      }

      if (inSchedule) {
        baseDatos[i].accesos++; 
        totalAccesos++;
        ultimoTagNombre = nom;
        eepromSaveAulas();
        pushHistorial(uid, nom);
        earcon(baseDatos[i].patronID);
        addLog("ACCESO", nom + " - Permitido");
      } else {
        addLog("DENIEGO", nom + " - Fuera de horario");
        earcon(0); // Sonido de error
      }
      
      found = true; 
      break;
    }
  }

  if (!found) {
    ultimoTagNombre = "DESCONOCIDO";
    earcon(0);
    addLog("WARN", "TAG no registrado: " + uid);
  }

  ultimoUID = uid; 
  tiempoBloqueo = millis();
  mfrc522.PICC_HaltA();
}


void earcon(int id) {
  switch (id) {
    case 1:  // Success Standard
      tone(BUZZER_PIN,800,100); delay(120); 
      tone(BUZZER_PIN,1000,150); delay(200); break;
    case 2:  // Double Beep Soft
      tone(BUZZER_PIN,1200,80); delay(100); 
      tone(BUZZER_PIN,1200,80); delay(100); break;
    case 3:  // Triple Fast
      for(int i=0;i<3;i++){tone(BUZZER_PIN,1500,60);delay(80);} break;
    case 4:  // Ascending Melody
      tone(BUZZER_PIN,523,100);delay(110);
      tone(BUZZER_PIN,659,100);delay(110);
      tone(BUZZER_PIN,784,100);delay(110);
      tone(BUZZER_PIN,1046,200);delay(250); break;
    case 5:  // Relaxing Down
      tone(BUZZER_PIN,880,150);delay(160);
      tone(BUZZER_PIN,698,150);delay(160);
      tone(BUZZER_PIN,523,200);delay(250); break;
    case 8:  // Fanfare
      tone(BUZZER_PIN,783,100);delay(110);
      tone(BUZZER_PIN,783,100);delay(110);
      tone(BUZZER_PIN,783,100);delay(110);
      tone(BUZZER_PIN,1046,300);delay(350); break;
    case 10: // Special Access
      tone(BUZZER_PIN,600,100);delay(120);
      tone(BUZZER_PIN,800,100);delay(120);
      tone(BUZZER_PIN,1200,200);delay(250); break;
    default: // Low error
      tone(BUZZER_PIN,300,500); delay(600); break;
  }
}

void earconBoot() {
  tone(BUZZER_PIN,523,100);delay(130);tone(BUZZER_PIN,784,100);delay(130);tone(BUZZER_PIN,1046,200);delay(250);
}
