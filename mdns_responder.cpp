/*
 * mdns_responder.cpp
 * Responder mDNS minimal para sona.local → IP del Arduino.
 * Escucha en el multicast mDNS (224.0.0.251:5353) y responde
 * con un registro A cuando cualquier dispositivo pregunta por "sona.local".
 * Funciona en Chrome, Android, iOS y Windows sin configuración extra.
 */
#include "mdns_responder.h"

static WiFiUDP   _mdnsUdp;
static IPAddress _mdnsMulticast(224, 0, 0, 251);
static const uint16_t MDNS_PORT = 5353;

void mdnsResponderInit() {
  _mdnsUdp.beginMulticast(_mdnsMulticast, MDNS_PORT);
}

// Codifica un nombre DNS tipo "sona.local" en formato wire
static int encodeName(uint8_t* buf, const char* name) {
  int pos = 0;
  const char* p = name;
  while (*p) {
    const char* dot = strchr(p, '.');
    int labelLen = dot ? (dot - p) : (int)strlen(p);
    buf[pos++] = (uint8_t)labelLen;
    for (int i = 0; i < labelLen; i++) buf[pos++] = p[i];
    if (!dot) break;
    p = dot + 1;
  }
  buf[pos++] = 0; // fin de nombre
  return pos;
}

// Lee un nombre DNS desde el buffer (ignora punteros de compresión)
static String decodeName(uint8_t* buf, int& offset, int len) {
  String name = "";
  while (offset < len) {
    uint8_t labelLen = buf[offset];
    if (labelLen == 0) { offset++; break; }
    if ((labelLen & 0xC0) == 0xC0) { offset += 2; break; } // puntero, ignorar
    offset++;
    if (name.length()) name += '.';
    for (int i = 0; i < labelLen && offset < len; i++)
      name += (char)tolower(buf[offset++]);
  }
  return name;
}

void mdnsResponderRun() {
  int packetSize = _mdnsUdp.parsePacket();
  if (packetSize == 0) return;

  uint8_t buf[512];
  int len = _mdnsUdp.read(buf, sizeof(buf));
  if (len < 12) return;

  // Solo procesar queries (bit QR = 0)
  if (buf[2] & 0x80) return;

  uint16_t qdcount = (buf[4] << 8) | buf[5];
  int offset = 12;

  for (int q = 0; q < qdcount && offset < len; q++) {
    String name = decodeName(buf, offset, len);
    uint16_t qtype  = (buf[offset] << 8) | buf[offset + 1]; offset += 2;
    /* uint16_t qclass = */ (buf[offset] << 8) | buf[offset + 1]; offset += 2;

    // Responder solo a tipo A (IPv4) para "sona.local"
    if (qtype != 1) continue;
    if (name != "sona.local") continue;

    // ── Construir respuesta mDNS ──────────────────────────────────
    uint8_t resp[512];
    resp[0] = buf[0]; resp[1] = buf[1]; // Transaction ID
    resp[2] = 0x84; resp[3] = 0x00;    // Flags: QR=1, AA=1, Response
    resp[4] = 0x00; resp[5] = 0x00;    // Questions: 0
    resp[6] = 0x00; resp[7] = 0x01;    // Answers: 1
    resp[8] = 0x00; resp[9] = 0x00;    // Authority: 0
    resp[10]= 0x00; resp[11]= 0x00;    // Additional: 0

    int pos = 12;
    // Nombre: "sona.local"
    pos += encodeName(resp + pos, "sona.local");
    // Type A
    resp[pos++] = 0x00; resp[pos++] = 0x01;
    // Class IN + cache-flush bit
    resp[pos++] = 0x80; resp[pos++] = 0x01;
    // TTL: 120 segundos
    resp[pos++] = 0x00; resp[pos++] = 0x00;
    resp[pos++] = 0x00; resp[pos++] = 0x78;
    // RDATA length: 4
    resp[pos++] = 0x00; resp[pos++] = 0x04;
    // IP del Arduino
    IPAddress ip = WiFi.localIP();
    resp[pos++] = ip[0]; resp[pos++] = ip[1];
    resp[pos++] = ip[2]; resp[pos++] = ip[3];

    // Enviar al grupo multicast mDNS
    _mdnsUdp.beginPacket(_mdnsMulticast, MDNS_PORT);
    _mdnsUdp.write(resp, pos);
    _mdnsUdp.endPacket();
  }
}
