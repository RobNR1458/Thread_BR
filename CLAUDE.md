# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Thread_BR** is an OpenThread Border Router implementation for the ESP32-S3 microcontroller that bridges Thread mesh networks with AWS IoT Cloud. The project integrates multiple IoT protocols:

- **Thread/IEEE 802.15.4**: Low-power wireless mesh protocol for device communication
- **CoAP (Constrained Application Protocol)**: For receiving sensor data from Thread devices
- **MQTT**: For publishing data to AWS IoT Core
- **mTLS**: For secure device authentication with AWS
- **Ethernet (W5500)**: Network connectivity via SPI

The border router collects sensor data from Thread-enabled devices via a CoAP server, aggregates the data, and publishes it to AWS IoT using MQTT with X.509 certificate-based authentication.

### Key Architectural Components

1. **OpenThread Border Router (OTBR)**: The core radio/networking layer that bridges Thread and Ethernet networks
   - Managed by `esp_openthread_border_router` component
   - Configured in `main/esp_ot_config.h`
   - Handles RCP (Radio Co-Processor) firmware updates

2. **CoAP Sensor Data Collection**: Thread devices send sensor readings via CoAP
   - Implemented in `main/thread_coap_task.c`
   - Runs a CoAP server listening on resource `sensor/data`
   - Receives `sensor_data_t` structures (temperature, humidity, pressure, gas concentration)
   - Queues data for AWS publishing via `g_aws_queue`

3. **AWS IoT Integration**: MQTT client for cloud connectivity
   - **Primary implementation**: `main/aws_task.c` with `components/aws_helpers/`
   - **Transport layer**: `network_transport.c/h` using ESP-IDF's esp-tls for TLS 1.2 connections
   - **MQTT library**: coreMQTT v1.1.0+ from AWS IoT Device SDK for Embedded C
   - **Authentication**: X.509 certificates embedded at build time (AWS Root CA, device cert, private key)
   - **Connection features**:
     - Automatic reconnection with exponential backoff (1s to 32s, max 5 retries)
     - QoS 1 (at-least-once) message delivery
     - Persistent sessions (cleanSession=false)
     - Keep-alive: 60 seconds with automatic PINGREQ/PINGRESP
   - **Data publishing**: JSON payloads to `thread/sensores` topic
   - **Current endpoint**: `a216nupm45ewkv-ats.iot.us-east-2.amazonaws.com:8883`
   - **Legacy component**: `components/aws_mqtt/` contains Fleet Provisioning code (not currently used)

4. **Inter-Task Communication**: FreeRTOS queue for data flow
   - Defined in `main/shared_data.h`
   - **Queue handle**: `g_aws_queue` (global, initialized in `app_main()`)
   - **Capacity**: 10 items of type `sensor_data_t`
   - **Flow**: Thread CoAP task → Queue → AWS IoT task
   - **Behavior**: Non-blocking send with overflow logging, 1-second blocking receive in AWS task

### Storage and Partitioning

- **NVS (Non-Volatile Storage)**: Configuration and state persistence
- **SPIFFS**: RCP firmware storage (auto-update capability)
- **Flash Partitions** (see `partitions.csv`):
  - `nvs`: 0xC000 bytes
  - `phy_init`: 0x1000 bytes
  - `factory`: 4M (main application)
  - `rcp_fw`: 1M (RCP firmware via SPIFFS)

### Network Configuration

- **Ethernet**: W5500 chip via SPI
  - SPI Host: SPI2
  - Clock: 36 MHz
  - Supports IPv6 forwarding and mDNS (Bonjour)
- **Thread Network**: Via RCP over UART1 (460800 baud)
- **IPv6**: Full support with multiple address configuration
- **mDNS**: Hostname `esp-ot-br`

## Build System

The project uses **ESP-IDF 5.4.2+** with CMake build system.

### Essential Build Commands

```bash
# Full clean build
idf.py fullclean build

# Build only (assumes previous configuration)
idf.py build

# Configure project (interactive menu)
idf.py menuconfig

# Flash to device
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Combined flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Build size analysis
idf.py size
idf.py size-components
```

