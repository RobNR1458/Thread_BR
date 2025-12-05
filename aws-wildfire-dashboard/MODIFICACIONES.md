# 🔧 Modificaciones Necesarias para el Frontend

## 🎯 Problema Identificado

El dashboard en **https://d1t4o7dihdp0n9.cloudfront.net/** no se refresca porque el código del frontend está usando nombres de campos incorrectos al procesar las respuestas de la API.

---

## ✅ Solución

### Archivo a Modificar: `frontend/src/components/Dashboard.js`

**Ubicación**: Líneas 36 y 46

#### Cambio 1: Línea 36 (Historical Data)

**❌ Código Actual (INCORRECTO)**:
```javascript
setHistoricalData(data.readings || []);
```

**✅ Código Correcto**:
```javascript
setHistoricalData(data.data || []);
```

**Razón**: La API `/historical` retorna `{data: [...], count: ...}`, no `{readings: []}`.

---

#### Cambio 2: Línea 46 (Alerts)

**❌ Código Actual (INCORRECTO)**:
```javascript
setAlerts(data.alerts || []);
```

**✅ Código Correcto**:
```javascript
setAlerts(data.data || []);
```

**Razón**: La API `/alerts` retorna `{data: [...], stats: ...}`, no `{alerts: []}`.

---

## 📋 Código Completo Modificado

```javascript
// Fetch historical data
const loadHistoricalData = async () => {
  try {
    const range = getTimeRange(timeRange);
    const data = await fetchHistorical(selectedDevice, range.from, range.to, '5m');
    setHistoricalData(data.data || []); // ← CAMBIO AQUÍ (línea 36)
  } catch (err) {
    console.error('Error loading historical data:', err);
  }
};

// Fetch alerts
const loadAlerts = async () => {
  try {
    const data = await fetchAlerts(selectedDevice, null, 50);
    setAlerts(data.data || []); // ← CAMBIO AQUÍ (línea 46)
  } catch (err) {
    console.error('Error loading alerts:', err);
  }
};
```

---

## 🔄 Pasos para Aplicar los Cambios

### 1. Hacer las Modificaciones

Edita el archivo `frontend/src/components/Dashboard.js` y aplica los 2 cambios descritos arriba.

### 2. Rebuild del Proyecto

```bash
cd frontend

# Asegúrate de que el archivo .env existe y tiene las credenciales correctas
cat .env

# Debería mostrar:
# REACT_APP_API_ENDPOINT=https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
# REACT_APP_API_KEY=uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC
# REACT_APP_REFRESH_INTERVAL=5000
# REACT_APP_MAX_DEVICES=20

# Instalar dependencias (si aún no lo has hecho)
npm install

# Build del proyecto
npm run build
```

El build creará una carpeta `build/` con todos los archivos compilados.

### 3. Redeploy a S3

Necesitas subir los archivos de la carpeta `build/` al bucket S3 que está conectado a CloudFront.

**Opción A - Con AWS CLI**:
```bash
# Reemplaza YOUR_BUCKET_NAME con el nombre de tu bucket S3
aws s3 sync build/ s3://YOUR_BUCKET_NAME/ --delete --region us-east-2
```

**Opción B - Con consola AWS**:
1. Ve a S3 en la consola AWS
2. Abre el bucket que está conectado a CloudFront
3. Elimina todos los archivos existentes
4. Sube todos los archivos de la carpeta `build/`

### 4. Invalidar Cache de CloudFront

Esto es **crucial** para que los cambios se vean inmediatamente:

```bash
# Reemplaza YOUR_DISTRIBUTION_ID con el ID de tu distribución CloudFront
# Puedes encontrarlo en la consola CloudFront o ejecutando:
# aws cloudfront list-distributions --query "DistributionList.Items[?DomainName=='d1t4o7dihdp0n9.cloudfront.net'].Id" --output text

aws cloudfront create-invalidation \
  --distribution-id YOUR_DISTRIBUTION_ID \
  --paths "/*" \
  --region us-east-1
```

**Sin invalidación**, CloudFront seguirá sirviendo la versión antigua en cache por hasta 24 horas.

---

## 🧪 Verificación

### Antes de Hacer Cambios

Abre https://d1t4o7dihdp0n9.cloudfront.net/ y presiona F12 para abrir DevTools:

