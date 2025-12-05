# Thread Border Router - Documentación del Sistema

## Descripción General

Thread_BR es una implementación de OpenThread Border Router para ESP32-S3 que funciona como puente entre redes mesh Thread y AWS IoT Cloud. El sistema integra múltiples protocolos IoT para crear una solución completa de monitoreo de sensores con conectividad a la nube.

### Arquitectura del Sistema

El sistema consta de tres componentes principales:

1. **OpenThread Border Router (OTBR)**: Capa de radio/red que conecta la red Thread con Ethernet/WiFi
2. **Servidor CoAP**: Recolecta datos de sensores de dispositivos Thread
3. **Cliente MQTT AWS IoT**: Publica datos a la nube con autenticación X.509

## Componentes Clave

### 1. OpenThread Border Router

El OTBR gestiona la comunicación entre la red Thread y la infraestructura de red tradicional.

**Configuración Principal:**
- RCP (Radio Co-Processor) sobre UART1 a 460800 baudios
- Actualización automática de firmware RCP desde SPIFFS (Serial Peripheral Interface Flash File System)
- Soporte para IPv6 y mDNS con hostname `esp-ot-br`
- Backbone netif configurable (WiFi STA o Ethernet W5500)

**Archivos:**
- `main/esp_ot_config.h` - Configuración de UART/SPI para RCP
- `main/border_router_launch.c` - Inicialización del border router
- `components/esp_openthread_border_router` - Componente managed

### 2. Recolección de Datos CoAP

Los dispositivos Thread envían datos de sensores vía CoAP al recurso `sensordata`.

**Estructura de datos:**
```c
typedef struct {
    char device_id[32];
    float temperature;
    float humidity;
    float pressure;
    float gas;
} sensor_data_t;
```

**Flujo de datos:**
1. Sleepy End Device (SED) envía mensaje CoAP a MLEID del BR (Mesh local endpoint identifier)
2. Thread Routers (FTD) retransmiten automáticamente (multi-hop)
3. Border Router recibe en `thread_coap_task.c:coap_handler()`
4. Datos se agregan a la queue en `g_aws_queue` para publicación

**Archivos:**
- `main/thread_coap_task.c` - Servidor CoAP y handler
- `main/shared_data.h` - Definición de estructuras de datos

### 3. Integración AWS IoT

Publicación MQTT de datos de sensores a AWS IoT Core con autenticación mTLS.

**Características:**
- Autenticación X.509 con certificados embebidos (No incluidos en el repo ya que tienen acceso directo a mi cuenta de aws, pero cada quien puede sacar sus certificados al crear una "thing" en IoT core)
- Publicación a tópico `thread/sensores`
- Soporte Fleet Provisioning con CSR
- Estadísticas de tamaño de mensajes (min/max/avg cada 10 mensajes)

**Formato JSON publicado:**
```json
{
  "id": "device_001",
  "temp": 25.5,
  "hum": 60.2,
  "gas": 150
}
```

**Archivos:**
- `main/aws_task.c` - Tarea principal MQTT
- `components/aws_mqtt/` - Componente de integración AWS
- `certs/` - Certificados X.509 embebidos

## Sistema de Conectividad WiFi

### WiFi Onboarding

Sistema de configuración WiFi sin recompilar firmware mediante portal cautivo.

**Flujo de Configuración Inicial:**
1. Dispositivo detecta ausencia de credenciales en NVS (non volatile storage es una particion de la Flash)
2. Crea Access Point: SSID `Thread_Border_Router`, password `practicum2`
3. Inicia servidor DNS (puerto 53) para captive portal
4. Inicia servidor HTTP (puerto 80) con portal web
5. Usuario se conecta al AP (access point )y configura WiFi vía navegador
6. Credenciales se guardan cifradas en NVS
7. Dispositivo se reinicia y conecta automáticamente

**Portal Cautivo:**
- Escaneo automático de redes WiFi disponibles
- Interfaz web con tema oscuro moderno
- Auto-apertura en Android/iOS/Windows
- Endpoints HTTP: `/`, `/scan`, `/connect`, `/generate_204`, `/hotspot-detect.html`, etc.

