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
   - Implemented in `main/aws_task.c` and `components/aws_mqtt/`
   - Uses coreMQTT library for MQTT operations
   - X.509 certificates embedded at build time (AWS Root CA, device cert, private key)
   - Fleet Provisioning support via CSR (Certificate Signing Request)
   - Publishes to AWS topic `thread/sensores`

4. **Inter-Task Communication**: FreeRTOS queue for data flow
   - Defined in `main/shared_data.h`
   - `g_aws_queue`: Passes sensor data from CoAP task to AWS task

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

Located in external AWS IoT SDK (configured in root `CMakeLists.txt`):
- `coreMQTT`: MQTT client library
- `corePKCS11`: PKCS#11 cryptographic operations
- `Fleet-Provisioning-for-AWS-IoT-embedded-sdk`: Device provisioning
- `backoffAlgorithm`: Retry logic with exponential backoff
- `posix_compat`: POSIX compatibility layer

## Code Structure

### Main Application (`main/`)

- **Thread_BR.c**: Entry point (`app_main`), initializes:
  - SPIFFS for RCP firmware storage
  - NVS flash
  - mDNS with hostname
  - OpenThread border router
  - Calls `launch_openthread_border_router()` to start OTBR

- **aws_task.c**: AWS IoT MQTT task
  - Creates `aws_iot_task` on FreeRTOS (8KB stack, priority 5)
  - Waits for sensor data on `g_aws_queue`
  - Publishes JSON formatted data: `{"id":"...", "temp":..., "hum":..., "gas":...}`
  - **TODO in code**: Endpoint URL at line 12 must be customized for your AWS account

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

### AWS Component (`components/aws_mqtt/`)

- **mqtt_operations.c/h**: MQTT protocol operations
  - Connect, disconnect, publish, subscribe
  - Handles network transport (TLS)

- **pkcs11_operations.c/h**: Certificate and key management
  - Loads X.509 device certificate from embedded binary
  - Manages private key for TLS client authentication

- **fleet_provisioning_with_csr_demo.c**: AWS Fleet Provisioning workflow
  - Generates Certificate Signing Request (CSR)
  - Registers device using provisioning template
  - Activates provisioned certificate

- **fleet_provisioning_serializer.c/h**: Protocol serialization
  - Encodes/decodes provisioning messages

- **mbedtls_pkcs11_posix.c/h**: TLS/PKCS#11 integration
  - Bridges mbedTLS with PKCS#11 interface

- **demo_config.h**: Default configurations
  - Client ID, broker endpoint, port, buffer sizes

- **Kconfig.projbuild**: Build-time configuration menu
  - MQTT broker endpoint and port
  - Device serial number
  - PKI credential access method (flash storage default)

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

2. Update AWS configuration in `main/aws_task.c` (lines 11-14):
   - Set `AWS_IOT_ENDPOINT` to your AWS account endpoint
   - Set `AWS_IOT_THING_NAME` to match your thing name
   - Optional: Change `MQTT_TOPIC` if needed

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
- File: `main/aws_task.c`, in `aws_iot_task()` loop
- Use `MQTT_Publish()` with topic string and payload
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

- `g_aws_queue` drops data when full (log warning in `thread_coap_task.c:26-27`)
- Increase queue size in application code if frequent drops occur
- Current implicit size needs explicit queue creation in `app_main` or task

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
| MQTT connect timeout | Verify AWS endpoint in `aws_task.c:12`, check internet connectivity |
| CoAP data not queued | Check RCP is running (UART logs), verify Thread network joined |
| No mDNS response for `esp-ot-br` | Ensure mDNS configured in `sdkconfig.defaults`, IPv6 enabled on network |
| RCP firmware update fails | Ensure `rcp_fw` partition has ≥1M space, check UART connection |
| Ethernet not detected | Verify W5500 pin config in `sdkconfig.defaults` matches hardware |
| Flash size exceeded | Use `idf.py size-components` to identify large components; optimize or expand partition |

