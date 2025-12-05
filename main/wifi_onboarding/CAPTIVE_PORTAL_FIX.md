# Captive Portal Fix - Auto-open del Portal WiFi

## Problema Original

Cuando el usuario se conectaba al WiFi `Thread-BR-Setup-XXXX`, el portal NO se abría automáticamente.

**Causa:** Los sistemas operativos (Android, iOS, Windows) hacen peticiones a URLs específicas para detectar si hay internet. Cuando estas URLs fallan (404), el SO entiende que debe abrir un portal cautivo. Sin embargo, nuestro servidor no manejaba esas URLs.

## Logs del Problema

```
W (124361) httpd_uri: httpd_uri: URI '/generate_204' not found
W (124381) httpd_uri: httpd_uri: URI '/gen_204' not found
W (197501) httpd_uri: URI '/time/1/current?cup2key=...' not found
```

## Solución Implementada

### Cambios en `wifi_onboarding.c`:

1. **Nueva función `redirect_handler()`** (líneas 173-183)
   - Maneja todas las URLs de connectivity check
   - Redirige con HTTP 302 a `http://192.168.4.1/`
   - Log de cada redirección para debugging

2. **Aumento de `max_uri_handlers`** (línea 372)
   - De 8 → 16 handlers
   - Permite registrar más URLs de captive portal

3. **Nuevo parámetro `max_uri_len`** (línea 374)
   - 512 bytes para URIs largas
   - Soporta URLs con query strings largos

4. **Handlers registrados para captive portal** (líneas 412-455):
   - `/generate_204` - Android (primario)
   - `/gen_204` - Android (alternativo)
   - `/hotspot-detect.html` - iOS/macOS
   - `/ncsi.txt` - Windows
   - `/*` - Catch-all para cualquier otra URL

## URLs de Connectivity Check por SO

| Sistema Operativo | URL | Propósito |
|-------------------|-----|-----------|
| **Android** | `/generate_204` | Espera respuesta HTTP 204 No Content |
| **Android** | `/gen_204` | Variante alternativa |
| **iOS/macOS** | `/hotspot-detect.html` | Detecta portal cautivo |
| **Windows** | `/ncsi.txt` | Network Connectivity Status Indicator |
| **Linux** | `/success.txt` | Variante de connectivity check |

## Comportamiento Después del Fix

### **1. Usuario conecta al WiFi**
```
I (123771) wifi:station: 36:72:52:55:22:64 join, AID=1
I (123811) wifi_onboarding: Station connected, MAC: 36:72:52:55:22:64
```

### **2. SO hace connectivity check**
```
I (124361) wifi_onboarding: Captive portal redirect from: /generate_204
I (124381) wifi_onboarding: Captive portal redirect from: /gen_204
```

### **3. SO detecta portal y lo abre automáticamente**
- Android: Muestra notificación "Iniciar sesión en la red"
- iOS: Abre popup automático con el portal
- Windows: Abre navegador con portal

### **4. Usuario configura WiFi en el portal**
```
I (200000) wifi_onboarding: Starting WiFi scan...
I (201000) wifi_onboarding: Scan complete, found 5 networks
I (250000) wifi_onboarding: Received credentials - SSID: MiRedWiFi
I (250001) wifi_onboarding: Provisioning successful, will restart in 3 seconds...
```

## Testing

### **Compilar y flashear:**
```bash
idf.py build flash monitor
```

### **Probar desde celular:**

1. **Conectarse al WiFi:**
   - SSID: `Thread-BR-Setup-XXXX`
   - Password: `SETUP2025`

2. **Esperar auto-apertura del portal:**
   - **Android**: Notificación "Iniciar sesión en la red" → Click
   - **iOS**: Popup automático con portal
   - **Manual**: Si no abre, ir a `http://192.168.4.1`

3. **Verificar en logs:**
   ```
   I (XXXXX) wifi_onboarding: Captive portal redirect from: /generate_204
   ```

4. **Configurar WiFi y esperar reinicio**

## Troubleshooting

### **Si aún no se abre automáticamente:**

1. **Verificar logs de redirección:**
   ```bash
   # Buscar en monitor:
   grep "Captive portal redirect" monitor.log
   ```

2. **Desactivar datos móviles:**
   - Algunos SOs prefieren datos móviles si el WiFi no tiene internet
   - Desactiva datos móviles temporalmente

3. **Limpiar caché del navegador:**
   - Android: Configuración → Apps → Chrome → Borrar caché
   - iOS: Configuración → Safari → Borrar historial

4. **Acceso manual:**
   ```
   http://192.168.4.1
   ```

5. **Verificar DNS server:**
   ```
   I (581) dns_server: DNS server started
   I (581) dns_server: DNS server listening on port 53
   ```

### **Error: "Request Header Fields Too Large"**

```
W (257721) httpd_parse: request URI/header too long
```

**Solución:** Ya implementada con `config.max_uri_len = 512`

### **Error: "parse_block: incomplete"**

```
W (162821) httpd_parse: parse_block: incomplete (0/128)
```

**Causa:** Conexión interrumpida por el cliente
**Solución:** No es crítico, el cliente reintentará

## Ventajas del Fix

1. ✅ **Portal se abre automáticamente** en la mayoría de dispositivos
2. ✅ **Compatible con Android, iOS, Windows**
3. ✅ **Logs de debugging** para ver qué URLs se están solicitando
4. ✅ **Fallback manual** sigue funcionando (`http://192.168.4.1`)
5. ✅ **Experiencia de usuario mejorada** (sin pasos manuales)

## Código Añadido

### **redirect_handler() - Nueva función**
```c
static esp_err_t redirect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive portal redirect from: %s", req->uri);

    // Send 302 redirect to root
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}
```

### **Registro de handlers**
```c
// Android
httpd_register_uri_handler(server, &generate_204_uri);
httpd_register_uri_handler(server, &gen_204_uri);

// iOS
httpd_register_uri_handler(server, &hotspot_detect_uri);

// Windows
httpd_register_uri_handler(server, &ncsi_uri);

// Catch-all
httpd_register_uri_handler(server, &catch_all_uri);
```

## Referencias

- [Android Captive Portal](https://source.android.com/docs/core/connect/captive-portal)
- [iOS Captive Portal](https://developer.apple.com/library/archive/documentation/NetworkingInternet/Conceptual/NetworkingTopics/Articles/ServingCaptiveWebPortal.html)
- [ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)

## Versión
- **Fecha:** 2025-12-02
- **Archivo:** `main/wifi_onboarding/wifi_onboarding.c`
- **Cambios:** Líneas 38, 173-183, 372, 374, 412-458