**Archivos:**
- `main/wifi_onboarding/wifi_onboarding.c` - Lógica principal
- `main/wifi_onboarding/dns_server.c` - Servidor DNS para captive portal
- `main/wifi_onboarding/portal.html` - Interfaz web del portal

### WiFi Connectivity Watchdog

Monitor automático de conectividad que resetea WiFi cuando no hay internet.

**Funcionamiento:**
1. Espera 30 segundos para conexión inicial
2. Verifica conectividad cada 30 segundos (ping a 8.8.8.8)
3. Si no hay internet por 2 minutos consecutivos:
   - Borra credenciales WiFi
   - Reinicia dispositivo en modo AP
   - Preserva configuración Thread (dataset, channel, panid)

**Configuración ajustable:**
```c
#define WATCHDOG_CHECK_INTERVAL_MS   30000  // Verificación cada 30s
#define WATCHDOG_TIMEOUT_MS          120000 // Timeout a 2 minutos
#define PING_TARGET_IP               "8.8.8.8"
```

**Ventajas:**
- 100% automático 
- Permite cambio de ubicación física sin reconfiguración manual (anteriormente esto representaba un problema, ya que las credenciales wifi se quedaban embebidas y al cambiar de ubicación se tenia que borrar la flash para configurar nuevas credenciales, lo que a su vez borraba las credenciales Thread, por lo que tenia que hardcodear el dataset thread a cada dispostivo de la red manualmente.)
- Preserva toda la configuración de Thread
- Logs detallados para debugging haciendo uso del componente esp logging. 

**Archivos:**
- `main/wifi_connectivity_watchdog.c` - Implementación del watchdog
- `main/wifi_connectivity_watchdog.h` - Header público


## Topología de Red Thread

El sistema soporta redes mesh Thread multi-hop:

**Roles de dispositivos:**
- **Thread Border Router (BR)**: Este dispositivo - conecta mesh Thread a WiFi/Internet
- **Thread Router (FTD)**: Nodos relay que retransmiten mensajes entre SEDs y BR
- **Sleepy End Devices (SED)**: Nodos sensores con batería que duermen entre transmisiones

**Flujo de mensajes:**
```
SED → CoAP → Router (relay) → Border Router → AWS IoT (MQTT)
```

**Características:**
- SEDs envían mensajes CoAP a MLEID del BR independientemente de topología
- Routers retransmiten automáticamente 
- URI path `sensordata` consistente en todos los dispositivos
- Enrutamiento automático sin cambios de código
- Multi-hop aumentando distancia física entre SED y BR

## Almacenamiento y Particiones

**NVS (Non-Volatile Storage):**
- Namespace `wifi_onboarding`: Credenciales WiFi (SSID, password)
- Configuración Thread (dataset, channel, panid)
- Estado persistente del sistema

**SPIFFS:**
- Firmware RCP para actualizaciones automáticas
- Punto de montaje: `/spiffs_rcp`

**Particiones Flash (partitions.csv):**
```
nvs:      0xC000 bytes   - Configuración persistente
phy_init: 0x1000 bytes   - Calibración PHY WiFi
factory:  4M              - Aplicación principal
rcp_fw:   1M              - Firmware RCP (SPIFFS)
```

## Estadísticas MQTT

El sistema registra estadísticas de tamaño de mensajes MQTT publicados, esto permite calcular y tener un mayor control del cobro esperado del lado de aws, ya que aws incluye una calculadora para mqtt:

**Métricas por mensaje:**
- Tamaño en bytes del payload JSON
- Tamaño mínimo observado
- Tamaño máximo observado
- Promedio acumulado
- Contador total de mensajes

**Ejemplo de log:**
```
I (12345) AWS_TASK: Message size: 95 bytes | Min: 92 | Max: 98 | Avg: 94 | Count: 15
```

**Reportes resumen:**
- Se muestran cada 10 mensajes publicados
- Útil para optimizar payloads y monitorear uso de red

## Configuración del Proyecto

### Variables Críticas en sdkconfig.defaults

```ini
CONFIG_IDF_TARGET=esp32s3                    # Hardware target
CONFIG_OPENTHREAD_BORDER_ROUTER=y            # Habilitar OTBR
CONFIG_AUTO_UPDATE_RCP=y                     # Actualización automática RCP
CONFIG_MQTT_NETWORK_BUFFER_SIZE=2048         # Buffer MQTT
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y             # Soporte AP mode
CONFIG_LWIP_DNS=y                            # DNS server
CONFIG_HTTPD_MAX_URI_HANDLERS=16             # Handlers HTTP
```

