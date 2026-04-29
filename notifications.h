#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <Arduino.h>

// Envía un webhook HTTP POST a systemConfig.webhookUrl
// body: payload JSON a enviar
void sendWebhook(const String& event, const String& uid, const String& nombre, uint8_t result);

#endif
