#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include "WiFiS3.h"

extern WiFiServer server;

void webServerInit();
void handleWebAdmin();
void handleDiscovery();
void wifiWatchdog();

#endif
