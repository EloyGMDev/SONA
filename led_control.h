#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>

// Inicializa los pines del LED RGB
void ledInit();

// Establece el color del LED (0-255 por canal)
void setLED(uint8_t r, uint8_t g, uint8_t b);

// Colores predefinidos de estado
void ledOff();
void ledGreen();   // Acceso OK
void ledRed();     // Acceso denegado / error
void ledBlue();    // WiFi perdido / buscando
void ledYellow();  // Boot / procesando
void ledPurple();  // Lockdown activo
void ledCyan();    // NTP sincronizado
void ledWhite();   // Modo registro

// Parpadeo de color n veces (bloqueante corto, solo para feedback)
void ledBlink(uint8_t r, uint8_t g, uint8_t b, int times, int ms = 150);

#endif