### Credenciales AWS

Los certificados X.509 se embeben en tiempo de compilación:

**Archivos requeridos en `certs/`:**
- `aws-root-ca.pem` - Certificado raíz AWS IoT
- `device.crt` - Certificado del dispositivo
- `device.key` - Clave privada RSA

**Configuración en código:**
- Endpoint AWS: `main/aws_task.c` línea 12
- Thing Name: `main/aws_task.c` línea 13
- Tópico MQTT: `main/aws_task.c` línea 14

## Comandos de Compilación

### Setup del entorno
```bash
. $HOME/esp/esp-idf/export.sh
```

### Compilación limpia
```bash
idf.py fullclean
idf.py build
```

### Flasheo y monitoreo
```bash
# Flashear firmware
idf.py -p /dev/ttyUSB0 flash

# Monitorear logs
idf.py -p /dev/ttyUSB0 monitor

# Flashear y monitorear
idf.py -p /dev/ttyUSB0 flash monitor
```

### Borrar NVS (reset WiFi onboarding)
```bash
# Opción 1: Solo NVS (mantiene firmware)
idf.py -p /dev/ttyUSB0 erase-nvs

# Opción 2: Flash completo (borra todo)
idf.py -p /dev/ttyUSB0 erase-flash
```

### Análisis de tamaño
```bash
idf.py size
idf.py size-components
```

## Tamaños de Stack FreeRTOS

**Tareas principales:**
- AWS IoT task: 8192 bytes (`main/aws_task.c`)
- CoAP server task: Definido en `thread_coap_task.c`
- Border router tasks: `CONFIG_ESP_MAIN_TASK_STACK_SIZE` en sdkconfig
- DNS server: 4096 bytes
- HTTP server: 8192 bytes

Ajustar si se detectan stack overflows en logs.

## Dependencias Externas

### Managed Components (ESP Component Registry)

```
espressif__cbor                      - Serialización CBOR
espressif__esp-serial-flasher        - Actualización firmware
espressif__esp_ot_cli_extension      - Comandos CLI OpenThread
espressif__esp_rcp_update            - Actualizador RCP
espressif__mdns                      - mDNS/Bonjour
espressif__usb_host_*                - Drivers USB-UART (CP210x, CH34x)
```

Actualizar con: `idf.py update-dependencies`

### AWS IoT SDK (configurado en root CMakeLists.txt)

```
coreMQTT                             - Cliente MQTT
corePKCS11                           - Operaciones criptográficas
Fleet-Provisioning-for-AWS-IoT       - Provisioning de dispositivos
backoffAlgorithm                     - Lógica de reintentos
posix_compat                         - Capa de compatibilidad POSIX
```

## Troubleshooting

### WiFi y Conectividad

| Problema | Solución |
|----------|----------|
| Portal cautivo no se abre | Ir manualmente a http://192.168.4.1 |
| No aparece Thread_Border_Router | Verificar logs "WiFi AP started", reiniciar dispositivo |
| Contraseña AP rechazada | Usar exactamente `practicum2` (minúsculas) |
| CLI `wifi state` muestra "disconnected" | Bug cosmético conocido, verificar IP en logs `wifi_onboarding` |
| Watchdog resetea con internet | Firewall puede bloquear ICMP, cambiar `PING_TARGET_IP` |
| Cambio de ubicación sin WiFi | Esperar 2 minutos para auto-reset o usar comando `wifireset` |

### MQTT y AWS

| Problema | Solución |
|----------|----------|
| Timeout de conexión MQTT | Verificar endpoint AWS en `aws_task.c:12`, revisar internet |
| Datos CoAP no llegan a AWS | Verificar RCP running (logs UART), red Thread joined |
| Buffer MQTT insuficiente | Aumentar `CONFIG_MQTT_NETWORK_BUFFER_SIZE` en sdkconfig |

### OpenThread y RCP

| Problema | Solución |
|----------|----------|
| RCP no comunica | Verificar pines UART en `esp_ot_config.h` vs hardware |
| Actualización RCP falla | Verificar partición `rcp_fw` ≥1M, revisar SPIFFS mount |
| Backbone netif error | WiFi debe conectar antes de inicializar BR, revisar logs |

