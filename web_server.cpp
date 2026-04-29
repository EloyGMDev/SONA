#include "web_server.h"
#include "config.h"
#include "utils.h"
#include "database.h"
#include "hardware_io.h"
#include <RTC.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <malloc.h>

extern "C" char* sbrk(int incr);
int getFreeHeap() {
  struct mallinfo mi = mallinfo();
  return mi.fordblks;
}

WiFiServer server(80);
WiFiUDP udp;
unsigned int udpPort = 42800;
char packetBuffer[255]; 

static void sp(WiFiClient& c, const char* s) { c.print(s); }
static void sp(WiFiClient& c, const __FlashStringHelper* s) { c.print(s); }
static void sp(WiFiClient& c, const String& s) { c.print(s); }

String getParam(String data, String param) {
  int pos = data.indexOf(param+"="); 
  if (pos==-1) return "";
  int start = pos+param.length()+1;
  int end = data.indexOf('&',start); 
  if (end==-1) end=data.length();
  return data.substring(start,end);
}

void procesarUpdate(String data) {
  int idx = getParam(data,"idx").toInt();
  String uid = getParam(data,"uid");
  String nom = getParam(data,"nom");
  int pat = getParam(data,"pat").toInt();
  if (idx >= 0 && idx < MAX_AULAS) {
    uid.toUpperCase(); 
    nom.replace("+"," ");
    uid.toCharArray(baseDatos[idx].uid, 15);
    nom.toCharArray(baseDatos[idx].nombre, 20);
    baseDatos[idx].patronID = constrain(pat,0,10);
    baseDatos[idx].startHour = getParam(data,"sh").toInt();
    baseDatos[idx].startMin  = getParam(data,"sm").toInt();
    baseDatos[idx].endHour   = getParam(data,"eh").toInt();
    baseDatos[idx].endMin    = getParam(data,"em").toInt();
    eepromSaveAulas();
    addLog("ADMIN","Slot "+String(idx)+" actualizado (Horario incl.)");
  }
}

// Handler para OTA (streaming directo a flash)
void handleOTA(WiFiClient& client, String allHeaders) {
  // Validar Content-Length
  int clIdx = allHeaders.indexOf("Content-Length: ");
  if (clIdx == -1) { enviar401(client); return; }
  long cl = allHeaders.substring(clIdx + 16).toInt();

  addLog("OTA", "Iniciando descarga (" + String(cl/1024) + " KB)...");

  if (!InternalStorage.open(cl)) {
    addLog("ERROR", "OTA: No hay espacio en flash");
    sp(client, F("HTTP/1.1 500 Error\r\n\r\nError Flash"));
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
    sp(client, F("HTTP/1.1 200 OK\r\n\r\nOK"));
    delay(1000);
    InternalStorage.apply(); 
  } else {
    addLog("ERROR", "OTA falló: Conexión perdida");
    sp(client, F("HTTP/1.1 500 Error\r\n\r\nFallo de conexión"));
  }
}

void enviar401(WiFiClient& client) {
  sp(client, F("HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n\r\nAcceso denegado"));
}