### Build Configuration

Key configuration sources (in order of precedence):
1. `sdkconfig` (auto-generated, do not edit)
2. `sdkconfig.defaults` (committed defaults, edit for project-wide changes)
3. `idf.py menuconfig` (interactive configuration)

Critical configurations in `sdkconfig.defaults`:
- `CONFIG_IDF_TARGET=esp32s3` (hardware target)
- `CONFIG_OPENTHREAD_BORDER_ROUTER=y` (enable OTBR)
- `CONFIG_AUTO_UPDATE_RCP=y` (automatic RCP firmware updates)
- `CONFIG_MQTT_NETWORK_BUFFER_SIZE=2048` (MQTT buffer)
- Ethernet W5500 pin mappings (SPI SCLK, MOSI, MISO, CS, INT, RST)

### Component Dependencies

**Active dependencies** (referenced in root `CMakeLists.txt`):
- `coreMQTT`: MQTT 3.1.1 client library (AWS IoT Device SDK)
- `backoffAlgorithm`: Exponential backoff for connection retries (1s-32s range)
- `posix_compat`: POSIX API compatibility layer for FreeRTOS

**Inactive/Optional dependencies** (available but not actively used):
- `corePKCS11`: PKCS#11 cryptographic operations (for hardware secure elements)
- `Fleet-Provisioning-for-AWS-IoT-embedded-sdk`: Device provisioning with CSR

**Project-specific components**:
- `aws_helpers`: Custom component with TLS transport and clock utilities
  - `network_transport.c/h`: ESP-IDF esp-tls integration for MQTT
  - `clock_esp.c/h`: Time functions required by coreMQTT

## Code Structure

### Main Application (`main/`)

- **Thread_BR.c**: Entry point (`app_main`), initializes:
  - SPIFFS for RCP firmware storage
  - NVS flash
  - mDNS with hostname `esp-ot-br`
  - **AWS queue**: Creates `g_aws_queue` with capacity of 10 sensor_data_t items
  - OpenThread border router
  - Calls `launch_openthread_border_router()` to start OTBR
  - **Key addition**: Queue MUST be created before `launch_openthread_border_router()` to ensure availability for tasks

- **aws_task.c**: AWS IoT MQTT task (**FULLY IMPLEMENTED**)
  - **Task configuration**: 8KB stack, priority 5, 2-second startup delay
  - **Connection flow**:
    1. Initialize TLS network context with embedded certificates
    2. Establish TLS 1.2 connection to AWS IoT endpoint
    3. Initialize coreMQTT context with TransportInterface
    4. Send MQTT CONNECT with client ID = Thing name
  - **Main loop**:
    - Reads `sensor_data_t` from `g_aws_queue` (1-second timeout)
    - Formats JSON: `{"id":"...","temp":...,"hum":...,"press":...,"gas":...}`
    - Publishes to topic `thread/sensores` with QoS 1
    - Calls `MQTT_ProcessLoop()` for keep-alive and PUBACK handling
  - **Error handling**:
    - Detects connection failures (send/recv errors, timeout)
    - Automatic reconnection with exponential backoff (max 5 attempts)
    - Graceful disconnection on unrecoverable errors
  - **Configuration** (lines 29-32):
    - `AWS_IOT_ENDPOINT`: Currently set to `a216nupm45ewkv-ats.iot.us-east-2.amazonaws.com`
    - `AWS_IOT_THING_NAME`: `esp32_thread_border_router`
    - `MQTT_TOPIC`: `thread/sensores`
    - `MQTT_PORT`: 8883 (standard MQTT/TLS)
  - **Detailed comments**: Lines 13-28 provide complete configuration instructions

- **thread_coap_task.c**: CoAP server for Thread sensor data
  - Registers CoAP resource at `sensor/data`
  - Receives binary `sensor_data_t` payloads
  - Routes data to AWS queue for cloud publishing
  - Logs received readings and queue errors

