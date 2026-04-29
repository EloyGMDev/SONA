#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>

// ── EEPROM CRUD ──────────────────────────────
void eepromSaveAulas();
void eepromLoadAulas();
void eepromSaveHist();
void eepromLoadHist();
void eepromSavePwd();
void eepromLoadPwd();
void eepromSaveConfig();
void eepromLoadConfig();

// Borra toda la EEPROM y reinicia estructuras a valores por defecto
void eepromResetAll();

// ── HISTORIAL ────────────────────────────────
// Inserta una entrada al inicio del historial circular
void pushHistorial(const String& uid, const String& nombre, uint8_t result = 0);

// Borra todo el historial
void clearHistorial();

// ── ESTADÍSTICAS ─────────────────────────────
void resetAccesos();           // Reset contador de todos los slots
void resetAccesosSlot(int i);  // Reset contador de un slot

// ── INTEGRIDAD ───────────────────────────────
bool eepromValidate();  // Comprueba checksum de SystemConfig

#endif