void enviarPanelHTML(WiFiClient& client) {
  sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"));
  sp(client, F("<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  sp(client, F("<title>SONA - Centro de Control</title><link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600&display=swap' rel='stylesheet'>"));
  sp(client, F("<style>"));
  sp(client, F(":root{--bg:#050505;--panel:#111111;--acc:#00ffa3;--acc-glow:rgba(0,255,163,0.3);--err:#ff4b2b;--txt:#ffffff;--dim:#888;--transition:all 0.3s cubic-bezier(0.4,0,0.2,1);}"));
  sp(client, F("*{box-sizing:border-box;margin:0;padding:0;}"));
  sp(client, F("body{background:var(--bg);color:var(--txt);font-family:'Outfit',sans-serif;overflow-x:hidden;-webkit-font-smoothing:antialiased;}"));
  sp(client, F(".glass{background:rgba(255,255,255,0.03);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.05);border-radius:16px;}"));
  
  sp(client, F(".navbar{padding:20px 40px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid rgba(255,255,255,0.1);background:rgba(0,0,0,0.8);position:sticky;top:0;z-index:100;backdrop-filter:blur(10px);}"));
  sp(client, F(".logo{font-size:20px;font-weight:600;letter-spacing:-0.5px;background:linear-gradient(90deg,#00ffa3,#00c8ff);-webkit-background-clip:text;-webkit-text-fill-color:transparent;}"));
  sp(client, F(".stats{display:flex;gap:20px;font-size:12px;color:var(--dim);}"));
  sp(client, F(".stat-item b{color:#fff;}"));

  sp(client, F(".main{display:grid;grid-template-columns:1fr 340px;gap:24px;padding:30px;max-width:1400px;margin:0 auto;}"));
  sp(client, F("@media(max-width:900px){.main{grid-template-columns:1fr;padding:15px;}}"));

  sp(client, F(".card{padding:24px;margin-bottom:24px;transition:var(--transition);}"));
  sp(client, F(".card:hover{border-color:rgba(0,255,163,0.2);box-shadow:0 10px 30px rgba(0,0,0,0.5);}"));
  sp(client, F(".card-title{font-size:14px;font-weight:600;color:var(--acc);margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;text-transform:uppercase;letter-spacing:1px;}"));

  sp(client, F(".terminal{height:250px;overflow-y:auto;background:#000;border-radius:12px;padding:15px;font-family:'Courier New',monospace;font-size:12px;line-height:1.6;border:1px solid #222;}"));
  sp(client, F(".log-line{margin-bottom:4px;animation:fadeIn 0.3s ease;opacity:0.8;}.log-line:hover{opacity:1;color:var(--acc);}"));
  sp(client, F(".ll0{color:#888}.ll1{color:#f0a500}.ll2{color:#ff4b2b}"));
  sp(client, F("@keyframes fadeIn{from{opacity:0;transform:translateY(5px);}to{opacity:0.8;transform:translateY(0);}}"));

  sp(client, F("table{width:100%;border-collapse:separate;border-spacing:0 8px;}"));
  sp(client, F("th{padding:12px;text-align:left;font-size:11px;color:var(--dim);text-transform:uppercase;}"));
  sp(client, F("td{padding:14px 12px;background:rgba(255,255,255,0.02);border-top:1px solid rgba(255,255,255,0.05);border-bottom:1px solid rgba(255,255,255,0.05);}"));
  sp(client, F("td:first-child{border-left:1px solid rgba(255,255,255,0.05);border-radius:8px 0 0 8px;}"));
  sp(client, F("td:last-child{border-right:1px solid rgba(255,255,255,0.05);border-radius:0 8px 8px 0;}"));

  sp(client, F("input,select{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);padding:12px;border-radius:8px;color:#fff;width:100%;transition:var(--transition);outline:none;}"));
  sp(client, F("input:focus{border-color:var(--acc);background:rgba(255,255,255,0.08);}"));
  sp(client, F("button{padding:12px 20px;border-radius:8px;border:none;font-weight:600;cursor:pointer;transition:var(--transition);font-family:'Outfit',sans-serif;}"));
  sp(client, F(".btn-acc{background:var(--acc);color:#000;}.btn-acc:hover{transform:translateY(-2px);box-shadow:0 5px 15px var(--acc-glow);}"));
  sp(client, F(".btn-err{background:rgba(255,75,43,0.1);color:var(--err);border:1px solid var(--err);}.btn-err:hover{background:var(--err);color:#fff;}"));

  sp(client, F(".switch{position:relative;display:inline-block;width:34px;height:20px;}"));
  sp(client, F(".switch input{opacity:0;width:0;height:0;}"));
  sp(client, F(".slider{position:absolute;cursor:pointer;inset:0;background-color:#333;transition:.4s;border-radius:20px;}"));
  sp(client, F(".slider:before{position:absolute;content:'';height:14px;width:14px;left:3px;bottom:3px;background-color:white;transition:.4s;border-radius:50%;}"));
  sp(client, F("input:checked + .slider{background-color:var(--acc);}"));
  sp(client, F("input:checked + .slider:before{transform:translateX(14px);}"));

  sp(client, F("</style></head><body>"));

  sp(client, F("<div class='navbar'><div class='logo'>"));
  sp(client, systemConfig.deviceName);
  sp(client, F("</div><div class='stats'>"));
  sp(client, F("<div class='stat-item'>TIEMPO ACTIVO: <b id='sup'>--s</b></div><div class='stat-item'>SE&ntilde;AL WIFI: <b id='srs'>--dBm</b></div>"));
  sp(client, F("</div></div>"));

  // Auth Overlay
  sp(client, F("<div id='ao' style='position:fixed;inset:0;background:var(--bg);z-index:999;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:24px;transition:var(--transition);'>"));
  sp(client, F("<div class='logo' style='font-size:32px'>ACCESO RESTRINGIDO</div>"));
  sp(client, F("<div class='glass' style='padding:40px;width:320px;display:grid;gap:20px;'>"));
  sp(client, F("<input id='pi' type='password' style='text-align:center' placeholder='Clave de Administrador'><button class='btn-acc' onclick='login()'>AUTENTICAR</button>"));
  sp(client, F("<div id='ae' style='color:var(--err);text-align:center;font-size:12px;'></div></div></div>"));

  sp(client, F("<div class='main' id='app' style='display:none; opacity:0; transition:opacity 0.8s ease;'>"));
  
  // Left Column
  sp(client, F("<div><div class='card glass'><div class='card-title'><span>Actividad en Tiempo Real</span>"));
  sp(client, F("<div><span style='font-size:10px;margin-right:8px;color:var(--dim)'>Silenciar</span><label class='switch'><input type='checkbox' id='lm'><span class='slider'></span></label></div></div>"));
  sp(client, F("<div class='terminal' id='la'></div></div>"));
  
  sp(client, F("<div class='card glass'><div class='card-title'>Gestor de Base de Datos</div><div style='overflow-x:auto'><table>"));
  sp(client, F("<thead><tr><th>#</th><th>UID RFID</th><th>Nombre / Ubicacion</th><th>Sonido</th><th>Horario</th><th>Uso</th><th></th></tr></thead><tbody id='ab'></tbody></table></div>"));
  sp(client, F("<p style='font-size:10px;color:var(--dim);margin-top:10px;'>* Cambios activos al salvar.</p></div>"));
  
  sp(client, F("<div class='card glass'><div class='card-title'>Gesti&oacute;n de Red (SONA Mesh)</div>"));
  sp(client, F("<div id='mn' style='display:grid;gap:10px;'>"));
  sp(client, F("<p style='font-size:11px;color:var(--dim);'>Buscando otras antenas en la red...</p></div>"));
  sp(client, F("<button class='btn-acc' style='margin-top:15px;width:100%' onclick='sy()'>SINCRONIZAR TODA LA RED</button></div></div></div>"));

  // Right Column
  sp(client, F("<div><div class='card glass'><div class='card-title'>Editor de Registro</div><div style='display:grid;gap:15px;'>"));
  sp(client, F("<div><label style='font-size:10px;color:var(--dim)'>INDICE / UID / NOMBRE</label><div style='display:flex;gap:5px;'><input id='fi' type='number' placeholder='Slot' style='width:70px'><input id='fu' placeholder='UID RFID'><input id='fn' placeholder='Ej: Aula 202'></div></div>"));
  sp(client, F("<div><label style='font-size:10px;color:var(--dim)'>HORARIO DE ACCESO (DE - A)</label><div style='display:flex;gap:5px;align-items:center;'><input id='fsh' type='number' placeholder='HH'><input id='fsm' type='number' placeholder='MM'><span>a</span><input id='feh' type='number' placeholder='HH'><input id='fem' type='number' placeholder='MM'></div></div>"));
  sp(client, F("<div><label style='font-size:10px;color:var(--dim)'>PATRON SONORO (EARCON)</label><div style='display:flex;gap:5px;'><select id='fp'><option value='1'>Exito Estandar</option><option value='2'>Doble Beep (Corto)</option><option value='3'>Triple Rapido (Aviso)</option><option value='4'>Melodia Ascendente</option><option value='5'>Melodia Suave</option><option value='8'>Fanfarria Victoria</option><option value='10'>VIP Especial</option></select><button class='btn-acc' style='padding:5px 15px;' onclick='te()'>PROBAR</button></div></div>"));
  sp(client, F("<button class='btn-acc' style='margin-top:10px' onclick='save()'>APLICAR CAMBIOS</button></div></div>"));

  sp(client, F("<div class='card glass'><div class='card-title'>Seguridad de Acceso</div><div style='display:grid;gap:12px;'>"));
  sp(client, F("<label style='font-size:10px;color:var(--dim)'>NUEVA CLAVE MAESTRA</label><div style='display:flex;gap:10px;'><input id='np' type='password'><button class='btn-err' onclick='chpwd()'>ACTUALIZAR</button></div></div></div>"));
  
  sp(client, F("<div class='card glass'><div class='card-title'>Identidad del Sistema</div>"));
  sp(client, F("<div style='display:grid;gap:12px;'>"));
  sp(client, F("<div><label style='font-size:10px;color:var(--dim)'>IDENTIFICADOR DE ESTA ANTENA (Ej: Recepcion / Pasillo 1)</label>"));
  sp(client, F("<div style='display:flex;gap:10px;margin-top:5px;'><input id='dn' placeholder='Nombre del receptor'><button class='btn-acc' onclick='sd()'>ACTUALIZAR</button></div></div>"));
  sp(client, F("<p style='font-size:11px;color:var(--dim)'>mDNS: sona.local | Version: "));
  sp(client, FW_VERSION);
  sp(client, F("</p>"));
  sp(client, F("<div style='margin-top:20px;padding-top:20px;border-top:1px solid rgba(255,255,255,0.05);'>"));
  sp(client, F("<label style='font-size:10px;color:var(--dim);display:block;margin-bottom:10px;'>ACTUALIZACION DE FIRMWARE (OTA)</label>"));
  sp(client, F("<input type='file' id='ofile' style='font-size:11px;margin-bottom:10px;'>"));
  sp(client, F("<button onclick='uo()' class='btn-err' style='width:100%'>SUBIR NUEVO BINARIO</button>"));
  sp(client, F("<div id='os' style='font-size:10px;margin-top:5px;color:var(--acc)'></div></div>"));
  sp(client, F("</div></div></div>"));

  sp(client, F("</div><script>"));
  sp(client, F("function crc32(s){var t=[];for(var i=0;i<256;i++){var c=i;for(var j=0;j<8;j++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[i]=c;}var r=0xFFFFFFFF;for(var i=0;i<s.length;i++)r=t[(r^s.charCodeAt(i))&0xFF]^(r>>>8);return(~r>>>0).toString(16).toUpperCase().padStart(8,'0');}"));
  sp(client, F("var H=localStorage.getItem('esp_h')||'';var _aulas=[];if(H)checkAuth();"));
  sp(client, F("function login(){H=crc32(document.getElementById('pi').value);checkAuth();}"));
  sp(client, F("function checkAuth(){fetch('/api/log',{headers:{'X-Auth':H}}).then(r=>{if(r.ok){localStorage.setItem('esp_h',H);document.getElementById('ao').style.transform='translateY(-100%)';setTimeout(()=>{document.getElementById('ao').style.display='none';document.getElementById('app').style.display='grid';setTimeout(()=>document.getElementById('app').style.opacity='1',50);},400);start();}else{document.getElementById('ae').innerText='Clave incorrecta';localStorage.removeItem('esp_h');}}).catch(e=>console.error(e));}"));
  sp(client, F("function api(p,o={}){o.headers=Object.assign({'X-Auth':H},o.headers||{});return fetch(p,o);}"));
  sp(client, F("function start(){loadLog();loadAulas();ln();setInterval(loadLog,3000);setInterval(ln,10000);}"));
  sp(client, F("function loadLog(){if(document.getElementById('lm').checked)return;api('/api/log').then(r=>r.json()).then(d=>{var s=parseInt(d.uptime);var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;document.getElementById('sup').innerText=(h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(sc<10?'0':'')+sc;document.getElementById('srs').innerText=d.rssi+'dBm';var lstr='';d.log.forEach(function(l){var lv=l.l<2?l.l:2;lstr+='<div class=ll'+lv+'>'+l.t+'</div>';});document.getElementById('la').innerHTML=lstr;document.getElementById('la').scrollTop=9999;});}"));
  sp(client, F("function ln(){api('/api/mesh/nodes').then(r=>r.json()).then(ns=>{var h='';if(ns.length==0)h='<p style=\"font-size:11px;color:var(--dim)\">No hay otras antenas activas.</p>';ns.forEach(n=>{h+='<div style=\"display:flex;justify-content:space-between;align-items:center;padding:12px;background:rgba(255,255,255,0.03);border-radius:8px;margin-bottom:8px;border:1px solid rgba(255,255,255,0.05);\"><div><b style=\"color:var(--acc)\">'+n.name+'</b><br><small style=\"color:var(--dim)\">'+n.ip+'</small></div><div style=\"display:flex;gap:8px;\"><button onclick=\"rn(\\\''+n.ip+'\\\',\\\''+n.name+'\\\')\" class=\"btn-acc\" style=\"padding:4px 8px;font-size:10px\">RENOMBRAR</button><a href=\"http://'+n.ip+'\" target=\"_blank\" class=\"btn-acc\" style=\"padding:4px 8px;font-size:10px;text-decoration:none;display:inline-block;\">GESTIONAR</a></div></div>';});document.getElementById('mn').innerHTML=h;});}"));
  sp(client, F("function rn(ip,old){var n=prompt('Nuevo nombre para '+old,old);if(n&&n!=old){api('/api/mesh/rename',{method:'POST',body:'ip='+ip+'&name='+encodeURIComponent(n)}).then(r=>{if(r.ok)setTimeout(ln,1000);});}}"));
  sp(client, F("function sy(){if(!confirm('Enviar base de datos actual a TODA la red?'))return;api('/api/mesh/sync',{method:'POST'}).then(r=>{if(r.ok)alert('Sincronizacion enviada con exito.');});}"));
  sp(client, F("function loadAulas(){api('/api/aulas').then(r=>r.json()).then(function(a){_aulas=a;var h='';for(var i=0;i<a.length;i++){var x=a[i];h+='<tr><td>'+x.idx+'</td><td>'+x.uid+'</td>';h+='<td><b>'+x.nombre+'</b></td><td>P'+x.pat+'</td>';var t=x.sh+':'+(x.sm<10?'0':'')+x.sm+'-'+x.eh+':'+(x.em<10?'0':'')+x.em;h+='<td>'+t+'</td><td>'+x.accesos+'</td>';h+='<td><button onclick=\"es('+x.idx+')\" class=\"btn-acc\" style=\"padding:5px 10px;font-size:10px\">EDITAR</button></td></tr>';}document.getElementById('ab').innerHTML=h;});}"));
  sp(client, F("function es(idx){var x=_aulas[idx];if(!x)return;document.getElementById('fi').value=x.idx;document.getElementById('fu').value=x.uid;document.getElementById('fn').value=x.nombre;document.getElementById('fp').value=x.pat;document.getElementById('fsh').value=x.sh;document.getElementById('fsm').value=x.sm;document.getElementById('feh').value=x.eh;document.getElementById('fem').value=x.em;window.scrollTo({top:0,behavior:'smooth'});}"));
  sp(client, F("function save(){var btn=event.target;btn.disabled=true;btn.innerText='GUARDANDO...';var b=`idx=${document.getElementById('fi').value}&uid=${document.getElementById('fu').value}&nom=${encodeURIComponent(document.getElementById('fn').value)}&pat=${document.getElementById('fp').value}&sh=${document.getElementById('fsh').value}&sm=${document.getElementById('fsm').value}&eh=${document.getElementById('feh').value}&em=${document.getElementById('fem').value}`;api('/api/update',{method:'POST',body:b}).then(r=>{btn.disabled=false;btn.innerText='APLICAR CAMBIOS';if(r.ok)loadAulas();});}"));
  sp(client, F("function chpwd(){if(!confirm('Cambiar clave maestra?'))return;api('/api/pwd',{method:'POST',body:'pwd='+encodeURIComponent(document.getElementById('np').value)}).then(r=>{if(r.ok)location.reload();});}"));
  sp(client, F("function uo(){ const f=document.getElementById('ofile').files[0]; if(!f){alert('Selecciona un archivo');return;} if(!confirm('Flashear '+f.name+'?'))return; document.getElementById('os').innerText='Subiendo... 0%'; api('/api/ota',{method:'POST',body:f}).then(r=>{if(r.ok)alert('Actualizacion completada! Reiniciando...');else alert('Error en la actualizacion');}); }"));
  sp(client, F("function sd(){ api('/api/sys',{method:'POST',body:'name='+encodeURIComponent(document.getElementById('dn').value)}).then(r=>{if(r.ok)alert('Nombre actualizado en el sistema.');}); }"));
  sp(client, F("function te(){api('/api/test_earcon?id='+document.getElementById('fp').value);}"));
  sp(client, F("api('/api/sys').then(r=>r.json()).then(d=>{document.getElementById('dn').value=d.name;});"));
  sp(client, F("</script></body></html>"));
}

void webServerInit() {
  addLog("WIFI", "Iniciando stack WiFi...");
  WiFi.disconnect();
  WiFi.end(); 
  delay(1000);

  addLog("WIFI", "Conectando a: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int to = 0;
  while (WiFi.status() != WL_CONNECTED && to < 20) { delay(1000); to++; }
  
  if (WiFi.status() == WL_CONNECTED) {
    server.begin();
    udp.begin(udpPort);
    
    // Si el nombre es el de "fábrica" (sona), buscamos si hay colisión
    if (String(systemConfig.hostname) == "sona") {
      addLog("RED", "Sondeando nombres disponibles...");
      int suffix = 0;
      bool taken = true;
      char trialName[24];
      
      while(taken && suffix < 10) {
        if (suffix == 0) strcpy(trialName, "sona");
        else sprintf(trialName, "sona%d", suffix);
        
        // Enviar sonda UDP
        udp.beginPacket(IPAddress(255,255,255,255), udpPort);
        udp.print("SONA_DISCOVER");
        udp.endPacket();
        
        unsigned long start = millis();
        taken = false;
        while(millis() - start < 1500) {
          int ps = udp.parsePacket();
          if (ps) {
            int len = udp.read(packetBuffer, 255);
            packetBuffer[len] = 0;
            if (String(packetBuffer).indexOf("\"name\":\"" + String(trialName) + "\"") != -1) {
              taken = true;
              break;
            }
          }
        }
        if (taken) suffix++;
      }
      
      if (String(systemConfig.hostname) != String(trialName)) {
        strncpy(systemConfig.hostname, trialName, 24);
        addLog("RED", "Nombre asignado: " + String(trialName));
      }
    }

    addLog("WIFI", "¡Sistema listo!");
    addLog("WIFI", "IP: " + WiFi.localIP().toString());
  }
}

void handleWebAdmin() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (WiFi.localIP() == IPAddress(0,0,0,0)) return;

  WiFiClient client = server.available();
  if (!client) return;
  
  // Log eliminado para evitar inundación (flood)

  
  String allHeaders = ""; 
  String body = "";
  unsigned long t0 = millis();
  
  while (client.connected() && (millis()-t0 < 2000)) {
    if (client.available()) {
      char c = client.read(); 
      allHeaders += c;
      if (allHeaders.endsWith("\r\n\r\n")) break;
    }
  }
  // ... resto de la lógica ...

  
  if (allHeaders.indexOf("POST") != -1) {
    int clIdx = allHeaders.indexOf("Content-Length: ");
    if (clIdx != -1) {
      int clEnd = allHeaders.indexOf("\r\n", clIdx);
      int cl = allHeaders.substring(clIdx+16, clEnd).toInt();
      for (int i = 0; i < cl && client.connected(); i++) {
        while (!client.available() && millis()-t0 < 3000);
        if (client.available()) body += (char)client.read();
      }
    }
  }
  
  int lineEnd = allHeaders.indexOf("\r\n");
  String reqLine = allHeaders.substring(0, (lineEnd == -1) ? allHeaders.length() : lineEnd);

  bool auth = false;
  int authIdx = allHeaders.indexOf("X-Auth: ");
  if (authIdx != -1) {
    int authEnd = allHeaders.indexOf("\r\n", authIdx);
    String h = allHeaders.substring(authIdx+8, authEnd); 
    h.trim();
    if (h == String(cfgPwd.adminHash)) auth = true;
  }

  if (reqLine.indexOf("GET / ") != -1 || reqLine.indexOf("GET /index.html") != -1) {
    enviarPanelHTML(client);
  } 
  else if (reqLine.indexOf("/api/") != -1) {
    if (!auth) { 
      enviar401(client); 
    } else {
      if (reqLine.indexOf("GET /api/log") != -1) {
        String json = "{\"log\":" + getLogJSON()
          + ",\"uptime\":"  + String(millis()/1000)
          + ",\"rssi\":"    + String(WiFi.RSSI())
          + ",\"heap\":"    + String(getFreeHeap())
          + ",\"accesos\":" + String(totalAccesos)
          + ",\"lastUID\":\"" + ultimoTagUID + "\""
          + ",\"lastNombre\":\"" + ultimoTagNombre + "\""
          + ",\"lastTs\":"  + String(ultimoTagTs) + "}";
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n"));
        client.print(json);
      } 
      else if (reqLine.indexOf("GET /api/hist") != -1) {
        String j = "[";
        for (int i = 0; i < MAX_HIST; i++) {
          if (i>0) j += ",";
          j += "{\"uid\":\"" + String(historial[i].uid) + "\",\"nombre\":\"" + String(historial[i].nombre) + "\",\"ts\":" + String(historial[i].timestamp) + "}";
        }
        j += "]";
        sp(client, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
        client.print(j);
      }
      else if (reqLine.indexOf("GET /api/aulas") != -1) {
        String j = "[";
        for (int i = 0; i < MAX_AULAS; i++) {
          if (i>0) j += ",";
          j += "{\"idx\":" + String(i) + ",\"uid\":\"" + String(baseDatos[i].uid) + "\",\"nombre\":\"" + String(baseDatos[i].nombre) + "\",\"pat\":" + String(baseDatos[i].patronID) + ",\"accesos\":" + String(baseDatos[i].accesos) + ",\"sh\":" + String(baseDatos[i].startHour) + ",\"sm\":" + String(baseDatos[i].startMin) + ",\"eh\":" + String(baseDatos[i].endHour) + ",\"em\":" + String(baseDatos[i].endMin) + "}";
        }
        j += "]";
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"));
        client.print(j);
      }
      else if (reqLine.indexOf("GET /api/sys") != -1) {
        String j = "{\"name\":\"" + String(systemConfig.deviceName) + "\"}";
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"));
        client.print(j);
      }
      else if (reqLine.indexOf("POST /api/sys") != -1) {
        String n = getParam(body, "name");
        n.replace("+"," ");
        if (n.length() > 0) {
          n.toCharArray(systemConfig.deviceName, 24);
          eepromSaveConfig();
          sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true}"));
        } else sp(client, F("HTTP/1.1 400 Bad Request\r\n\r\n"));
      }
      else if (reqLine.indexOf("POST /api/update") != -1) {
        procesarUpdate(body);
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true}"));
      }
      else if (reqLine.indexOf("GET /api/test_earcon") != -1) {
        int id = reqLine.substring(reqLine.indexOf("id=")+3).toInt();
        earcon(id);
        sp(client, F("HTTP/1.1 200 OK\r\n\r\nOK"));
      }
      else if (reqLine.indexOf("POST /api/pwd") != -1) {
        String np = getParam(body, "pwd");
        if (np.length() >= 4) {
          crc32hex(np).toCharArray(cfgPwd.adminHash, 9);
          eepromSavePwd();
          sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true}"));
        } else sp(client, F("HTTP/1.1 400 Bad Request\r\n\r\n"));
      }
      else if (reqLine.indexOf("POST /api/ota") != -1) {
        handleOTA(client, allHeaders);
      }
      else if (reqLine.indexOf("GET /api/backup") != -1) {
        addLog("ADMIN", "Exportando BD binaria...");
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n"));
        client.write((uint8_t*)baseDatos, sizeof(baseDatos));
      }
      else if (reqLine.indexOf("GET /api/mesh/nodes") != -1) {
        // Escaneo silencioso en segundo plano
        udp.beginPacket(IPAddress(255,255,255,255), udpPort);
        udp.print("SONA_DISCOVER");
        udp.endPacket();
        
        sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n["));
        unsigned long start = millis();
        bool first = true;
        while(millis() - start < 600) {
          int ps = udp.parsePacket();
          if (ps) {
            int len = udp.read(packetBuffer, 255);
            packetBuffer[len] = 0;
            String res = String(packetBuffer);
            if (res.indexOf("\"ip\":\"" + WiFi.localIP().toString() + "\"") == -1) {
              if (!first) sp(client, F(","));
              sp(client, res);
              first = false;
            }
          }
        }
        sp(client, F("]"));
      }
      else if (reqLine.indexOf("POST /api/mesh/sync") != -1) {
        addLog("MESH", "Enviando sincronización global...");
        udp.beginPacket(IPAddress(255,255,255,255), udpPort);
        udp.print("SONA_SYNC_REQ:" + WiFi.localIP().toString());
        udp.endPacket();
        sp(client, F("HTTP/1.1 200 OK\r\n\r\n{\"ok\":true}"));
      }
      else if (reqLine.indexOf("POST /api/mesh/rename") != -1) {
        String targetIP = getParam(body, "ip");
        String newName = getParam(body, "name");
        newName.replace("+", " ");
        String authHeader = "";
        int hIdx = allHeaders.indexOf("X-Auth: ");
        if (hIdx != -1) {
            int hEnd = allHeaders.indexOf("\r\n", hIdx);
            authHeader = allHeaders.substring(hIdx + 8, hEnd);
            authHeader.trim();
        }
        addLog("MESH", "Renombrando remoto " + targetIP + " a " + newName);
        WiFiClient meshClient;
        if (meshClient.connect(targetIP.c_str(), 80)) {
            String postBody = "name=" + newName;
            meshClient.print("POST /api/sys HTTP/1.1\r\n");
            meshClient.print("Host: " + targetIP + "\r\n");
            meshClient.print("X-Auth: " + authHeader + "\r\n");
            meshClient.print("Content-Type: application/x-www-form-urlencoded\r\n");
            meshClient.print("Content-Length: " + String(postBody.length()) + "\r\n");
            meshClient.print("Connection: close\r\n\r\n");
            meshClient.print(postBody);
            unsigned long t = millis();
            while (meshClient.connected() && !meshClient.available() && millis()-t < 2000);
            meshClient.stop();
            sp(client, F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true}"));
        } else {
            sp(client, F("HTTP/1.1 500 Error\r\n\r\n"));
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
      udp.print("{\"name\":\"" + String(systemConfig.deviceName) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"ver\":\"" FW_VERSION "\"}");
      udp.endPacket();
    } 
    else if (String(packetBuffer).indexOf("SONA_SYNC_REQ:") != -1) {
      String masterIPStr = String(packetBuffer).substring(14);
      addLog("MESH", "Sincronizando desde " + masterIPStr + "...");
      
      WiFiClient syncClient;
      if (syncClient.connect(masterIPStr.c_str(), 80)) {
        syncClient.print("GET /api/backup HTTP/1.1\r\nHost: " + masterIPStr + "\r\nConnection: close\r\n\r\n");
        
        while (syncClient.connected() && !syncClient.available()) delay(10);
        
        // Saltar cabeceras HTTP
        while (syncClient.connected()) {
          String line = syncClient.readStringUntil('\n');
          if (line == "\r" || line == "") break;
        }
        
        // Leer datos binarios
        uint8_t* ptr = (uint8_t*)baseDatos;
        size_t total = 0;
        unsigned long timeout = millis();
        while (total < sizeof(baseDatos) && (millis() - timeout < 5000)) {
          if (syncClient.available()) {
            ptr[total++] = syncClient.read();
            timeout = millis();
          }
        }
        
        if (total == sizeof(baseDatos)) {
          eepromSaveAulas();
          addLog("MESH", "Base de datos sincronizada con éxito.");
        } else {
          addLog("ERROR", "Sincronización fallida (timeout/tamaño).");
        }
        syncClient.stop();
      } else {
        addLog("ERROR", "No se pudo conectar con el maestro.");
      }
    }
    else if (String(packetBuffer).indexOf("\"ip\":\"") != -1 && String(packetBuffer).indexOf("\"name\":\"") != -1) {
      // Es una respuesta de otro nodo SONA
      String res = String(packetBuffer);
      // Extraer IP para comparar
      int ipStart = res.indexOf("\"ip\":\"") + 6;
      int ipEnd = res.indexOf("\"", ipStart);
      String nodeIP = res.substring(ipStart, ipEnd);

      if (nodeIP != WiFi.localIP().toString()) {
        int nameStart = res.indexOf("\"name\":\"") + 8;
        int nameEnd = res.indexOf("\"", nameStart);
        String nodeName = res.substring(nameStart, nameEnd);
        
        addLog("MESH", "Detectado: " + nodeName + " en http://" + nodeIP);
      }
    }
  }
}

void meshDiscoveryRun() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - meshLastCheck < MESH_DISCOVERY_INTERVAL) return;
  meshLastCheck = millis();

  // Broadcast de descubrimiento
  udp.beginPacket(IPAddress(255,255,255,255), udpPort);
  udp.print("SONA_DISCOVER");
  udp.endPacket();
}

void wifiWatchdog() {
  if (millis() - wifiCheck < WIFI_CHECK_INTERVAL) return;
  wifiCheck = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    addLog("SISTEMA", "Conexión perdida. Reiniciando WiFi stack...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    
    // Esperar un momento sin bloquear el loop totalmente
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      delay(100);
      handleRFID(); // Mantener el servicio esencial vivo mientras reconecta
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      server.begin();
      udp.begin(udpPort);
      addLog("SISTEMA", "Reconexión completada con éxito.");
    }
  }
}
