#ifndef HARDWARE_IO_H
#define HARDWARE_IO_H

#include <Arduino.h>
#include <MFRC522.h>

// Objeto global
extern MFRC522 mfrc522;

void hardwareInit();
void handleRFID();
void earcon(int id);
void earconBoot();

#endif
