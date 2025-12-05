# ⚠️ Tareas Pendientes - Frontend Deployment

## 📋 Estado Actual

### ✅ Completado (Backend - 100%)
- Toda la infraestructura AWS está desplegada y funcionando
- 5 Lambda functions operativas
- API Gateway con 3 endpoints (/realtime, /historical, /alerts)
- DynamoDB con datos de sensores + alertas
- ML Processor ejecutándose cada 5 minutos
- S3 archival configurado

### ❌ Pendiente (Frontend)
- Build del proyecto React
- Deploy a hosting (Amplify/Vercel/S3)

---

## 🚧 Problema Encontrado

### Error de Build en WSL

**Error**:
```
Error: Cannot find module 'C:\Windows\package.json'
```

**Causa**:
`react-scripts` (parte de Create React App) tiene problemas de compatibilidad con WSL (Windows Subsystem for Linux) al intentar ejecutar `npm run build`. El script intenta usar `cmd.exe` de Windows y falla al resolver paths de archivos.

**Ubicación del problema**:
- Directorio: `/home/Roberto800/Projects/Thread_BR/aws-wildfire-dashboard/frontend`
- Comando que falla: `npm run build`

---

## ✅ Lo que YA está configurado

### Archivo `.env` creado con credenciales:
```env
REACT_APP_API_ENDPOINT=https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
REACT_APP_API_KEY=uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC
REACT_APP_REFRESH_INTERVAL=5000
REACT_APP_MAX_DEVICES=20
```

### Dependencias instaladas:
- `node_modules/` ya contiene todas las dependencias (1312 packages)
- `package.json` y `package-lock.json` correctos

---

## 🛠️ Soluciones para Completar el Deployment

### Opción 1: Docker (Recomendada - funciona en WSL)

```bash
cd /ruta/a/aws-wildfire-dashboard/frontend

# Build usando contenedor Docker
docker run --rm \
  -v $(pwd):/app \
  -w /app \
  node:20 \
  bash -c "npm install && npm run build"

# Los archivos compilados estarán en ./build/
```

Luego puedes desplegar los archivos de `build/` a cualquier hosting.

---

### Opción 2: Vercel (Más simple - hosting incluido)

```bash
# Instalar Vercel CLI
npm install -g vercel

# Desde el directorio frontend
cd /ruta/a/aws-wildfire-dashboard/frontend

# Deploy (Vercel hace build automáticamente)
vercel --prod

# Te dará una URL: https://wildfire-dashboard-xxxxx.vercel.app
```

**Ventaja**: Vercel maneja el build automáticamente, no necesitas hacerlo manualmente.

---

### Opción 3: Linux nativo o Mac

Si tienes acceso a Linux nativo (no WSL) o Mac:

```bash
cd /ruta/a/aws-wildfire-dashboard/frontend

# Build normal
npm install
npm run build

# Deploy a S3
aws s3 sync build/ s3://wildfire-dashboard-frontend --delete
```

---

### Opción 4: AWS Amplify CLI

```bash
# Instalar Amplify CLI
npm install -g @aws-amplify/cli

# Configurar AWS credentials
amplify configure

# Desde directorio frontend
cd /ruta/a/aws-wildfire-dashboard/frontend

# Inicializar Amplify
amplify init
# Responde:
# - Name: wildfire-dashboard
# - Environment: prod
# - Editor: (tu preferencia)
# - Type: JavaScript
# - Framework: React
# - Source directory: src
# - Distribution directory: build
# - Build command: npm run build
# - Start command: npm start

# Agregar hosting
amplify add hosting
# Select: Hosting with Amplify Console
# Type: Manual deployment

# Publicar (Amplify hace build en la nube)
amplify publish

# Te dará una URL: https://xxxxxx.amplifyapp.com
```

**Ventaja**: Amplify hace el build en sus servidores AWS, evita problemas locales.

---

### Opción 5: S3 + CloudFront (Deployment manual)

Primero necesitas hacer el build con Docker (Opción 1), luego:

```bash
# Crear bucket S3
aws s3 mb s3://wildfire-dashboard-frontend --region us-east-2

# Configurar como website
aws s3 website s3://wildfire-dashboard-frontend \
  --index-document index.html \
  --error-document index.html

# Subir archivos (desde directorio frontend)
aws s3 sync build/ s3://wildfire-dashboard-frontend --delete

# Hacer público
aws s3api put-bucket-policy \
  --bucket wildfire-dashboard-frontend \
  --policy '{
    "Version": "2012-10-17",
    "Statement": [{
      "Sid": "PublicReadGetObject",
      "Effect": "Allow",
      "Principal": "*",
      "Action": "s3:GetObject",
      "Resource": "arn:aws:s3:::wildfire-dashboard-frontend/*"
    }]
  }'

# URL de acceso:
# http://wildfire-dashboard-frontend.s3-website.us-east-2.amazonaws.com
```