- **shared_data.h**: Inter-task communication
  - `sensor_data_t`: Struct carrying temperature, humidity, pressure, gas readings
  - `g_aws_queue`: Global FreeRTOS queue handle

- **esp_ot_config.h**: OpenThread/Border Router configuration
  - UART RCP configuration (UART1, 460800 baud)
  - SPI RCP configuration (optional alternative)
  - SPIFFS mount paths for RCP firmware

### AWS Helpers Component (`components/aws_helpers/`)

**NEW COMPONENT** - Lightweight TLS transport for coreMQTT:

- **network_transport.c/h**: TLS transport implementation
  - Uses ESP-IDF's `esp-tls` library (not PKCS#11)
  - Functions: `xTlsConnect()`, `xTlsDisconnect()`, `TLS_FreeRTOS_send()`, `TLS_FreeRTOS_recv()`
  - Certificate loading: Embedded binary format (not file-based)
  - SNI support: Enabled by default (required by AWS IoT)
  - ALPN support: Optional for port 443 (`x-amzn-mqtt-ca` protocol)
  - Timeouts: Configurable via `vTlsSetConnectTimeout()`, `vTlsSetSendTimeout()`, `vTlsSetRecvTimeout()`
  - Source: Copied from `~/esp/esp-aws-iot/libraries/coreMQTT/port/network_transport/`

- **clock_esp.c/h**: Time utilities for coreMQTT
  - `Clock_GetTimeMs()`: Returns milliseconds since boot (uint32_t)
  - `Clock_SleepMs()`: FreeRTOS task delay wrapper
  - Implementation: Uses `esp_timer_get_time()` / 1000
  - Source: Copied from `~/esp/esp-aws-iot/libraries/common/posix_compat/`

- **CMakeLists.txt**: Component registration
  - Sources: `network_transport.c`, `clock_esp.c`
  - Dependencies: `esp-tls`, `mbedtls`

### Legacy AWS Component (`components/aws_mqtt/`)

⚠️ **NOTE**: This component contains Fleet Provisioning code that is **NOT CURRENTLY USED**. The project uses direct X.509 certificate authentication via `aws_helpers` instead.

Contents (for reference only):
- **mqtt_operations.c/h**: High-level MQTT wrappers (not used by aws_task.c)
- **pkcs11_operations.c/h**: Hardware crypto integration (not needed for embedded certs)
- **fleet_provisioning_with_csr_demo.c**: Automated device provisioning (optional feature)
- **fleet_provisioning_serializer.c/h**: Provisioning protocol serialization
- **mbedtls_pkcs11_posix.c/h**: PKCS#11 wrapper for mbedTLS
- **demo_config.h**: Legacy configuration file
- **Kconfig.projbuild**: Build-time menu (not actively used)

**Potential cleanup**: This component can be simplified or removed if Fleet Provisioning is not needed.

### Certificates (`certs/`)

Files embedded at compile time (via CMakeLists.txt):
- **aws-root-ca.pem**: AWS IoT Root Certificate Authority
- **device.crt**: Device X.509 certificate (signed by AWS CA)
- **device.key**: Device private key (RSA)

Referenced in `main/CMakeLists.txt` with `EMBED_TXTFILES` for binary embedding.

## Development Workflow

### Initial Setup

1. Clone repo and ensure ESP-IDF environment is sourced:
   ```bash
   . $IDF_PATH/export.sh
   ```

2. Update AWS configuration in `main/aws_task.c` (lines 29-32):
   - Set `AWS_IOT_ENDPOINT` to your AWS account endpoint (get from AWS IoT Console > Settings)
   - Set `AWS_IOT_THING_NAME` to match your Thing name in AWS IoT Core
   - Optional: Change `MQTT_TOPIC` if needed (ensure IAM policy allows publish)
   - Detailed instructions in comments at lines 13-28

3. Replace certificate files in `certs/`:
   - Download from AWS IoT Console: Certificates > Your device
   - Update: `aws-root-ca.pem`, `device.crt`, `device.key`