### Comandos WiFi CLI

El comando CLI `wifi state` puede mostrar "disconnected" incluso cuando WiFi funciona correctamente. Esto es un problema cosmético del módulo `esp_ot_cli_extension` que no registra handlers de eventos cuando se usa `wifi_onboarding` en lugar de `esp_ot_wifi_connect()`. No afecta la funcionalidad real.

### Build y Flash

| Problema | Solución |
|----------|----------|
| Build falla: IDF_PATH no set | Ejecutar `. $IDF_PATH/export.sh` |
| Flash size exceeded | `idf.py size-components`, optimizar o expandir partición |
| portal.html not found | Verificar `ls main/wifi_onboarding/portal.html` |

## Estructura de Archivos Clave

```
Thread_BR/
├── main/
│   ├── Thread_BR.c                  # Entry point, inicialización
│   ├── aws_task.c                   # Cliente MQTT AWS IoT
│   ├── thread_coap_task.c           # Servidor CoAP Thread
│   ├── shared_data.h                # Estructuras de datos compartidas
│   ├── esp_ot_config.h              # Configuración OpenThread/RCP
│   ├── border_router_launch.c       # Inicialización border router
│   ├── wifi_connectivity_watchdog.c # Monitor de conectividad
│   ├── wifi_reset_cmd.c             # Comando CLI reset WiFi
│   └── wifi_onboarding/
│       ├── wifi_onboarding.c        # Lógica AP + HTTP + NVS
│       ├── dns_server.c             # Servidor DNS captive portal
│       └── portal.html              # Interfaz web del portal
├── components/
│   └── aws_mqtt/                    # Integración AWS IoT
│       ├── mqtt_operations.c        # Operaciones MQTT
│       ├── pkcs11_operations.c      # Gestión certificados
│       └── fleet_provisioning_*     # Fleet Provisioning
├── certs/
│   ├── aws-root-ca.pem              # CA raíz AWS
│   ├── device.crt                   # Certificado dispositivo
│   └── device.key                   # Clave privada
├── managed_components/              # Componentes ESP Registry
├── partitions.csv                   # Layout de particiones flash
├── sdkconfig.defaults               # Configuración por defecto
└── CMakeLists.txt                   # Build system
```

## Características de Seguridad

### Credenciales WiFi
- Guardadas cifradas en NVS con cifrado AES
- Persisten entre reinicios
- Solo borrables con `erase-nvs`, `erase-flash`, o comando `wifireset`

### Access Point
- WPA2-PSK por defecto (contraseña: `practicum2`)
- Solo activo durante configuración inicial
- Se apaga automáticamente después de provisionar

### Certificados AWS
- Almacenados en flash (embebidos en compile-time)
- mTLS para autenticación con AWS IoT Core
- Opción de usar Secure Element (ATECC608A) vía Kconfig

### Comunicación Thread
- Cifrado AES-128-CCM en capa Thread
- Autenticación de dispositivos vía dataset compartido

## Notas de Desarrollo

### Primera Configuración

1. **Actualizar configuración AWS** en `main/aws_task.c` (líneas 11-14):
   - Endpoint AWS IoT
   - Thing Name
   - Tópico MQTT (opcional)

2. **Reemplazar certificados** en `certs/`:
   - Descargar desde AWS IoT Console
   - Copiar: `aws-root-ca.pem`, `device.crt`, `device.key`

3. **Configuración de red**:
   - WiFi se configura vía portal cautivo (sin pre-configuración)
   - Ethernet W5500 (alternativa): pines en `sdkconfig.defaults`
   - RCP UART: pines en `main/esp_ot_config.h` si difieren del default

### Primer Arranque

Cuando no hay credenciales WiFi almacenadas:

1. Dispositivo muestra en logs:
```
====================================================
  FIRST TIME SETUP - WiFi Configuration Required
====================================================
1. Connect your phone/laptop to WiFi: Thread_Border_Router
2. Password: practicum2
3. Portal will open automatically (or go to http://192.168.4.1)
4. Select your WiFi network and enter password
5. Device will restart and connect to your WiFi
====================================================
```