Para agregar CloudFront (CDN + HTTPS):
```bash
# Crear distribución CloudFront
aws cloudfront create-distribution \
  --origin-domain-name wildfire-dashboard-frontend.s3-website.us-east-2.amazonaws.com \
  --default-root-object index.html

# CloudFront te dará un dominio: https://xxxxx.cloudfront.net
```

---

## 🔍 Verificación del Frontend

Una vez desplegado, verifica que funciona:

### 1. Abrir la URL del dashboard

### 2. Verificar que carga sin errores de consola

Abre DevTools (F12) → Console, no debe haber errores de CORS o API

### 3. Verificar que muestra datos

- El dashboard debe mostrar los últimos datos de sensores
- Los gráficos deben actualizarse cada 5 segundos
- Debe mostrar `risk_score`, `heat_index`, `dew_point`

### 4. Test de funcionalidad

- **Página principal**: Dashboard con métricas en tiempo real
- **Gráficos**: Temperatura, humedad, gas concentration
- **Risk Score**: Indicador de riesgo (0-100)
- **Alertas**: Panel de anomalías (puede estar vacío inicialmente)

---

## 📊 APIs Disponibles para el Frontend

El frontend ya está configurado para usar estas APIs:

### Base URL:
```
https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
```

### Endpoints:

**1. Datos en tiempo real**
```bash
GET /realtime?deviceId={optional}
Headers: x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC

Response:
{
  "readings": [{
    "device_id": "sensor_1",
    "temperature": 28.5,
    "humidity": 45,
    "pressure": 1015,
    "gas_concentration": 450,
    "heat_index": 28.54,
    "dew_point": 15.39,
    "risk_score": 73,
    "timestamp": 1764143735326
  }],
  "count": 1
}
```

**2. Datos históricos**
```bash
GET /historical?from=2025-11-26T00:00:00Z&to=2025-11-26T23:59:59Z&interval=1h&deviceId={optional}
Headers: x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC

Response:
{
  "data": [{
    "device_id": "sensor_1",
    "time": "2025-11-26T07:00:00.000Z",
    "metrics": {
      "temperature": {"avg": 28.5, "max": 30.1, "min": 27.2, "count": 12},
      "humidity": {"avg": 45.3, "max": 48.0, "min": 42.5, "count": 12}
    }
  }],
  "count": 1
}
```

**3. Alertas de anomalías**
```bash
GET /alerts?deviceId={optional}&severity={LOW|MODERATE|HIGH|CRITICAL}&limit=50
Headers: x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC

Response:
{
  "data": [{
    "alert_id": "uuid",
    "device_id": "sensor_1",
    "anomaly_type": "TEMP_SPIKE",
    "severity": "HIGH",
    "metrics": {
      "current_value": 35.2,
      "mean": 28.5,
      "z_score": 3.19
    }
  }],
  "stats": {
    "total": 1,
    "bySeverity": {"LOW": 0, "MODERATE": 0, "HIGH": 1, "CRITICAL": 0}
  }
}
```

---

## 🔐 Credenciales Importantes

**API Key** (ya configurada en `.env`):
```
uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC
```

**Region AWS**:
```
us-east-2 (Ohio)
```

**Account ID**:
```
503767748192
```

---

## ⏱️ Tiempo Estimado

- **Docker build + S3**: ~10 minutos
- **Vercel**: ~5 minutos (más simple)
- **Amplify CLI**: ~15 minutos (primera vez)

---

## 🆘 Troubleshooting

### Si el dashboard no muestra datos:

1. **Verificar que el ESP32 está publicando**:
```bash
# Ver logs de IoT Core
aws iot describe-endpoint --endpoint-type iot:Data-ATS --region us-east-2

# Verificar reglas IoT activas
aws iot list-topic-rules --region us-east-2
```

2. **Verificar que hay datos en DynamoDB**:
```bash
aws dynamodb scan \
  --table-name wildfire-sensor-data \
  --limit 5 \
  --region us-east-2
```

3. **Probar APIs manualmente**:
```bash
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/realtime" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"
```

### Si hay errores de CORS:

El API Gateway ya tiene CORS configurado. Si ves errores de CORS:
- Verifica que la API Key está en el header `x-api-key`
- Verifica que el frontend usa `https://` (no `http://`)

---

## 📞 Contacto y Recursos

**Documentación completa**: Ver `DEPLOYMENT-SUMMARY.md`

**Archivos importantes**:
- `frontend/.env` - Credenciales API
- `frontend/src/` - Código fuente React
- `backend/lambda/` - Funciones Lambda (ya desplegadas)

**APIs funcionando**:
- ✅ API Gateway activo
- ✅ Lambda functions operativas
- ✅ DynamoDB con datos
- ✅ ML Processor ejecutándose

**Solo falta**: Build + Deploy del frontend React

---

**Última actualización**: 2025-11-26
**Estado backend**: ✅ 100% Operativo
**Estado frontend**: ⚠️ 90% (falta build + deploy)