4. Configure network settings:
   - W5500 Ethernet pins in `sdkconfig.defaults` (already set for reference hardware)
   - RCP UART pins in `main/esp_ot_config.h` if not using default

### Common Development Tasks

**Modifying CoAP Handler**
- File: `main/thread_coap_task.c`, function `coap_handler()`
- The handler receives OpenThread messages and must queue to `g_aws_queue`
- Ensure queue capacity; current code logs and discards on full queue

**Adding MQTT Publish Topics**
- File: `main/aws_task.c`, in `aws_iot_task()` loop (starting line 210)
- Current implementation publishes to `thread/sensores` with QoS 1
- To add topics:
  1. Define new topic string constant (e.g., `#define MQTT_TOPIC_ALERTS "thread/alerts"`)
  2. Create `MQTTPublishInfo_t` structure with topic name and payload
  3. Call `MQTT_Publish(&mqttContext, &publishInfo, packetId)`
  4. Ensure IAM policy in AWS IoT allows publish to new topic
- Monitor queue depth to prevent backlog

**Changing Partition Layout**
- File: `partitions.csv`
- Ensure factory app partition ≥ binary size (see `idf.py size`)
- RCP firmware partition can be resized (minimum ~400KB for typical RCP)

**Debugging RCP Communication**
- Check UART1 pins in `esp_ot_config.h` match hardware
- Logs: Search for tag `esp_ot_br` or `esp_openthread`
- Monitor SPIFFS mount in logs; RCP firmware auto-updates on mismatch

**Adding Custom CoAP Resources**
- Create new resource in `thread_coap_task.c` similar to `sensor/data`
- Register with `otCoapAddResource()` before server starts
- Keep handler lightweight; queue data for async processing

### Testing and Monitoring

1. **Serial Monitor**: View logs and OpenThread CLI output
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

2. **OpenThread CLI**: Access via UART after flash
   - Commands: `state`, `dataset`, `child table`, `channel`, `panid`, etc.
   - See `esp_ot_cli_extension` component for custom commands

3. **MQTT Testing**:
   - Subscribe to topic in AWS IoT Console
   - Simulate CoAP sensor data push to verify flow
   - Check `MQTT_NETWORK_BUFFER_SIZE` if large payloads fail

4. **Logs to Monitor**:
   - `esp_ot_br`: Border router startup and health
   - `THREAD_COAP`: Sensor data reception and queue events
   - `AWS_TASK`: MQTT publish events and connectivity

## Key Implementation Notes

### AWS Credentials

- Certificates must be re-embedded after renewal
- Current implementation uses plain flash storage (default `Kconfig.projbuild`)
- For production, consider ATECC608A (Secure Element) or DS peripheral via Kconfig choice

### Task Stack Sizes

