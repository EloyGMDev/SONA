#include "web_server.h"
#include "config.h"
#include "utils.h"
#include "database.h"
#include "hardware_io.h"
#include "ntp_sync.h"
#include <RTC.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <malloc.h>

extern "C" char* sbrk(int incr);
void enviar401(WiFiClient& client);
int getFreeHeap() {
  struct mallinfo mi = mallinfo();
  return mi.fordblks;
}

WiFiServer server(80);
WiFiUDP udp;
unsigned int udpPort = 42800;
char packetBuffer[255]; 

static void sp(WiFiClient& c, const char* s) { c.print(s); }

String getParam(String data, String param) {
  int pos = data.indexOf(param+"="); 
  if (pos==-1) return "";
  int start = pos+param.length()+1;
  int end = data.indexOf('&',start); 
  if (end==-1) end=data.length();
  return data.substring(start,end);
}

bool hasParam(String data, String param) {
  return data.indexOf(param+"=") == 0 || data.indexOf("&"+param+"=") != -1;
}

String escapeJSON(const String& src) {
  String out = "";
  for (unsigned int i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((unsigned char)c >= 32) out += c;
  }
  return out;
}

String urlDecode(const String& src) {
  String decoded = "";
  char temp[] = "00";
  for (unsigned int i = 0; i < src.length(); i++) {
    if (src[i] == '%') {
      if (i + 2 < src.length()) {
        temp[0] = src[i+1];
        temp[1] = src[i+2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (src[i] == '+') {
      decoded += ' ';
    } else {
      decoded += src[i];
    }
  }
  return decoded;
}

String getParamDecoded(String data, String param) {
  return urlDecode(getParam(data, param));
}

int indexOfIgnoreCase(const String& haystack, const String& needle, int startOffset = 0) {
  int hLen = haystack.length();
  int nLen = needle.length();
  if (nLen == 0) return 0;
  if (hLen < nLen) return -1;
  
  for (int i = startOffset; i <= hLen - nLen; i++) {
    bool match = true;
    for (int j = 0; j < nLen; j++) {
      char h = haystack[i + j];
      char n = needle[j];
      if (h >= 'A' && h <= 'Z') h += 32;
      if (n >= 'A' && n <= 'Z') n += 32;
      if (h != n) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return -1;
}

String getHeaderValue(const String& headers, const String& name) {
  int idx = indexOfIgnoreCase(headers, name);
  if (idx == -1) return "";
  
  int colonIdx = headers.indexOf(':', idx);
  if (colonIdx == -1) return "";
  
  for (int i = idx + name.length(); i < colonIdx; i++) {
    if (headers[i] != ' ' && headers[i] != '\t') return "";
  }
  
  int valStart = colonIdx + 1;
  int valEnd = headers.indexOf("\r\n", valStart);
  if (valEnd == -1) valEnd = headers.length();
  
  String val = headers.substring(valStart, valEnd);
  val.trim();
  return val;
}

void enviarConfigWiFiHTML(WiFiClient& client) {
  sp(client, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
  sp(client, R"html(<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>SONA - WiFi Setup</title>
<style>
body{background:#050505;color:#fff;font-family:sans-serif;padding:30px 15px;display:flex;justify-content:center;}
.card{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.05);border-radius:12px;padding:24px;width:100%;max-width:360px;box-shadow:0 10px 30px rgba(0,0,0,0.5);}
h2{font-size:18px;color:#00d2ff;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px;}
p{font-size:12px;color:#888;margin-bottom:20px;line-height:1.4;}
input{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);padding:12px;border-radius:8px;color:#fff;width:100%;margin-bottom:15px;outline:none;box-sizing:border-box;}
input:focus{border-color:#00d2ff;}
button{background:#00d2ff;color:#000;padding:12px;border-radius:8px;border:none;font-weight:bold;width:100%;cursor:pointer;transition:all 0.2s;}
button:hover{transform:translateY(-1px);box-shadow:0 5px 15px rgba(0,210,255,0.3);}
.msg{font-size:12px;text-align:center;margin-top:15px;color:#00d2ff;}
</style></head><body>
<div class='card'>
<h2>WiFi Setup</h2>
<p>Introduce los datos para conectar el dispositivo a tu red local / Introdueix les dades per a connectar el dispositiu a la teva xarxa local.</p>
<input id='s' placeholder='SSID (Red / Xarxa)'>
<input id='p' type='password' placeholder='Password (Contraseña / Contrasenya)'>
<button onclick='save()'>CONECTAR / CONNECTAR</button>
<div id='m' class='msg'></div>
</div>
<script>
function save(){
  var s=document.getElementById('s').value;
  var p=document.getElementById('p').value;
  if(!s){alert('SSID required');return;}
  document.getElementById('m').innerText='Connecting / Connectant...';
  fetch('/api/sysconfig',{
    method:'POST',
    body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)
  }).then(r=>{
    if(r.ok){
      document.getElementById('m').innerText='Config saved. Device is rebooting... / Configuració desada. El dispositiu s\'està reiniciant...';
    }else{
      document.getElementById('m').innerText='Error saving config / Error en desar la configuració';
    }
  });
}
</script></body></html>)html");
}

void procesarUpdate(String data) {
  int idx = getParam(data,"idx").toInt();
  String uid = getParam(data,"uid");
  String nom = getParamDecoded(data,"nom");
  int pat = getParam(data,"pat").toInt();
  int sh = getParam(data,"sh").toInt();
  int sm = getParam(data,"sm").toInt();
  int eh = getParam(data,"eh").toInt();
  int em = getParam(data,"em").toInt();
  int days = hasParam(data,"days") ? getParam(data,"days").toInt() : DAY_ALL;
  if (idx >= 0 && idx < MAX_AULAS) {
    uid.toUpperCase(); 
    uid.toCharArray(baseDatos[idx].uid, 15);
    nom.toCharArray(baseDatos[idx].nombre, 32);
    baseDatos[idx].patronID = constrain(pat,0,10);
    baseDatos[idx].startHour = sh;
    baseDatos[idx].startMin  = sm;
    baseDatos[idx].endHour   = eh;
    baseDatos[idx].endMin    = em;
    baseDatos[idx].days      = days;
    baseDatos[idx].enabled   = (uid.length() > 0);
    eepromSaveAulas();
    addLog("ADMIN","Slot "+String(idx)+" actualizado");
  }
}

// Handler para OTA (streaming directo a flash)
void handleOTA(WiFiClient& client, String allHeaders) {
  // Validar Content-Length
  String clStr = getHeaderValue(allHeaders, "Content-Length");
  if (clStr.length() == 0) { enviar401(client); return; }
  long cl = clStr.toInt();

  addLog("OTA", "Iniciando descarga (" + String(cl/1024) + " KB)...");

  if (!InternalStorage.open(cl)) {
    addLog("ERROR", "OTA: No hay espacio en flash");
    sp(client, "HTTP/1.1 500 Error\r\n\r\nError Flash");
    return;
  }

  long count = 0;
  unsigned long lastLog = millis();
  
  while (count < cl && client.connected()) {
    if (client.available()) {
      InternalStorage.write(client.read());
      count++;
      if (millis() - lastLog > 2000) {
        lastLog = millis();
        addLog("OTA", "Progreso: " + String((count * 100) / cl) + "%");
      }
    }
  }

  InternalStorage.close();
  
  if (count >= cl) { 
    addLog("SISTEMA", "¡OTA Éxito! Reiniciando...");
    sp(client, "HTTP/1.1 200 OK\r\n\r\nOK");
    delay(1000);
    InternalStorage.apply(); 
  } else {
    addLog("ERROR", "OTA falló: Conexión perdida");
    sp(client, "HTTP/1.1 500 Error\r\n\r\nFallo de conexión");
  }
}

void enviar401(WiFiClient& client) {
  sp(client, "HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n\r\nAcceso denegado");
}

void enviarPanelHTML(WiFiClient& client) {
  sp(client, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
  sp(client, R"html(<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SONA - Control Center</title><link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600&display=swap' rel='stylesheet'>
<style>
:root{--bg:#050505;--panel:#111111;--acc:#00d2ff;--acc-glow:rgba(0,210,255,0.3);--err:#ff4b2b;--txt:#ffffff;--dim:#888;--transition:all 0.3s cubic-bezier(0.4,0,0.2,1);}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--bg);color:var(--txt);font-family:'Outfit',sans-serif;overflow-x:hidden;-webkit-font-smoothing:antialiased;}
.glass{background:rgba(255,255,255,0.03);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.05);border-radius:16px;}
.navbar{padding:20px 40px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid rgba(255,255,255,0.1);background:rgba(0,0,0,0.8);position:sticky;top:0;z-index:100;backdrop-filter:blur(10px);}
.logo{font-size:20px;font-weight:600;letter-spacing:-0.5px;background:linear-gradient(90deg,#00c8ff,#0066ff);-webkit-background-clip:text;-webkit-text-fill-color:transparent;}
.stats{display:flex;gap:20px;font-size:12px;color:var(--dim);align-items:center;}
.stat-item b{color:#fff;}
.main{display:grid;grid-template-columns:1fr 340px;gap:24px;padding:30px;max-width:1400px;margin:0 auto;}
@media(max-width:900px){.main{grid-template-columns:1fr;padding:15px;}}
.card{padding:24px;margin-bottom:24px;transition:var(--transition);}
.card:hover{border-color:rgba(0,255,163,0.2);box-shadow:0 10px 30px rgba(0,0,0,0.5);}
.card-title{font-size:14px;font-weight:600;color:var(--acc);margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;text-transform:uppercase;letter-spacing:1px;}
.terminal{height:250px;overflow-y:auto;background:#000;border-radius:12px;padding:15px;font-family:'Courier New',monospace;font-size:12px;line-height:1.6;border:1px solid #222;}
.log-line{margin-bottom:4px;animation:fadeIn 0.3s ease;opacity:0.8;}.log-line:hover{opacity:1;color:var(--acc);}
@keyframes fadeIn{from{opacity:0;transform:translateY(5px);}to{opacity:0.8;transform:translateY(0);}}
table{width:100%;border-collapse:separate;border-spacing:0 8px;}
th{padding:12px;text-align:left;font-size:11px;color:var(--dim);text-transform:uppercase;}
td{padding:14px 12px;background:rgba(255,255,255,0.02);border-top:1px solid rgba(255,255,255,0.05);border-bottom:1px solid rgba(255,255,255,0.05);}
td:first-child{border-left:1px solid rgba(255,255,255,0.05);border-radius:8px 0 0 8px;}
td:last-child{border-right:1px solid rgba(255,255,255,0.05);border-radius:0 8px 8px 0;}
input,select{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);padding:12px;border-radius:8px;color:#fff;width:100%;transition:var(--transition);outline:none;}
input:focus{border-color:var(--acc);background:rgba(255,255,255,0.08);}
button{padding:12px 20px;border-radius:8px;border:none;font-weight:600;cursor:pointer;transition:var(--transition);font-family:'Outfit',sans-serif;}
.btn-acc{background:var(--acc);color:#000;}.btn-acc:hover{transform:translateY(-2px);box-shadow:0 5px 15px var(--acc-glow);}
.btn-err{background:rgba(255,75,43,0.1);color:var(--err);border:1px solid var(--err);}.btn-err:hover{background:var(--err);color:#fff;}
.switch{position:relative;display:inline-block;width:34px;height:20px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;inset:0;background-color:#333;transition:.4s;border-radius:20px;}
.slider:before{position:absolute;content:'';height:14px;width:14px;left:3px;bottom:3px;background-color:white;transition:.4s;border-radius:50%;}
input:checked + .slider{background-color:var(--acc);}
input:checked + .slider:before{transform:translateX(14px);}
select option{background:#111;color:#fff;}
</style></head><body>
<div class='navbar'><div class='logo' id='navLogo'>SONA SYSTEM</div><div class='stats'>
<div class='stat-item'>
  <select id='langSel' onchange='setLanguage(this.value)' style='background:transparent;border:1px solid var(--dim);color:#fff;padding:4px 8px;border-radius:4px;font-size:11px;cursor:pointer;width:auto;'>
    <option value='es' style='background:#111;color:#fff;'>ES</option>
    <option value='ca' style='background:#111;color:#fff;'>CA</option>
  </select>
</div>
<div class='stat-item'><span data-translate='uptime'>UPTIME</span>: <b id='sup'>--s</b></div>
<div class='stat-item'><span data-translate='signal'>SIGNAL</span>: <b id='srs'>--dBm</b></div>
</div></div>
<div id='connErrorBanner' style='display:none; background:#ff4b2b; color:#fff; text-align:center; padding:12px; font-size:12px; font-weight:600; position:sticky; top:60px; z-index:99; box-shadow:0 4px 10px rgba(0,0,0,0.3);'></div>

<!-- Auth Overlay -->
<div id='ao' style='position:fixed;inset:0;background:var(--bg);z-index:999;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:24px;transition:var(--transition);'>
<div class='logo' style='font-size:32px' data-translate='restricted_access'>RESTRICTED ACCESS</div>
<div class='glass' style='padding:40px;width:320px;display:grid;gap:20px;'>
<input id='pi' type='password' style='text-align:center' placeholder='Admin Key' data-translate='admin_key'><button class='btn-acc' onclick='login()' data-translate='authenticate'>AUTHENTICATE</button>
<div id='ae' style='color:var(--err);text-align:center;font-size:12px;'></div></div></div>

<div class='main' id='app' style='display:none; opacity:0; transition:opacity 0.8s ease;'>
<div><div class='card glass'><div class='card-title'><span data-translate='activity_stream'>Activity Stream</span>
<div><span style='font-size:10px;margin-right:8px;color:var(--dim)' data-translate='mute_logs'>Mute Logs</span><label class='switch'><input type='checkbox' id='lm'><span class='slider'></span></label></div></div>
<div class='terminal' id='la'></div></div>
<div class='card glass'><div class='card-title' data-translate='db_manager'>Database Manager</div><div style='overflow-x:auto'><table>
<thead><tr><th>#</th><th data-translate='uid'>UID</th><th data-translate='name'>Location Name</th><th data-translate='pattern'>Pattern</th><th data-translate='schedule'>Schedule</th><th data-translate='access'>Access</th><th></th></tr></thead><tbody id='ab'></tbody></table></div></div>
<div class='card glass'><div class='card-title'><span data-translate='sona_network'>Red de Dispositivos SONA</span><button class='btn-acc' style='padding:5px 10px;font-size:10px' onclick='discoverDevices()' data-translate='scan'>SCAN</button></div>
<div id='sonaNetList' style='display:grid;gap:10px;'></div></div></div>

<div><div class='card glass'><div class='card-title' data-translate='active_editor'>Active Editor</div><div style='display:grid;gap:15px;'>
<div><label style='font-size:10px;color:var(--dim)' data-translate='slot_uid_name'>SLOT / UID / NAME</label><div style='display:flex;gap:5px;'><input id='fi' type='number' placeholder='Slot' style='width:70px'><input id='fu' placeholder='UID'><input id='fn' placeholder='Name' data-translate='name'></div></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='permitted_schedule'>PERMITTED SCHEDULE (START - END)</label><div style='display:flex;gap:5px;align-items:center;'><input id='fsh' type='number' placeholder='HH'><input id='fsm' type='number' placeholder='MM'><span>:</span><input id='feh' type='number' placeholder='HH'><input id='fem' type='number' placeholder='MM'></div></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='permitted_days'>DÍAS PERMITIDOS</label><div style='display:flex;gap:10px;margin-top:5px;flex-wrap:wrap;'><label style='font-size:12px;'><input type='checkbox' id='d1' checked> L</label><label style='font-size:12px;'><input type='checkbox' id='d2' checked> M</label><label style='font-size:12px;'><input type='checkbox' id='d3' checked> X</label><label style='font-size:12px;'><input type='checkbox' id='d4' checked> J</label><label style='font-size:12px;'><input type='checkbox' id='d5' checked> V</label><label style='font-size:12px;'><input type='checkbox' id='d6' checked> S</label><label style='font-size:12px;'><input type='checkbox' id='d7' checked> D</label></div></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='sound_pattern'>SOUND PATTERN</label><select id='fp'><option value='1'>Standard Success</option><option value='2'>Double Beep</option><option value='3'>Triple Fast</option><option value='4'>Ascending Melody</option><option value='5'>Descending Soft</option><option value='6'>Futuristic Laser</option><option value='7'>Happy Chime</option><option value='8'>Fanfare Victory</option><option value='9'>Cyber Pulsar</option><option value='10'>Special VIP</option></select></div>
<button class='btn-acc' style='margin-top:10px' onclick='save()' data-translate='commit_changes'>COMMIT CHANGES</button></div></div>

<!-- System Configuration Card -->
<div class='card glass'><div class='card-title' data-translate='system_config'>System Configuration</div><div style='display:grid;gap:15px;'>
<div><label style='font-size:10px;color:var(--dim)' data-translate='wifi_ssid'>WIFI NETWORK (SSID)</label><input id='sysSsid' placeholder='SSID'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='wifi_pass'>WIFI PASSWORD</label><input id='sysPass' type='password' placeholder='Password' data-translate='wifi_pass'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='hostname'>SYSTEM HOSTNAME</label><input id='sysHost' placeholder='sona'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='classroom_name'>Nombre del Aula / Clase</label><input id='sysClass' placeholder='Clase 1'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='classroom_number'>Número de Aula</label><input id='sysClassNum' placeholder='Ej: 202'></div>
<div><label style='font-size:10px;color:var(--dim)'>LATITUD GPS</label><input id='sysLat' type='number' step='any' placeholder='0.0'></div>
<div><label style='font-size:10px;color:var(--dim)'>LONGITUD GPS</label><input id='sysLon' type='number' step='any' placeholder='0.0'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='ntp_server'>NTP SERVER</label><input id='sysNtp' placeholder='pool.ntp.org'></div>
<div><label style='font-size:10px;color:var(--dim)' data-translate='utc_offset'>TIMEZONE (UTC OFFSET)</label><input id='sysOffset' type='number' placeholder='2'></div>
<div style='display:flex;align-items:center;justify-content:space-between;'>
<label style='font-size:11px;' data-translate='quiet_mode'>QUIET MODE</label><label class='switch'><input type='checkbox' id='sysQuietEn'><span class='slider'></span></label></div>
<div id='sysQuietTimes' style='display:flex;gap:5px;align-items:center;'>
<label style='font-size:10px;color:var(--dim)' data-translate='quiet_start'>START</label><input id='sysQStart' type='number' placeholder='22'>
<label style='font-size:10px;color:var(--dim)' data-translate='quiet_end'>END</label><input id='sysQEnd' type='number' placeholder='8'></div>

<button class='btn-acc' style='margin-top:10px' onclick='saveSysConfig()' data-translate='save_system_config'>SAVE CONFIGURATION</button></div></div>

<div class='card glass'><div class='card-title' data-translate='security_settings'>Security Settings</div><div style='display:grid;gap:12px;'>
<label style='font-size:10px;color:var(--dim)' data-translate='new_admin_password'>NEW ADMIN PASSWORD</label><div style='display:flex;gap:10px;'><input id='np' type='password' placeholder='Password' data-translate='new_admin_password'><button class='btn-err' onclick='chpwd()' data-translate='update'>UPDATE</button></div>
<hr style='border:none;border-top:1px solid rgba(255,255,255,0.07);margin:4px 0;'>
<p style='font-size:11px;color:var(--dim)' data-translate='eeprom_reset_desc'>Borra todos los datos y configuración almacenados. Útil si el dispositivo no guarda la configuración correctamente.</p>
<button class='btn-err' style='width:100%' onclick='resetEeprom()' data-translate='eeprom_reset'>RESET EEPROM</button>
</div></div>

<div class='card glass'><div class='card-title' data-translate='system_firmware'>System Firmware</div><div style='display:grid;gap:12px;'>
<p style='font-size:11px;color:var(--dim)'><span data-translate='current_version'>Current Version:</span> )html" VERSION R"html(</p>
<input type='file' id='ofile' multiple style='font-size:11px;margin-bottom:10px;'>
<button onclick='uo()' class='btn-err' style='width:100%' data-translate='flash_new_binary'>FLASH NEW BINARY</button>
<button onclick='deployAll()' class='btn-acc' style='width:100%' data-translate='deploy_all'>DEPLOY TO ALL DEVICES</button>
<div id='os' style='font-size:10px;margin-top:5px;color:var(--acc);line-height:1.6;'></div>
<div id='pBarBg' style='width:100%;height:6px;background:rgba(255,255,255,0.05);border-radius:3px;margin-top:5px;display:none;overflow:hidden;'>
  <div id='pBar' style='width:0%;height:100%;background:var(--acc);transition:width 0.1s ease;'></div>
</div></div></div></div></div>

<script>
const i18n = {
  es: {
    restricted_access: 'ACCESO RESTRINGIDO',
    admin_key: 'Clave de Administrador',
    authenticate: 'AUTENTICAR',
    auth_failed: 'Autenticación Fallida',
    uptime: 'TIEMPO ACTIVO',
    signal: 'SEÑAL',
    activity_stream: 'Flujo de Actividad',
    mute_logs: 'Silenciar Registros',
    db_manager: 'Gestor de Base de Datos',
    uid: 'UID',
    name: 'Nombre',
    pattern: 'Patrón',
    schedule: 'Horario',
    access: 'Accesos',
    edit: 'EDITAR',
    active_editor: 'Editor Activo',
    slot_uid_name: 'SLOT / UID / NOMBRE',
    permitted_schedule: 'HORARIO PERMITIDO (INICIO - FIN)',
    permitted_days: 'DÍAS PERMITIDOS',
    sound_pattern: 'PATRÓN DE SONIDO',
    commit_changes: 'GUARDAR CAMBIOS',
    security_settings: 'Configuración de Seguridad',
    new_admin_password: 'NUEVA CONTRASEÑA ADMIN',
    update: 'ACTUALIZAR',
    system_firmware: 'Firmware del Sistema',
    current_version: 'Versión Actual:',
    flash_new_binary: 'FLASHEAR NUEVO BINARIO',
    system_config: 'Configuración del Sistema',
    wifi_ssid: 'RED WIFI (SSID)',
    wifi_pass: 'CONTRASEÑA WIFI',
    hostname: 'HOSTNAME DEL SISTEMA',
    classroom_name: 'Nombre del Aula / Clase',
    classroom_number: 'Número de Aula',
    ntp_server: 'SERVIDOR NTP',
    utc_offset: 'ZONA HORARIA (OFFSET UTC)',
    quiet_mode: 'MODO SILENCIOSO',
    quiet_start: 'INICIO',
    quiet_end: 'FIN',
    save_system_config: 'GUARDAR CONFIGURACIÓN',
    sona_network: 'Dispositivos SONA en la Red',
    scan: 'ESCANEAR',
    connect: 'Conectarse',
    import: 'Importar',
    export: 'Exportar',
    scanning: 'Escaneando red...',
    eeprom_reset: 'RESET EEPROM',
    eeprom_reset_desc: 'Borra todos los datos y configuración almacenados. Útil si el dispositivo no guarda la configuración.',
    eeprom_reset_confirm: '⚠️ Esto borrará TODA la base de datos, historial y configuración del dispositivo. ¿Continuar?',
    deploy_all: 'DESPLEGAR EN TODOS LOS DISPOSITIVOS'
  },
  ca: {
    restricted_access: 'ACCÉS RESTRINGIT',
    admin_key: 'Clau d\'Administrador',
    authenticate: 'AUTENTICAR',
    auth_failed: 'Autenticació Fallida',
    uptime: 'TEMPS ACTIU',
    signal: 'SENYAL',
    activity_stream: 'Flux d\'Activitat',
    mute_logs: 'Silenciar Registres',
    db_manager: 'Gestor de Base de Dades',
    uid: 'UID',
    name: 'Nom',
    pattern: 'Patró',
    schedule: 'Horari',
    access: 'Accessos',
    edit: 'EDITAR',
    active_editor: 'Editor Actiu',
    slot_uid_name: 'SLOT / UID / NOM',
    permitted_schedule: 'HORARI PERMÈS (INICI - FI)',
    permitted_days: 'DIES PERMESOS',
    sound_pattern: 'PATRÓ DE SO',
    commit_changes: 'DESAR CANVIS',
    security_settings: 'Configuració de Seguretat',
    new_admin_password: 'NOVA CONTRASENYA ADMIN',
    update: 'ACTUALITZAR',
    system_firmware: 'Firmware del Sistema',
    current_version: 'Versió Actual:',
    flash_new_binary: 'FLASHEJAR NOU BINARI',
    system_config: 'Configuració del Sistema',
    wifi_ssid: 'XARXA WIFI (SSID)',
    wifi_pass: 'CONTRASENYA WIFI',
    hostname: 'HOSTNAME DEL SISTEMA',
    classroom_name: 'Nom de l\'Aula / Classe',
    classroom_number: 'Número d\'Aula',
    ntp_server: 'SERVIDOR NTP',
    utc_offset: 'ZONA HORÀRIA (DIFERÈNCIA UTC)',
    quiet_mode: 'MODE SILENCIÓS',
    quiet_start: 'INICI',
    quiet_end: 'FI',
    save_system_config: 'DESAR CONFIGURACIÓ',
    sona_network: 'Dispositius SONA a la Xarxa',
    scan: 'ESCANEJAR',
    connect: 'Connectar',
    import: 'Importar',
    export: 'Exportar',
    scanning: 'Escanejant xarxa...',
    eeprom_reset: 'RESET EEPROM',
    eeprom_reset_desc: 'Esborra totes les dades i configuració emmagatzemades. Útil si el dispositiu no desa la configuració.',
    eeprom_reset_confirm: '⚠️ Això esborrarà TOTA la base de dades, historial i configuració del dispositiu. Continuar?',
    deploy_all: 'DESPLEGAR EN TOTS ELS DISPOSITIUS'
  }
};

function setLanguage(lang){
  localStorage.setItem('esp_lang', lang);
  document.getElementById('langSel').value = lang;
  document.querySelectorAll('[data-translate]').forEach(el => {
    const k = el.getAttribute('data-translate');
    if(i18n[lang] && i18n[lang][k]){
      if(el.tagName === 'INPUT' && (el.type === 'text' || el.type === 'password' || el.type === 'number')){
        el.placeholder = i18n[lang][k];
      } else {
        el.innerText = i18n[lang][k];
      }
    }
  });
}

// Inicializar idioma en el arranque
var L = localStorage.getItem('esp_lang') || 'es';
setLanguage(L);

function crc32(s){var t=[];for(var i=0;i<256;i++){var c=i;for(var j=0;j<8;j++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[i]=c;}var r=0xFFFFFFFF;for(var i=0;i<s.length;i++)r=t[(r^s.charCodeAt(i))&0xFF]^(r>>>8);return(~r>>>0).toString(16).toUpperCase().padStart(8,'0');}
var H=localStorage.getItem('esp_h')||'';if(H)checkAuth();
   function login(){H=crc32(document.getElementById('pi').value);checkAuth();}
   var errMsgs = {
     es: "⚠️ ERROR DE CONEXIÓN: Si estás usando 'sona.local', intenta acceder con la IP del Arduino (ej: http://192.168.X.X) en la barra de direcciones.",
     ca: "⚠️ ERROR DE CONNEXIÓ: Si estàs fent servir 'sona.local', intenta accedir amb la IP directa de l'Arduino (ex: http://192.168.X.X) per evitar errors de DNS."
   };
   function checkAuth(){fetch('/api/log',{headers:{'X-Auth':H}}).then(r=>{if(r.ok){var b=document.getElementById('connErrorBanner');if(b)b.style.display='none';localStorage.setItem('esp_h',H);document.getElementById('ao').style.transform='translateY(-100%)';setTimeout(()=>{document.getElementById('ao').style.display='none';document.getElementById('app').style.display='grid';setTimeout(()=>document.getElementById('app').style.opacity='1',50);},400);start();}else{document.getElementById('ae').innerText= (L==='ca' ? i18n.ca.auth_failed : i18n.es.auth_failed);localStorage.removeItem('esp_h');}}).catch(e=>{console.error(e);var b=document.getElementById('connErrorBanner');if(b){b.innerText=errMsgs[L]||errMsgs.es;b.style.display='block';}});}
   function api(p,o={}){
     o.headers=Object.assign({'X-Auth':H},o.headers||{});
     return fetch(p,o).then(r=>{
       if(r.status===401){
         localStorage.removeItem('esp_h');
         document.getElementById('ae').innerText=(L==='ca'?i18n.ca.auth_failed:i18n.es.auth_failed);
         document.getElementById('ao').style.display='flex';
         setTimeout(()=>document.getElementById('ao').style.transform='translateY(0)',50);
         document.getElementById('app').style.opacity='0';
         setTimeout(()=>document.getElementById('app').style.display='none',400);
         throw new Error('Unauthorized');
       }
       var b=document.getElementById('connErrorBanner');if(b)b.style.display='none';
       return r;
     }).catch(err=>{
       if(err.message!=='Unauthorized'){
         var b=document.getElementById('connErrorBanner');
         if(b){b.innerText=errMsgs[L]||errMsgs.es;b.style.display='block';}
       }
       throw err;
     });
   }
   async function start(){
     try {
       await loadLog();
       await loadAulas();
       await discoverDevices();
     } catch(e) {
       console.error(e);
     }
     setInterval(loadLog, 4000);
   }

var sysConfigLoaded = false;
function loadLog(){
  if(document.getElementById('lm').checked)return Promise.resolve();
  return api('/api/log').then(r=>r.json()).then(d=>{
    document.getElementById('sup').innerText=d.uptime+'s';
    document.getElementById('srs').innerText=d.rssi+'dBm';
    document.getElementById('navLogo').innerText = (d.sysClass && d.sysClass.trim() !== '') ? d.sysClass.toUpperCase() : 'SONA SYSTEM';
    
    if(!sysConfigLoaded){
      document.getElementById('sysSsid').value = d.sysSsid || '';
      document.getElementById('sysPass').value = d.sysPass || '';
      document.getElementById('sysHost').value = d.sysHost || '';
      document.getElementById('sysClass').value = d.sysClass || '';
      document.getElementById('sysClassNum').value = d.sysClassNum || '';
      document.getElementById('sysNtp').value = d.sysNtp || '';
      document.getElementById('sysOffset').value = d.sysOffset !== undefined ? d.sysOffset : 2;
      document.getElementById('sysQuietEn').checked = d.sysQuietEn || false;
      document.getElementById('sysQStart').value = d.sysQStart !== undefined ? d.sysQStart : 22;
      document.getElementById('sysQEnd').value = d.sysQEnd !== undefined ? d.sysQEnd : 8;
      document.getElementById('sysLat').value = d.lat !== undefined ? d.lat : 0.0;
      document.getElementById('sysLon').value = d.lon !== undefined ? d.lon : 0.0;
      sysConfigLoaded = true;
    }
    
    var h='';
    d.log.forEach(l=>{
      let c = '';
      if(l.l === 2) c = 'color:#ffcc00;';
      if(l.l === 3) c = 'color:#ff4b2b;';
      h+=`<div class='log-line' style='${c}'>${l.t}</div>`;
    });
    document.getElementById('la').innerHTML=h;
    var l=document.getElementById('la');
    l.scrollTop=l.scrollHeight;
  });
}
var aulasArray = [];
function loadAulas(){
  return api('/api/aulas').then(r=>r.json()).then(a=>{
    aulasArray = a;
    var h='';
    a.forEach(x=>{
      var sh = x.sh !== undefined ? x.sh : 0;
      var sm = x.sm !== undefined ? x.sm : 0;
      var eh = x.eh !== undefined ? x.eh : 0;
      var em = x.em !== undefined ? x.em : 0;
      var dVal = x.days !== undefined ? x.days : 127;
      var dayLabels = ["L","M","X","J","V","S","D"];
      var daysStr = "";
      for (var d=0; d<7; d++) {
        if (dVal & (1<<d)) {
          daysStr += "<b style='color:#00d2ff'>" + dayLabels[d] + "</b>";
        } else {
          daysStr += "<span style='color:#333'>" + dayLabels[d] + "</span>";
        }
        daysStr += " ";
      }
      h+=`<tr><td>${x.idx}</td><td>${x.uid}</td><td style='font-weight:600'>${x.nombre}</td><td>P${x.pat}</td><td>${sh}:${sm.toString().padStart(2,'0')}-${eh}:${em.toString().padStart(2,'0')}<br><span style='font-size:10px;'>${daysStr}</span></td><td>${x.accesos}</td><td style='text-align:right'><button class='btn-acc' style='padding:5px 10px;font-size:10px' onclick='es(${x.idx})' data-translate='edit'>EDIT</button></td></tr>`;
    });
    document.getElementById('ab').innerHTML=h;
    setLanguage(L); // Re-traducir tras renderizar la tabla
  });
}
function es(idx){
  var x = aulasArray.find(item => item.idx === idx);
  if(!x) return;
  document.getElementById('fi').value=x.idx;
  document.getElementById('fu').value=x.uid;
  document.getElementById('fn').value=x.nombre;
  document.getElementById('fp').value=x.pat;
  document.getElementById('fsh').value=x.sh !== undefined ? x.sh : 0;
  document.getElementById('fsm').value=x.sm !== undefined ? x.sm : 0;
  document.getElementById('feh').value=x.eh !== undefined ? x.eh : 0;
  document.getElementById('fem').value=x.em !== undefined ? x.em : 0;
  var dVal = x.days !== undefined ? x.days : 127;
  for (var d=1; d<=7; d++) {
    document.getElementById('d'+d).checked = !!(dVal & (1<<(d-1)));
  }
  window.scrollTo({top:0,behavior:'smooth'});
}
function save(){
  var dVal = 0;
  for (var d=1; d<=7; d++) {
    if (document.getElementById('d'+d).checked) {
      dVal |= (1<<(d-1));
    }
  }
  var b=`idx=${document.getElementById('fi').value}&uid=${document.getElementById('fu').value}&nom=${encodeURIComponent(document.getElementById('fn').value)}&pat=${document.getElementById('fp').value}&sh=${document.getElementById('fsh').value}&sm=${document.getElementById('fsm').value}&eh=${document.getElementById('feh').value}&em=${document.getElementById('fem').value}&days=${dVal}`;
  api('/api/update',{method:'POST',body:b}).then(r=>{if(r.ok)loadAulas();});
}
function chpwd(){
  const msg = L==='ca' ? '¿Canviar contrasenya mestra?' : '¿Cambiar contraseña maestra?';
  if(!confirm(msg))return;
  api('/api/pwd',{method:'POST',body:'pwd='+encodeURIComponent(document.getElementById('np').value)}).then(r=>{if(r.ok)location.reload();});
}
function resetEeprom(){
  const msg = L==='ca' ? i18n.ca.eeprom_reset_confirm : i18n.es.eeprom_reset_confirm;
  if(!confirm(msg)) return;
  api('/api/eepromreset',{method:'POST',body:''}).then(r=>{
    if(r.ok){
      const okMsg = L==='ca' ? 'EEPROM esborrada. El dispositiu es reinicia...' : 'EEPROM borrada. El dispositivo se está reiniciando...';
      alert(okMsg);
      setTimeout(()=>location.reload(), 6000);
    }
  }).catch(()=>{
    const okMsg = L==='ca' ? 'EEPROM esborrada. El dispositiu es reinicia...' : 'EEPROM borrada. El dispositivo se está reiniciando...';
    alert(okMsg);
    setTimeout(()=>location.reload(), 6000);
  });
}
function saveSysConfig(){
  var b=`ssid=${encodeURIComponent(document.getElementById('sysSsid').value)}&pass=${encodeURIComponent(document.getElementById('sysPass').value)}&host=${encodeURIComponent(document.getElementById('sysHost').value)}&class=${encodeURIComponent(document.getElementById('sysClass').value)}&classNum=${encodeURIComponent(document.getElementById('sysClassNum').value)}&ntp=${encodeURIComponent(document.getElementById('sysNtp').value)}&offset=${document.getElementById('sysOffset').value}&quietEn=${document.getElementById('sysQuietEn').checked}&qStart=${document.getElementById('sysQStart').value}&qEnd=${document.getElementById('sysQEnd').value}&lat=${document.getElementById('sysLat').value}&lon=${document.getElementById('sysLon').value}`;
  const msg = L==='ca' ? '¿Desar configuració i reiniciar dispositiu?' : '¿Guardar configuración y reiniciar dispositivo?';
  if(!confirm(msg)) return;
  api('/api/sysconfig',{method:'POST',body:b}).then(r=>{
    if(r.ok) {
      const okMsg = L==='ca' ? 'Configuració desada. El dispositiu s\'està reiniciant...' : 'Configuración guardada. El dispositivo se está reiniciando...';
      alert(okMsg);
      setTimeout(() => location.reload(), 5000);
    }
  });
}
function discoverDevices(){
  const container = document.getElementById('sonaNetList');
  container.innerHTML = `<div style="text-align:center;color:var(--dim);font-size:12px;padding:20px;">${L==='ca' ? i18n.ca.scanning : i18n.es.scanning}</div>`;
  return api('/api/discover').then(r => r.json()).then(devices => {
    if (devices.length === 0) {
      container.innerHTML = `<div style="text-align:center;color:var(--dim);font-size:12px;padding:20px;">${L==='ca' ? 'No s\'han trobat altres dispositius SONA' : 'No se encontraron otros dispositivos SONA'}</div>`;
      return;
    }
    let h = '';
    devices.forEach(d => {
      if (d.ip === window.location.hostname || d.ip === window.location.host || d.name === document.getElementById('sysHost').value) return;
      const cName = d.class || 'Sona Device';
      h += `
<div style="display:flex;justify-content:space-between;align-items:center;background:rgba(255,255,255,0.02);padding:12px;border-radius:8px;border:1px solid rgba(255,255,255,0.05);margin-bottom:8px;">
  <div>
    <div style="font-weight:600;color:#00ffa3;">${d.name}.local <span style="color:#888;font-weight:400">(${cName})</span></div>
    <div style="font-size:11px;color:#888;">IP: ${d.ip} | v${d.ver}</div>
  </div>
  <div style="display:flex;gap:5px;">
    <a href="http://${d.ip}" target="_blank" class="btn-acc" style="padding:6px 12px;font-size:11px;text-decoration:none;display:inline-block;border-radius:6px;color:#000;font-weight:bold;text-align:center;">${L==='ca' ? i18n.ca.connect : i18n.es.connect}</a>
    <button class="btn-acc" style="padding:6px 12px;font-size:11px;border-radius:6px;" onclick="syncFrom('${d.ip}', '${d.name}', '${cName}')">${L==='ca' ? i18n.ca.import : i18n.es.import}</button>
    <button class="btn-acc" style="padding:6px 12px;font-size:11px;border-radius:6px;" onclick="syncTo('${d.ip}', '${d.name}', '${cName}')">${L==='ca' ? i18n.ca.export : i18n.es.export}</button>
  </div>
</div>`;
    });
    if (h === '') {
      container.innerHTML = `<div style="text-align:center;color:var(--dim);font-size:12px;padding:20px;">${L==='ca' ? 'No s\'han trobat altres dispositius SONA' : 'No se encontraron otros dispositivos SONA'}</div>`;
    } else {
      container.innerHTML = h;
    }
  }).catch(err => {
    container.innerHTML = `<div style="text-align:center;color:var(--err);font-size:12px;padding:20px;">Error: ${err.message}</div>`;
  });
}

function syncFrom(ip, hostname, className) {
  const targetPwd = prompt(L === 'ca' ? `Introdueix la contrasenya d'administrador per a ${hostname} (${ip}):` : `Introduce la contraseña de administrador para ${hostname} (${ip}):`, '');
  if (targetPwd === null) return;
  const targetHash = crc32(targetPwd);
  
  fetch(`http://${ip}/api/backup`, {
    headers: { 'X-Auth': targetHash }
  })
  .then(res => {
    if (!res.ok) throw new Error(L === 'ca' ? 'Contrasenya incorrecta o error en connectar.' : 'Contraseña incorrecta o error al conectar.');
    return res.arrayBuffer();
  })
  .then(buffer => {
    return api('/api/restore', {
      method: 'POST',
      body: buffer
    });
  })
  .then(res => {
    if (res.ok) {
      alert(L === 'ca' ? 'Sincronització completada amb èxit.' : 'Sincronización completada con éxito.');
      loadAulas();
    } else {
      alert(L === 'ca' ? 'Error en restaurar al dispositiu local.' : 'Error al restaurar en el dispositivo local.');
    }
  })
  .catch(err => alert('Error: ' + err.message));
}

function syncTo(ip, hostname, className) {
  const targetPwd = prompt(L === 'ca' ? `Introdueix la contrasenya d'administrador per a ${hostname} (${ip}):` : `Introduce la contraseña de administrador para ${hostname} (${ip}):`, '');
  if (targetPwd === null) return;
  const targetHash = crc32(targetPwd);
  
  api('/api/backup')
  .then(res => {
    if (!res.ok) throw new Error('Error al obtener la base de datos local.');
    return res.arrayBuffer();
  })
  .then(buffer => {
    return fetch(`http://${ip}/api/restore`, {
      method: 'POST',
      headers: { 'X-Auth': targetHash },
      body: buffer
    });
  })
  .then(res => {
    if (res.ok) {
      alert(L === 'ca' ? 'Dispositiu remot sincronitzat amb èxit.' : 'Dispositivo remoto sincronizado con éxito.');
    } else {
      alert(L === 'ca' ? 'Error en restaurar al dispositiu remot.' : 'Error al restaurar en el dispositivo remoto.');
    }
  })
  .catch(err => alert('Error: ' + err.message));
}

function uploadWithProgress(url, file, headers, onProgress, onLoad, onError) {
  const xhr = new XMLHttpRequest();
  xhr.open('POST', url, true);
  for (let key in headers) {
    xhr.setRequestHeader(key, headers[key]);
  }
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      onProgress(pct);
    }
  };
  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) {
      onLoad(xhr.responseText);
    } else {
      onError(new Error(`Status ${xhr.status}`));
    }
  };
  xhr.onerror = function() {
    onError(new Error('Network error'));
  };
  xhr.send(file);
}

function deployAll() {
  const files = document.getElementById('ofile').files;
  if (files.length === 0) {
    alert(L === 'ca' ? 'Selecciona al menys un arxiu .bin' : 'Selecciona al menos un archivo .bin');
    return;
  }
  
  if (!confirm(L === 'ca' ? 'Estàs segur de voler desplegar el firmware a tots els dispositius?' : '¿Estás seguro de querer desplegar el firmware en todos los dispositivos?')) return;
  
  const remotePwd = prompt(L === 'ca' ? 'Introdueix la contrasenya d\'administrador per als dispositius remots (obligatori):' : 'Introduce la contraseña de administrador para los dispositivos remotos (obligatorio):', '');
  if (remotePwd === null) return;
  if (remotePwd.trim() === '') {
    alert(L === 'ca' ? 'La contrasenya és obligatòria per al desplegament.' : 'La contraseña es obligatoria para el despliegue.');
    return;
  }
  const remoteHash = crc32(remotePwd);
  
  const statusDiv = document.getElementById('os');
  statusDiv.innerHTML = L === 'ca' ? 'Cercant dispositius actius...' : 'Buscando dispositivos activos...';
  
  fetch('/api/discover', { headers: { 'X-Auth': H } })
  .then(r => r.json())
  .then(devices => {
    let promises = [];
    statusDiv.innerHTML = '';
    
    // 1. Local Device Flash
    const localHost = document.getElementById('sysHost').value.toLowerCase();
    let localFile = files[0];
    for (let f of files) {
      const fName = f.name.toLowerCase();
      if (fName.includes(localHost)) {
        localFile = f;
        break;
      }
    }
    
    const localContainer = document.createElement('div');
    localContainer.innerHTML = `Local (${localFile.name}): <span id="p_local">0%</span>`;
    statusDiv.appendChild(localContainer);
    
    const pLocal = new Promise((resolve) => {
      uploadWithProgress('/api/ota', localFile, { 'X-Auth': H },
        function(pct) {
          document.getElementById('p_local').innerText = pct + '%';
        },
        function(res) {
          document.getElementById('p_local').innerText = 'OK';
          resolve({ name: `Local (${localFile.name})`, status: 'OK' });
        },
        function(err) {
          document.getElementById('p_local').innerText = 'Error: ' + err.message;
          resolve({ name: `Local (${localFile.name})`, status: 'Failed' });
        }
      );
    });
    promises.push(pLocal);
    
    // 2. Remote Devices Flash
    devices.forEach(d => {
      if (d.ip === window.location.hostname || d.ip === window.location.host || d.name === document.getElementById('sysHost').value) return;
      
      let matchedFile = files[0];
      for (let f of files) {
        const fName = f.name.toLowerCase();
        if (fName.includes(d.name.toLowerCase())) {
          matchedFile = f;
          break;
        }
      }
      
      const remoteContainer = document.createElement('div');
      remoteContainer.innerHTML = `${d.name} (${matchedFile.name}): <span id="p_${d.name}">0%</span>`;
      statusDiv.appendChild(remoteContainer);
      
      const pRemote = new Promise((resolve) => {
        uploadWithProgress(`http://${d.ip}/api/ota`, matchedFile, { 'X-Auth': remoteHash },
          function(pct) {
            const el = document.getElementById(`p_${d.name}`);
            if (el) el.innerText = pct + '%';
          },
          function(res) {
            const el = document.getElementById(`p_${d.name}`);
            if (el) el.innerText = 'OK';
            resolve({ name: `${d.name} (${matchedFile.name})`, status: 'OK' });
          },
          function(err) {
            const el = document.getElementById(`p_${d.name}`);
            if (el) el.innerText = 'Error: ' + err.message;
            resolve({ name: `${d.name} (${matchedFile.name})`, status: 'Failed' });
          }
        );
      });
      promises.push(pRemote);
    });
    
    Promise.all(promises).then(results => {
      let msg = L === 'ca' ? 'Resultat del desplegament:\n' : 'Resultado del despliegue:\n';
      results.forEach(res => {
        msg += `- ${res.name}: ${res.status}\n`;
      });
      alert(msg);
      
      const localRes = results.find(r => r.name.startsWith('Local'));
      if (localRes && localRes.status === 'OK') {
        setTimeout(() => location.reload(), 4000);
      }
    });
  })
  .catch(err => {
    alert('Error: ' + err.message);
    statusDiv.innerText = '';
  });
}

function uo(){
  const f=document.getElementById('ofile').files[0];
  if(!f){alert('Select file');return;}
  if(!confirm('¿Flash '+f.name+'?'))return;
  
  const statusDiv = document.getElementById('os');
  const barBg = document.getElementById('pBarBg');
  const bar = document.getElementById('pBar');
  
  statusDiv.innerText = L === 'ca' ? 'Pujant... 0%' : 'Subiendo... 0%';
  barBg.style.display = 'block';
  bar.style.width = '0%';
  bar.style.background = 'var(--acc)';
  
  uploadWithProgress('/api/ota', f, { 'X-Auth': H }, 
    function(pct) {
      statusDiv.innerText = (L === 'ca' ? 'Pujant... ' : 'Subiendo... ') + pct + '%';
      bar.style.width = pct + '%';
    },
    function(response) {
      statusDiv.innerText = L === 'ca' ? 'Pujada completa! Reiniciant...' : '¡Subida completa! Reiniciando...';
      bar.style.width = '100%';
      alert(L === 'ca' ? 'Actualització correcta! Reiniciant...' : '¡Actualización correcta! Reiniciando...');
      setTimeout(() => location.reload(), 4000);
    },
    function(err) {
      statusDiv.innerText = L === 'ca' ? 'Error en la pujada' : 'Error en la subida';
      bar.style.background = 'var(--err)';
      alert(L === 'ca' ? 'Error al flashejar: ' + err.message : 'Error al flashechar: ' + err.message);
    }
  );
}
</script></body></html>)html");
}

static void printEscapedJSON(WiFiClient& client, const char* src) {
  for (int i = 0; src[i] != '\0'; i++) {
    char c = src[i];
    if (c == '\\') client.print("\\\\");
    else if (c == '"') client.print("\\\"");
    else if (c == '\n') client.print("\\n");
    else if (c == '\r') client.print("\\r");
    else if (c == '\t') client.print("\\t");
    else if ((unsigned char)c >= 32) client.print(c);
  }
}

void webServerInit() {
  const char* apName = "SONA_SETUP";
  const char* apPass = "sonasetup";

  // Determinar SSID activo (EEPROM o hardcoded en globals.cpp)
  const char* activeSSID = (strlen(systemConfig.wifiSSID) > 0) ? systemConfig.wifiSSID : ssid;
  const char* activePass = (strlen(systemConfig.wifiSSID) > 0) ? systemConfig.wifiPassword : password;

  // Si hay algún SSID configurado (EEPROM o firmware) y no está vacío
  if (strlen(activeSSID) > 0) {
    addLog("WIFI", "Intentando conectar a red WiFi...");
    WiFi.begin(activeSSID, activePass);
    
    // Esperar hasta 20 segundos para conectar
    int to = 0;
    while (WiFi.status() != WL_CONNECTED && to < 20) { 
      delay(1000); 
      to++; 
      Serial.print("Intentando... ");
      handleRFID(); // Mantener el servicio esencial vivo
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      addLog("WIFI", "WiFi conectado, esperando IP...");
      
      int waitIp = 0;
      while (WiFi.localIP() == IPAddress(0,0,0,0) && waitIp < 10) {
        delay(500);
        waitIp++;
        handleRFID();
      }

      if (WiFi.localIP() != IPAddress(0,0,0,0)) {
        server.begin();
        udp.begin(udpPort);
        isAPMode = false;
        addLog("WIFI", "¡Conectado a WiFi y listo!");
        addLog("WIFI", "IP: " + WiFi.localIP().toString());
        ntpSync();
        return; // Conexión WiFi exitosa, no iniciamos AP
      }
    }
    addLog("WIFI", "No se pudo conectar a la red tras 20 segundos.");
  } else {
    addLog("WIFI", "Sin configuracion WiFi valida en EEPROM ni en firmware.");
  }

  // 2. Si no hay configuración o no se pudo conectar, levantamos el Punto de Acceso (AP)
  addLog("AP", "Iniciando Punto de Acceso...");
  int apStatus = WiFi.beginAP(apName, apPass);
  if (apStatus == WL_AP_LISTENING) {
    server.begin();
    isAPMode = true;
    addLog("AP", "Punto de Acceso iniciado: SONA_SETUP");
    addLog("AP", "Conectate con la clave 'sonasetup' e ingresa a http://192.168.4.1");
  } else {
    isAPMode = false;
    addLog("ERROR", "Fallo al iniciar el Punto de Acceso.");
  }
}

void handleWebAdmin() {
  if (!isAPMode && WiFi.status() != WL_CONNECTED) return;

  WiFiClient client = server.available();
  if (!client) return;
  
  String allHeaders = ""; 
  String body = "";
  unsigned long t0 = millis();
  
  while (client.connected() && (millis()-t0 < 2000)) {
    if (client.available()) {
      char c = client.read(); 
      if (allHeaders.length() < 1000) {
        allHeaders += c;
      } else {
        client.stop();
        return;
      }
      if (allHeaders.endsWith("\r\n\r\n")) break;
    }
  }

  int lineEnd = allHeaders.indexOf("\r\n");
  String reqLine = allHeaders.substring(0, (lineEnd == -1) ? allHeaders.length() : lineEnd);

  if (reqLine.indexOf("OPTIONS ") != -1) {
    sp(client, "HTTP/1.1 204 No Content\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
               "Access-Control-Allow-Headers: Content-Type, X-Auth\r\n"
               "Access-Control-Max-Age: 86400\r\n"
               "Connection: keep-alive\r\n\r\n");
    client.stop();
    return;
  }

  // Solo leer el cuerpo si es un POST y NO es OTA ni Restore (que se leen mediante streaming directo en sus handlers)
  if (reqLine.indexOf("POST") != -1 && 
      reqLine.indexOf("/api/ota") == -1 && 
      reqLine.indexOf("/api/restore") == -1) {
    String clStr = getHeaderValue(allHeaders, "Content-Length");
    if (clStr.length() > 0) {
      int cl = clStr.toInt();
      if (cl > 2048) cl = 2048; // Limitar tamaño de lectura del cuerpo para evitar fragmentar/agotar SRAM
      for (int i = 0; i < cl && client.connected(); i++) {
        unsigned long readStart = millis();
        while (!client.available() && millis() - readStart < 3000) {
          delay(1);
        }
        if (client.available()) body += (char)client.read();
      }
    }
  }

  bool auth = false;
  String h = getHeaderValue(allHeaders, "X-Auth");
  if (h.length() > 0) {
    if (h == String(cfgPwd.adminHash)) auth = true;
  }

  if (reqLine.indexOf("GET / ") != -1 || reqLine.indexOf("GET /index.html") != -1) {
    if (isAPMode) {
      enviarConfigWiFiHTML(client);
    } else {
      enviarPanelHTML(client);
    }
  } 
  else if (reqLine.indexOf("/api/") != -1) {
    if (!auth && !(isAPMode && reqLine.indexOf("POST /api/sysconfig") != -1) && reqLine.indexOf("/api/buzzer") == -1) { 
      enviar401(client); 
    } else {
      if (reqLine.indexOf("/api/buzzer") != -1) {
        sp(client, "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 2\r\n"
                   "Access-Control-Allow-Origin: *\r\n\r\n"
                   "OK");
        client.stop(); // Cerrar el socket inmediatamente para liberar al cliente HTTP
        for (int i = 0; i < 4; i++) {
          tone(BUZZER_PIN, 1000, 200);
          delay(300);
          tone(BUZZER_PIN, 1300, 200);
          delay(300);
        }
      }
      else if (reqLine.indexOf("GET /api/discover") != -1) {
        udp.beginPacket(IPAddress(255, 255, 255, 255), udpPort);
        udp.print("SONA_DISCOVER");
        udp.endPacket();
        
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n[");
        
        bool first = true;
        unsigned long startScan = millis();
        while (millis() - startScan < 200) {
          int cb = udp.parsePacket();
          if (cb) {
            int len = udp.read(packetBuffer, 255);
            if (len > 0) {
              packetBuffer[len] = 0;
              if (packetBuffer[0] == '{') {
                if (!first) client.print(",");
                client.print(packetBuffer);
                first = false;
              }
            }
          }
          delay(10);
        }
        client.print("]");
      }
      else if (reqLine.indexOf("GET /api/log") != -1) {
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
        client.print("{\"log\":");
        printLogJSON(client);
        client.print(",\"uptime\":"); client.print(millis()/1000);
        client.print(",\"rssi\":"); client.print(WiFi.RSSI());
        client.print(",\"heap\":"); client.print(getFreeHeap());
        client.print(",\"accesos\":"); client.print(totalAccesos);
        client.print(",\"lastUID\":\""); client.print(ultimoTagUID);
        client.print("\",\"lastNombre\":\""); printEscapedJSON(client, ultimoTagNombre.c_str());
        client.print("\",\"lastTs\":"); client.print(ultimoTagTs);
        client.print(",\"lastPermitido\":"); client.print(ultimoTagPermitido ? "true" : "false");
        client.print(",\"sysSsid\":\""); printEscapedJSON(client, systemConfig.wifiSSID);
        client.print("\",\"sysPass\":\""); printEscapedJSON(client, systemConfig.wifiPassword);
        client.print("\",\"sysHost\":\""); printEscapedJSON(client, systemConfig.hostname);
        client.print("\",\"sysNtp\":\""); printEscapedJSON(client, systemConfig.ntpServer);
        client.print("\",\"sysOffset\":"); client.print(systemConfig.utcOffset);
        client.print(",\"sysQuietEn\":"); client.print(systemConfig.quietEnabled ? "true" : "false");
        client.print(",\"sysQStart\":"); client.print(systemConfig.quietStart);
        client.print(",\"sysQEnd\":"); client.print(systemConfig.quietEnd);
        client.print(",\"sysClass\":\""); printEscapedJSON(client, systemConfig.classRoom);
        client.print("\",\"sysClassNum\":\""); printEscapedJSON(client, systemConfig.classNum);
        client.print("\",\"lat\":"); client.print(systemConfig.latitude, 8);
        client.print(",\"lon\":"); client.print(systemConfig.longitude, 8);
        client.print("}");
      } 
      else if (reqLine.indexOf("GET /api/hist") != -1) {
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n[");
        for (int i = 0; i < MAX_HIST; i++) {
          if (i>0) client.print(",");
          client.print("{\"uid\":\""); client.print(historial[i].uid);
          client.print("\",\"nombre\":\""); printEscapedJSON(client, historial[i].nombre);
          client.print("\",\"ts\":"); client.print(historial[i].timestamp);
          client.print("}");
        }
        client.print("]");
      }
      else if (reqLine.indexOf("GET /api/aulas") != -1) {
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n[");
        for (int i = 0; i < MAX_AULAS; i++) {
          if (i > 0) client.print(",");
          client.print("{\"idx\":"); client.print(i);
          client.print(",\"uid\":\""); client.print(baseDatos[i].uid);
          client.print("\",\"nombre\":\""); printEscapedJSON(client, baseDatos[i].nombre);
          client.print("\",\"pat\":"); client.print(baseDatos[i].patronID);
          client.print(",\"accesos\":"); client.print(baseDatos[i].accesos);
          client.print(",\"sh\":"); client.print(baseDatos[i].startHour);
          client.print(",\"sm\":"); client.print(baseDatos[i].startMin);
          client.print(",\"eh\":"); client.print(baseDatos[i].endHour);
          client.print(",\"em\":"); client.print(baseDatos[i].endMin);
          client.print(",\"days\":"); client.print(baseDatos[i].days);
          client.print("}");
        }
        client.print("]");
      }
      else if (reqLine.indexOf("POST /api/update") != -1) {
        procesarUpdate(body);
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"ok\":true}");
      }
      else if (reqLine.indexOf("POST /api/pwd") != -1) {
        String np = getParamDecoded(body, "pwd");
        if (np.length() >= 4) {
          crc32hex(np).toCharArray(cfgPwd.adminHash, 9);
          cfgPwd.signature = 0x5057;
          eepromSavePwd();
          sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"ok\":true}");
        } else sp(client, "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
      }
      else if (reqLine.indexOf("POST /api/sysconfig") != -1) {
        if (hasParam(body, "ssid")) {
          String ssidParam = getParamDecoded(body, "ssid");
          ssidParam.toCharArray(systemConfig.wifiSSID, 32);
        }
        if (hasParam(body, "pass")) {
          String passParam = getParamDecoded(body, "pass");
          passParam.toCharArray(systemConfig.wifiPassword, 64);
        }
        if (hasParam(body, "host")) {
          String hostParam = getParamDecoded(body, "host");
          hostParam.toCharArray(systemConfig.hostname, 24);
        }
        if (hasParam(body, "ntp")) {
          String ntpParam = getParamDecoded(body, "ntp");
          ntpParam.toCharArray(systemConfig.ntpServer, 40);
        }
        if (hasParam(body, "offset")) {
          systemConfig.utcOffset = getParam(body, "offset").toInt();
        }
        if (hasParam(body, "quietEn")) {
          systemConfig.quietEnabled = (getParam(body, "quietEn") == "true");
        }
        if (hasParam(body, "qStart")) {
          systemConfig.quietStart = getParam(body, "qStart").toInt();
        }
        if (hasParam(body, "qEnd")) {
          systemConfig.quietEnd = getParam(body, "qEnd").toInt();
        }
        if (hasParam(body, "class")) {
          String classParam = getParamDecoded(body, "class");
          classParam.toCharArray(systemConfig.classRoom, 32);
        }
        if (hasParam(body, "classNum")) {
          String classNumParam = getParamDecoded(body, "classNum");
          classNumParam.toCharArray(systemConfig.classNum, 8);
        }
        if (hasParam(body, "lat")) {
          systemConfig.latitude = getParam(body, "lat").toDouble();
        }
        if (hasParam(body, "lon")) {
          systemConfig.longitude = getParam(body, "lon").toDouble();
        }

        eepromSaveConfig();
        addLog("ADMIN", "Configuracion de sistema actualizada. Reiniciando...");
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"ok\":true}");
        
        client.stop();
        delay(1000);
        NVIC_SystemReset();
        return;
      }
      else if (reqLine.indexOf("POST /api/eepromreset") != -1) {
        eepromResetAll();
        addLog("ADMIN", "EEPROM reseteada por peticion web. Reiniciando...", LOG_WARN);
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"ok\":true}");
        client.stop();
        delay(1000);
        NVIC_SystemReset();
        return;
      }
      else if (reqLine.indexOf("POST /api/ota") != -1) {
        handleOTA(client, allHeaders);
      }
      else if (reqLine.indexOf("GET /api/backup") != -1) {
        addLog("ADMIN", "Exportando BD binaria...");
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
        client.write((uint8_t*)baseDatos, sizeof(baseDatos));
      }
      else if (reqLine.indexOf("POST /api/restore") != -1) {
        String clStr = getHeaderValue(allHeaders, "Content-Length");
        if (clStr.length() > 0) {
          long cl = clStr.toInt();
          if (cl == sizeof(baseDatos)) {
            long count = 0;
            while (count < cl && client.connected()) {
              if (client.available()) {
                ((uint8_t*)baseDatos)[count++] = client.read();
              }
            }
            if (count == cl) {
              eepromSaveAulas();
              addLog("SISTEMA", "RESTORE Óptimo. Datos recargados.");
              sp(client, "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/plain\r\n\r\nOK");
            } else sp(client, "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/plain\r\n\r\nFallo en transferencia");
          } else sp(client, "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/plain\r\n\r\nSize mismatch");
        }
      }
    }
  } else {
    enviarPanelHTML(client);
  }
  
  client.stop();
}

void handleDiscovery() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    if (String(packetBuffer) == "SONA_DISCOVER") {
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      // Incluir heap y rssi en la respuesta UDP para evitar falsas alertas en la app movil
      udp.print("{\"name\":\"" + String(systemConfig.hostname) + 
                "\",\"ip\":\"" + WiFi.localIP().toString() + 
                "\",\"class\":\"" + escapeJSON(systemConfig.classRoom) + 
                "\",\"classNum\":\"" + escapeJSON(systemConfig.classNum) + 
                "\",\"ver\":\"" VERSION "\",\"heap\":" + String(getFreeHeap()) + 
                ",\"rssi\":" + String(WiFi.RSSI()) + 
                ",\"lat\":" + String(systemConfig.latitude, 8) + 
                ",\"lon\":" + String(systemConfig.longitude, 8) + 
                ",\"lastUID\":\"" + ultimoTagUID + 
                "\",\"lastNombre\":\"" + escapeJSON(ultimoTagNombre) + 
                "\",\"lastTs\":" + String(ultimoTagTs) + 
                ",\"lastPermitido\":" + String(ultimoTagPermitido ? "true" : "false") + "}");
      udp.endPacket();
    }
  }
}

void wifiWatchdog() {
  if (isAPMode) return;

  if (millis() - wifiCheck < WIFI_CHECK_INTERVAL) return;
  wifiCheck = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    addLog("SISTEMA", "Conexión perdida. Reiniciando WiFi stack...");
    WiFi.disconnect();
    
    const char* activeSSID = (strlen(systemConfig.wifiSSID) > 0) ? systemConfig.wifiSSID : ssid;
    const char* activePass = (strlen(systemConfig.wifiSSID) > 0) ? systemConfig.wifiPassword : password;
    WiFi.begin(activeSSID, activePass);
    
    // Esperar un momento sin bloquear el loop totalmente
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      delay(100);
      handleRFID(); // Mantener el servicio esencial vivo mientras reconecta
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      server.begin();
      udp.begin(udpPort);
      addLog("SISTEMA", "Reconexión exitosa.");
      ntpSync();
    }
  }
}
