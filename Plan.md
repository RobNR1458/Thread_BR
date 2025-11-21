# Plan Escalonado: AWS IoT Core Connectivity
## Implementación Incremental con Git Commits

---

## ✅ DESCUBRIMIENTO IMPORTANTE
Tu `CMakeLists.txt` (líneas 4-8) **YA INCLUYE** las librerías principales:
- coreMQTT ✓
- backoffAlgorithm ✓
- corePKCS11 ✓
- Fleet-Provisioning ✓
- posix_compat ✓

**NO necesitas copiarlas completas**. Solo faltan archivos auxiliares del SDK que no están en include/ directo.

---

## FASE 1: Agregar Archivos Auxiliares del SDK
### Commit 1: "Add network transport and clock utilities from AWS SDK"

#### Archivos a copiar manualmente a `components/aws_helpers/`:

1. **network_transport.c** y **network_transport.h**
   - Origen: `~/esp/esp-aws-iot/libraries/coreMQTT/port/network_transport/`
   - Destino: `components/aws_helpers/`
   - Función: TLS transport usando esp-tls con certificados embebidos

2. **clock_esp.c** y **clock.h**
   - Origen: `~/esp/esp-aws-iot/libraries/common/posix_compat/`
   - Destino: `components/aws_helpers/`
   - Función: Clock_GetTimeMs() requerida por coreMQTT

#### Crear `components/aws_helpers/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "network_transport.c" "clock_esp.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tls mbedtls
)
```

#### Test: Compilar y verificar
```bash
idf.py build
```

#### Commit:
```bash
git add components/aws_helpers/
git commit -m "feat: Add AWS network transport and clock helpers

- Add network_transport.c/h for TLS connectivity with esp-tls
- Add clock_esp.c/h for time utilities
- Creates aws_helpers component for SDK utilities"
git push origin claude-edits
```

---

## FASE 2: Inicialización de Cola FreeRTOS
### Commit 2: "Initialize AWS queue in app_main"

#### Modificar `main/Thread_BR.c`:
- Agregar definición global: `QueueHandle_t g_aws_queue = NULL;`
- En `app_main()`, crear cola **ANTES** de cualquier tarea:
```c
g_aws_queue = xQueueCreate(10, sizeof(sensor_data_t));
if (g_aws_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create AWS queue");
    abort();
}
ESP_LOGI(TAG, "AWS queue created successfully");
```

#### Test: Compilar y verificar logs
```bash
idf.py build flash monitor
# Buscar: "AWS queue created successfully"
```

#### Commit:
```bash
git add main/Thread_BR.c
git commit -m "feat: Initialize AWS data queue in app_main

- Create g_aws_queue with capacity of 10 sensor_data_t items
- Ensures queue exists before tasks start
- Adds error handling for queue creation failure"
git push origin claude-edits
```

---

## FASE 3: Implementación Básica de AWS Task (Solo Conexión)
### Commit 3: "Implement basic AWS IoT connection without publishing"

#### Reescribir `main/aws_task.c`:
- Incluir headers: `network_transport.h`, `clock.h`, `core_mqtt.h`
- Implementar solo inicialización TLS y conexión MQTT
- **NO publicar** aún, solo conectar y mantener conexión
- Usar certificados embebidos
- Agregar logs detallados de cada paso

#### Actualizar `main/CMakeLists.txt`:
Agregar REQUIRES: `coreMQTT aws_helpers`

#### Test: Verificar conexión
```bash
idf.py build flash monitor
# Buscar logs:
# "[AWS_TASK] Initializing TLS transport..."
# "[AWS_TASK] Connecting to AWS IoT..."
# "[AWS_TASK] Connected successfully to AWS IoT"
```

#### Commit:
```bash
git add main/aws_task.c main/CMakeLists.txt
git commit -m "feat: Implement AWS IoT MQTT connection

- Initialize TLS transport with embedded certificates
- Establish MQTT connection to AWS IoT Core
- Add detailed logging for connection stages
- Uses network_transport and clock from aws_helpers
- No publishing yet, connection verification only"
git push origin claude-edits
```

---

## FASE 4: Agregar Publicación Desde Cola
### Commit 4: "Add MQTT publishing from sensor data queue"

#### Modificar `main/aws_task.c`:
- Agregar bucle que lee de `g_aws_queue`
- Formatear JSON: `{"id":"...", "temp":..., "hum":..., "gas":...}`
- Publicar a topic `thread/sensores` con QoS 1
- Agregar `MQTT_ProcessLoop()` para ACKs

#### Test: Enviar datos de prueba
1. Flash y monitor
2. Verificar que Thread CoAP recibe datos
3. Ver logs: `"[AWS_TASK] Published: {"id":"test"...}"`

#### Commit:
```bash
git add main/aws_task.c
git commit -m "feat: Add sensor data publishing to AWS IoT

