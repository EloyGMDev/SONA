<div align="center">

# SONA System v2.0

**Firmware de navegación asistida y control de accesos para entornos educativos.** Desarrollado para plataformas embebidas híbridas (Renesas RA4M1 / ESP32-S3), con arquitectura síncrona no bloqueante, persistencia segura y servicios de red distribuidos.

[![License](https://img.shields.io/badge/license-MIT-238636?style=flat-square)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.0-6e40c9?style=flat-square)](#)


---

</div>

## 1. Archivo Principal y Flujo de Control

### `rfid_navegacion_v2.ino`

Punto de entrada (*sketch* principal) del sistema. Implementa el ciclo de vida del hardware mediante un paradigma de ejecución cooperativa.

* **`setup()`**: Configura pines físicos e inicializa de forma secuencial la pila de hardware y software:
* Lanza el audio inicial de arranque (`earconBoot`).
* Inicializa el reloj en tiempo real (RTC), el lector RFID y el subsistema BLE.
* Carga estructuras de datos desde la EEPROM con verificación de integridad de base de datos y contraseñas.
* Levanta la pila de red Wi-Fi, el servidor web, el resolvedor de nombres mDNS y fuerza la primera sincronización horaria mediante NTP.


* **`loop()`**: Implementa un **hilo de ejecución cooperativo no bloqueante**. Despacha cíclicamente tareas mediante temporizadores basados en `millis()`:
* *Polling* de lectura de tarjetas RFID.
* Administración web HTTP y mensajería de red.
* Watchdog de conectividad Wi-Fi y descubrimiento en malla (Mesh).
* Resincronización horaria periódica de NTP.
* Monitorización de eventos BLE y resolución de nombres locales por mDNS.



---

## 2. Configuración y Estructura de Datos

### `config.h`

Cabecera central de configuración global y mapa de memoria.

* **Mapeos Físicos:** Definición de pines SPI, pin del buzzer piezoeléctrico y pin de registro rápido (`BTN_REGISTER`).
* **Límites del Sistema:** Parámetros operativos rígidos (`MAX_AULAS = 15`, `MAX_HIST = 10`) y *bitmasks* binarios para la planificación semanal (`DAY_MON` a `DAY_SUN`).
* **Estructuras Clave (`structs`):**
* `Aula`: Almacena UID, nombre, patrón acústico, contador de accesos, restricciones horarias y marcas de caducidad.
* `HistEntry`: Registro de accesos indexado con marca de tiempo RTC y código de resultado del planificador.
* `ConfigPwd`: Hash de seguridad de la contraseña de administración.
* `SystemConfig`: Parámetros de red IP, configuración de zona horaria y firma CRC32 de integridad.


* **Direccionamiento:** Calcula los *offsets* de direccionamiento lineal contiguo de la EEPROM y declara las variables globales compartidas (`extern`).

### `version.h`

Metadatos estrictos del firmware. Define el número de versión activa (`FW_VERSION`) y macros de tiempo de compilación.

### `globals.cpp`

Instancia y aloja la memoria asignada a las variables globales declaradas en `config.h`. Contiene las credenciales Wi-Fi hardcodeadas, la base de datos en RAM, estadísticas de tránsito en tiempo real, estructuras de control *anti-rebote* (*debounce*) del lector RFID y temporizadores de los watchdogs.

---

## 3. Capa de Persistencia y Base de Datos

### `database.h` / `database.cpp`

Abstracción de bajo nivel de acceso a la memoria EEPROM física.

```
+-------------------------------------------------------------------------+
|                          EEPROM LINEAR MEMORY                           |
+--------------------+--------------------+-------------------------------+
|  SystemConfig      |  ConfigPwd         |  Aula [0..14]                 | ...
|  (Params + CRC32)  |  (Password Hash)   |  (UID, Name, Patterns, Hours) |
+--------------------+--------------------+-------------------------------+

```

* **Llamadas CRUD:** Serializa y vuelca arrays directos de estructuras utilizando los *offsets* estáticos calculados (`baseDatos`, `historial`, `cfgPwd`, `systemConfig`).
* **Mecanismo de Integridad:** Gobernado por una suma de verificación **CRC32** sobre la estructura `SystemConfig`. Evita la carga de datos corruptos por caídas de tensión o fallos físicos de la memoria.
* **Resiliencia:** Incorpora formateo seguro ante fallos catastróficos (`eepromResetAll()`), rutinas de mantenimiento para el historial circular de eventos y reinicio selectivo de estadísticas de tránsito.

---

## 4. Control de Periféricos y Capa Física

### `hardware_io.h` / `hardware_io.cpp`

Capa de abstracción de hardware (HAL) para transceptores de entrada y salida.

* **Subsistema RFID (MFRC522):** Inicializa el bus SPI y configura el chip lector, forzando la **máxima ganancia de antena** para optimizar la distancia de lectura en entornos de movilidad reducida.
* **`handleRFID()`**: Interrupción por *polling* que procesa tarjetas de proximidad.
* Aplica un filtro *anti-bounce* de **5 segundos por tag** para evitar lecturas duplicadas consecutivas.
* Realiza búsquedas lógicas en la base de datos indexada en RAM.
* Contrasta la hora actual del RTC con las restricciones de la estructura del aula y dispara la respuesta acústica, visual y los logs de auditoría.


* **`earcon()` / `earconBoot()**`: Secuenciador acústico no bloqueante. Emite ráfagas de ondas de tonos en el piezoeléctrico (`BUZZER_PIN`) según el perfil del aula identificada, sirviendo como baliza auditiva para usuarios invidentes.
* **Subsistema BLE:** Inicializa y difunde la publicidad (*advertising*) de red Bluetooth Low Energy bajo el identificador de servicio **`0xFD00`**. Expone el nombre lógico del dispositivo (`deviceName`) para permitir la detección pasiva de proximidad mediante *smartphones*.

### `led_control.h`

Interfaz de API para el control de un LED RGB de estado. Asigna códigos de colores estáticos y secuencias de parpadeo rítmicas a estados críticos del sistema:

| Estado del Sistema | Código de Color RGB / Patrón |
| --- | --- |
| **Fallo de Red / Conectividad** | Rojo Estático |
| **Modo Registro Activo** | Azul Parpadeante Rápido |
| **Bloqueo de Emergencia (Lockdown)** | Magenta Estático |
| **Operación Normal** | Verde Pulso Atenuado |

---

## 5. Reglas de Negociación y Horarios

### `scheduler.h`

Motor lógico de validación de accesos e identidades. Declara los códigos de resultado de verificación que determinan el comportamiento del hardware ante un evento de lectura:

> * `ACCESS_OK`: Identificación válida, dentro de horario y autorizada.
> * `ACCESS_DENY_HOURS`: UID registrado pero fuera de la franja horaria permitida.
> * `ACCESS_DENY_LOCKDOWN`: Acceso denegado globalmente por cierre de seguridad del centro.
> 
> 

Declara las cabeceras de funciones para la inserción y consulta dinámica de listas negras de UIDs comprometidos.

---

## 6. Servicios de Red Avanzados

### `mdns_responder.h` / `mdns_responder.cpp`

Resolvedor multicast DNS nativo de huella ligera.

* Opera directamente sobre sockets UDP escuchando en el puerto **5353** bajo la dirección IP multicast **`224.0.0.251`**.
* Analiza tramas binarias de red mDNS (*Wire format*) y responde con un registro de recurso **Tipo A** (IPv4) cuando recibe una consulta local por el dominio **`sona.local`**, eliminando la necesidad de servidores DNS dedicados o direccionamiento estático en la red del centro.

### `ntp_sync.h` / `ntp_sync.cpp`

Módulo de sincronización cronológica de alta precisión. Consume la API interna del framework del ESP32-S3 para realizar peticiones SNTP por red, calcula los desfases horarios geográficos (`utcOffset`) y actualiza por hardware el reloj de tiempo real (RTC) del microcontrolador principal Renesas RA4M1 a través de bus local.

### `notifications.h`

Capa de integración externa. Despacha webhooks salientes mediante peticiones **HTTP POST** asíncronas con payloads formateados en JSON estructurado hacia servidores centrales de monitorización ante cualquier evento crítico del sistema.

### `web_server.h` / `web_server.cpp`

Núcleo central de las comunicaciones IP del microcontrolador.

* **Servidor HTTP (Puerto 80):** Renderiza el panel de administración web local y expone una **API REST JSON** para operaciones CRUD de asignación de aulas, gestión de slots horarios y extracción de logs de auditoría.
* **`wifiWatchdog()`**: Watchdog asíncrono que audita de forma continua la calidad y estado del enlace Wi-Fi. Ante pérdidas de conectividad persistentes, reinicia la pila de red de forma aislada sin bloquear el bucle de control del lector RFID o del buzzer.
* **Protocolo Mesh (Puerto 42800 UDP):** Envía y recibe tramas de broadcast UDP bajo el identificador **`SONA_DISCOVER`**. Implementa un protocolo descentralizado punto a punto que permite a los nodos adyacentes mapear la topología física del edificio automáticamente.

---

## 7. Funciones Auxiliares y Diagnóstico

### `utils.h` / `utils.cpp`

Biblioteca utilitaria de bajo nivel del kernel del firmware.

* **Buffer Circular de Logs:** Aloja un espacio en memoria RAM estática dedicado al volcado de trazas de depuración ordenadas por niveles de severidad (`DEBUG`, `INFO`, `WARNING`, `ERROR`).
* **Telemetría en Runtime:** Expone funciones de diagnóstico del estado del microcontrolador como `freeRam()` y `uptimeFormatted()`.
* **Rutinas Matemáticas:** Lógica optimizada para el cálculo polinómico del CRC32 hexadecimal, funciones de conversión de *timestamps* UNIX a estructuras de calendario legibles e intérpretes de conversión de registros binarios del RTC a máscaras de bits operativas del planificador.