2. Conectar dispositivo móvil a `Thread_Border_Router` con password `practicum2`
3. Portal cautivo se abre automáticamente (Android/iOS/Windows)
4. Seleccionar red WiFi y ingresar contraseña
5. Dispositivo guarda credenciales y reinicia
6. Arranques subsecuentes: conexión automática

### Monitoreo y Debugging

**Logs importantes a monitorear:**
- `esp_ot_br` - Border router startup y health
- `wifi_onboarding` - Provisioning, portal cautivo, conexión
- `wifi_watchdog` - Monitor de conectividad, timeouts
- `THREAD_COAP` - Recepción datos sensores, eventos queue
- `AWS_TASK` - Eventos MQTT, estadísticas mensajes

**Comandos CLI OpenThread útiles:**
```
> state              # Estado del nodo (leader/router/child)
> dataset            # Configuración de red Thread
> child table        # Dispositivos conectados
> channel            # Canal Thread actual
> panid              # PAN ID de la red
> ipaddr             # Direcciones IPv6
```

### Modificación de Handler CoAP

Archivo: `main/thread_coap_task.c`, función `coap_handler()`

- Handler recibe mensajes OpenThread y encola a `g_aws_queue`
- Verificar capacidad de queue (actualmente 10 elementos)
- Logs y descarte si queue lleno

### Agregar Tópicos MQTT

Archivo: `main/aws_task.c`, loop `aws_iot_task()`

- Usar `MQTT_Publish()` con tópico y payload
- Monitorear profundidad de queue para prevenir backlog

### Cambio de Layout de Particiones

Archivo: `partitions.csv`

- Verificar factory app partition ≥ tamaño binario (`idf.py size`)
- RCP firmware partition redimensionable (mínimo ~400KB)
- Después de cambios: `idf.py erase-flash && idf.py flash`

## Configuración de Hardware

### Pines ESP32-S3

**RCP UART (OpenThread):**
- Configurados en `main/esp_ot_config.h`
- UART1, 460800 baudios por defecto

**Ethernet W5500 (opcional):**
- SPI Host: SPI2
- Clock: 36 MHz
- Pines configurados en `sdkconfig.defaults`

### Requisitos de Hardware

- ESP32-S3 (mínimo 4MB flash, 2MB PSRAM recomendado)
- Módulo RCP OpenThread (ej: ESP32-H2)
- Opcional: Ethernet W5500 vía SPI
- Alimentación estable 5V/2A

## Versiones de Software

- ESP-IDF: 5.4.2+
- OpenThread: Incluido en ESP-IDF
- AWS IoT SDK: Versionado en submodules
- Managed Components: Actualizados vía Component Registry

## Métricas de Rendimiento

**Tamaño de firmware:**
- Binario principal: ~1.5 MB
- Partición factory: 4 MB (64% libre)

**Memoria:**
- MQTT buffer: 2048 bytes (ajustable)
- Queue capacity: 10 mensajes sensor_data_t
- DNS server stack: 4096 bytes
- HTTP server stack: 8192 bytes

**Latencia:**
- CoAP → AWS publicación: < 1 segundo (depende de red)
- WiFi onboarding: ~2 minutos (incluye setup manual)
- Auto-reset WiFi watchdog: 2.5 minutos (30s inicial + 120s timeout)

## Créditos

**Proyecto base:**
- Ejemplos ESP-IDF OpenThread Border Router
- AWS IoT Embedded SDK
- Managed components de Espressif

**Desarrollado por:**
- Roberto Negrete Román y Jorge Ramírez Cárdenas
- Como parte de proyecto Artemis para la materia Practicum II de la Universidad Anáhuac México


## Referencias Técnicas

### Documentación Oficial

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [OpenThread Documentation](https://openthread.io/guides)
- [AWS IoT Core Developer Guide](https://docs.aws.amazon.com/iot/)
- [Thread Specification](https://www.threadgroup.org/support#specifications)

### Repositorios Relacionados

- ESP-IDF: https://github.com/espressif/esp-idf
- OpenThread: https://github.com/openthread/openthread
- AWS IoT SDK C: https://github.com/aws/aws-iot-device-sdk-embedded-C

## Contacto y Soporte

Favor de contactarme creando un issue o mandandome un correo al correo roberto-ne-ro@hotmail.com