- Read sensor_data_t from g_aws_queue
- Format JSON payload with device_id, temp, humidity, gas
- Publish to 'thread/sensores' topic with QoS1
- Add MQTT_ProcessLoop for ACK handling"
git push origin claude-edits
```

---

## FASE 5: Manejo de Reconexión
### Commit 5: "Add connection retry with exponential backoff"

#### Modificar `main/aws_task.c`:
- Incluir `backoff_algorithm.h`
- Detectar desconexiones (MQTT_ProcessLoop retorna error)
- Implementar bucle de reconexión con backoff exponencial
- Intentos: 5 reintentos, backoff 1s → 32s
- Logs de reconexión

#### Test: Simular desconexión
1. Desconectar Ethernet
2. Verificar logs de reintento
3. Reconectar y verificar recuperación

#### Commit:
```bash
git add main/aws_task.c main/CMakeLists.txt
git commit -m "feat: Add MQTT reconnection with exponential backoff

- Detect connection failures in MQTT_ProcessLoop
- Implement backoffAlgorithm for retry delays
- Max 5 retries with 1s-32s backoff range
- Log reconnection attempts and success"
git push origin claude-edits
```

---

## FASE 6: Configuración AWS
### Commit 6: "Configure AWS IoT endpoint and credentials"

#### Actualizar `main/aws_task.c`:
- Línea 12: Cambiar `AWS_IOT_ENDPOINT` a tu endpoint real
- Línea 13: Verificar `AWS_IOT_THING_NAME`
- Línea 14: (Opcional) Ajustar `MQTT_TOPIC`

#### Verificar `certs/`:
- `aws-root-ca.pem` presente
- `device.crt` válido
- `device.key` corresponde al certificado

#### Test: Conexión real a AWS
```bash
idf.py build flash monitor
# Verificar en AWS IoT Console > Test > Subscribe a 'thread/sensores'
# Debe aparecer JSON de sensores
```

#### Commit:
```bash
git add main/aws_task.c
git commit -m "config: Set production AWS IoT endpoint

- Update AWS_IOT_ENDPOINT to production account
- Verify Thing name matches AWS IoT Console
- Ready for production sensor data publishing"
git push origin claude-edits
```

---

## FASE 7 (OPCIONAL): Simplificar aws_mqtt Component
### Commit 7: "Clean up unused Fleet Provisioning code"

Solo si **NO** planeas usar Fleet Provisioning:

#### Modificar `components/aws_mqtt/CMakeLists.txt`:
- Eliminar archivos de fleet provisioning de COMPONENT_SRCS
- Simplificar REQUIRES: solo `coreMQTT aws_helpers`
- Remover `corePKCS11`, `Fleet-Provisioning`, etc.

#### Commit:
```bash
git add components/aws_mqtt/CMakeLists.txt
git commit -m "refactor: Remove unused Fleet Provisioning code

- Simplify aws_mqtt component to only MQTT operations
- Remove PKCS11 and provisioning dependencies
- Reduces binary size and complexity"
git push origin claude-edits
```

---

## RESUMEN DE ARCHIVOS

### Nuevos (copiar manualmente):
1. `components/aws_helpers/network_transport.c`
2. `components/aws_helpers/network_transport.h`
3. `components/aws_helpers/clock_esp.c`
4. `components/aws_helpers/clock.h`
5. `components/aws_helpers/CMakeLists.txt` (crear)

### Modificados:
1. `main/Thread_BR.c` - Init queue
2. `main/aws_task.c` - Implementación completa
3. `main/CMakeLists.txt` - Agregar REQUIRES
4. `components/aws_mqtt/CMakeLists.txt` - Simplificar (opcional)

**Total: 7 commits incrementales, cada uno testeable independientemente**

---

## NOTAS IMPORTANTES

### Certificados Embebidos
Los certificados se cargan desde memoria usando las referencias:
```c
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[] asm("_binary_device_cert_pem_end");
extern const uint8_t device_key_pem_start[] asm("_binary_device_key_pem_start");
extern const uint8_t device_key_pem_end[] asm("_binary_device_key_pem_end");
```

### Configuración de Red
El network_transport.c del SDK ya maneja:
- TLS 1.2 con esp-tls
- SNI (Server Name Indication)
- ALPN para puerto 443 (opcional)
- Timeouts configurables

### Prioridades FreeRTOS
- AWS Task: Priority 5 (recomendado)
- CoAP Task: Priority 6 (ligeramente mayor para recepción)
- Stack: 8192 bytes suficiente para TLS + MQTT

### Troubleshooting
| Error | Solución |
|-------|----------|
| `TLS_TRANSPORT_CONNECT_FAILURE` | Verificar endpoint, certificados, conectividad |
| `MQTT_BAD_RESPONSE` | Revisar Thing name, políticas en AWS IoT |
| `Queue full` | Aumentar tamaño de cola o publicar más rápido |
| `Stack overflow` | Aumentar stack size en xTaskCreate |