1. **Console tab**: Deberías ver errores o advertencias sobre datos vacíos
2. **Network tab**: Las requests a `/historical` y `/alerts` retornan datos pero no se muestran

### Después de Aplicar Cambios

1. **Limpia el cache del navegador** (Ctrl+Shift+R o Cmd+Shift+R)
2. Abre https://d1t4o7dihdp0n9.cloudfront.net/
3. Verifica que:
   - ✅ Los sensores muestran datos en tiempo real (cada 5 segundos)
   - ✅ La sección "Historical Data" muestra gráficas
   - ✅ La sección "ML Alerts" muestra alertas (si hay)

### Test Manual de las APIs

Puedes verificar que las APIs funcionan correctamente:

```bash
# Test 1: Realtime (esta ya funciona)
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/realtime" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"

# Test 2: Historical
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/historical?from=2025-11-27T00:00:00Z&to=2025-11-27T23:59:59Z&interval=1h" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"

# Test 3: Alerts
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/alerts" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"
```

Todas deberían retornar JSON con datos.

---

## 📊 Estructura de Respuestas API (Para Referencia)

### `/realtime` ✅ (ya funciona correctamente)
```json
{
  "readings": [
    {
      "device_id": "sensor_801A20",
      "temperature": 24.58,
      "humidity": 43.07,
      "pressure": 7.86,
      "gas_concentration": 26842,
      "heat_index": 25.57,
      "dew_point": 11.19,
      "risk_score": 70,
      "timestamp": 1764220356439
    }
  ],
  "count": 1,
  "timestamp": "2025-11-27T05:12:40.650Z"
}
```

### `/historical` ❌ (requiere fix)
```json
{
  "data": [
    {
      "device_id": "sensor_1",
      "time": "2025-11-27T00:00:00.000Z",
      "metrics": {
        "temperature": {"avg": 28.5, "max": 30.1, "min": 27.2, "count": 12},
        "humidity": {"avg": 45.3, "max": 48.0, "min": 42.5, "count": 12}
      }
    }
  ],
  "count": 1,
  "query": {...}
}
```
**Nota**: El array de datos está en `data`, NO en `readings`.

### `/alerts` ❌ (requiere fix)
```json
{
  "data": [
    {
      "alert_id": "uuid",
      "device_id": "sensor_1",
      "anomaly_type": "TEMP_SPIKE",
      "severity": "HIGH",
      "metrics": {...}
    }
  ],
  "count": 1,
  "stats": {...}
}
```
**Nota**: El array de alertas está en `data`, NO en `alerts`.

---

## 🚨 Troubleshooting

### Si los cambios no se ven después de redeploy:

1. **Invalidar cache de CloudFront** (comando arriba)
2. **Limpiar cache del navegador**: Ctrl+Shift+R (Windows) / Cmd+Shift+R (Mac)
3. **Abrir en incógnito**: Para verificar sin cache local
4. **Esperar 5-10 minutos**: La invalidación de CloudFront puede tardar

### Si aparecen errores de API:

Verifica en DevTools → Console:
```javascript
// Ejecuta esto en la consola del navegador
console.log(process.env.REACT_APP_API_ENDPOINT);
console.log(process.env.REACT_APP_API_KEY);
```

Si retorna `undefined`, el archivo `.env` no se incluyó en el build. Solución:
1. Verifica que `.env` existe en `frontend/`
2. Re-ejecuta `npm run build`
3. Vuelve a subir a S3

### Si las variables de entorno están undefined:

React solo incluye variables que empiezan con `REACT_APP_` y que existen **en el momento del build**.

**Verificar**:
```bash
cd frontend
cat .env
npm run build

# Buscar las variables en el build
grep -r "hutgdr9cdb" build/
```

Si no encuentra nada, las variables no se compilaron. Crea el archivo `.env` correctamente y vuelve a buildear.

---

## 📞 Contacto

Si tienes dudas:
- Ver documentación completa en: `DEPLOYMENT-SUMMARY.md`
- API funcionando: ✅ Verificada con curl
- Backend: ✅ 100% operativo

Solo falta este pequeño fix en el frontend para que el dashboard muestre toda la información correctamente.

---

**Última actualización**: 2025-11-27
**Tiempo estimado**: 10-15 minutos (cambios + redeploy)
**Dificultad**: Baja ⭐