- AWS task: 8192 bytes (line 69 of `aws_task.c`)
- Border router tasks: Configured in `sdkconfig.defaults` (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`)
- Adjust if seeing stack overflows in logs

### Memory Constraints

- MQTT buffer: 2048 bytes (fits ~1 sensor reading with metadata)
- Increase `CONFIG_MQTT_NETWORK_BUFFER_SIZE` for larger payloads
- Monitor heap with `idf.py monitor` heap stats command

### Queue Behavior

- **Queue creation**: `g_aws_queue` is created in `Thread_BR.c:71-76` with capacity of 10 items
- **Full queue handling**: CoAP task logs warning and discards data (`thread_coap_task.c:26-27`)
- **Queue timeout**: AWS task waits 1 second for data before processing keep-alive
- **Increasing capacity**: Modify `xQueueCreate(10, ...)` in `Thread_BR.c` to larger value
- **Memory impact**: Each item is `sizeof(sensor_data_t)` ≈ 64 bytes
  - Queue 10: ~640 bytes
  - Queue 20: ~1280 bytes

## Third-Party Dependencies

Managed via `managed_components/` (ESP Component Registry):

- **espressif__cbor**: CBOR data serialization (used by Fleet Provisioning)
- **espressif__esp-serial-flasher**: Firmware update utilities
- **espressif__esp_ot_cli_extension**: CLI commands for OpenThread debugging
- **espressif__esp_rcp_update**: RCP firmware updater
- **espressif__mdns**: mDNS/Bonjour service discovery
- **espressif__usb_host_***: USB-to-UART drivers (CP210x, CH34x for RCP programming)

Run `idf.py update-dependencies` to sync with latest versions.

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails: `IDF_PATH` not set | Run `. $IDF_PATH/export.sh` |
| **TLS connection failure** | Verify certificates in `certs/` are valid, check AWS endpoint in `aws_task.c:29`, ensure internet connectivity via Ethernet |
| **MQTT_BAD_RESPONSE error** | Thing name mismatch: Check `AWS_IOT_THING_NAME` (line 30) matches AWS IoT Console. Verify IAM policy is attached to certificate. |
| **PUBACK not received** | QoS 1 timeout: Check `MQTT_ProcessLoop()` is called regularly. Increase network buffer if payloads are large (>2KB). |
| **Connection retries exhausted** | All 5 backoff attempts failed: Check Ethernet link, AWS endpoint DNS resolution, firewall rules for port 8883. View logs for specific TLS error codes. |
| **Queue full warnings** | CoAP receiving faster than AWS publishes: Increase queue size in `Thread_BR.c:71` or reduce sensor reporting rate. |
| CoAP data not queued | Check RCP is running (UART logs), verify Thread network joined, ensure `g_aws_queue` created before CoAP task starts |
| No mDNS response for `esp-ot-br` | Ensure mDNS configured in `sdkconfig.defaults`, IPv6 enabled on network |
| RCP firmware update fails | Ensure `rcp_fw` partition has ≥1M space, check UART connection |
| Ethernet not detected | Verify W5500 pin config in `sdkconfig.defaults` matches hardware |
| Flash size exceeded | Use `idf.py size-components` to identify large components; optimize or expand partition |
| **AWS Task crashes/reboots** | Stack overflow: Monitor with `uxTaskGetStackHighWaterMark()`. Increase stack from 8192 if needed. Check for null pointer dereferences in certificate loading. |

### AWS IoT Specific Logs

**Successful connection sequence:**
```
I (2000) AWS_TASK: AWS IoT Task started
I (2010) AWS_TASK: Initializing TLS network context...
I (2015) AWS_TASK: Network context initialized
I (2020) AWS_TASK: Connection attempt 1 of 5
I (2025) AWS_TASK: Connecting to AWS IoT endpoint: a216nupm45ewkv-ats.iot.us-east-2.amazonaws.com:8883
I (3500) AWS_TASK: TLS connection established successfully
I (3505) AWS_TASK: Initializing MQTT context...
I (3510) AWS_TASK: MQTT context initialized successfully
I (3515) AWS_TASK: Connecting to AWS IoT Core via MQTT...
I (3700) AWS_TASK: CONNACK received
I (3705) AWS_TASK: Connected to AWS IoT Core successfully!
I (3710) AWS_TASK: Session present: NO
I (3715) AWS_TASK: Successfully connected on attempt 1
I (3720) AWS_TASK: Connection established. Entering main loop...
I (4720) AWS_TASK: Publishing: {"id":"sensor_001","temp":25.50,"hum":60.20,"press":1013.25,"gas":150.00}
I (4800) AWS_TASK: PUBACK received for packet ID: 1
I (4805) AWS_TASK: Published successfully with packet ID: 1
```

**Reconnection with backoff:**
```
W (60000) AWS_TASK: MQTT_ProcessLoop returned status: 7  // MQTTKeepAliveTimeout
E (60005) AWS_TASK: Connection lost! Attempting to reconnect...
I (60010) AWS_TASK: Connection attempt 1 of 5
W (62000) AWS_TASK: TLS connection failed on attempt 1
W (62005) AWS_TASK: Retrying in 1000 ms...
I (63010) AWS_TASK: Connection attempt 2 of 5
...
I (65500) AWS_TASK: Successfully connected on attempt 2
I (65505) AWS_TASK: Reconnected successfully!
```

