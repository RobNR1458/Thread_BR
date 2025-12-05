# 🚀 AWS Wildfire Detection System - Deployment Summary

## ✅ Estado del Deployment: COMPLETO (Backend)

**Fecha**: 2025-11-26
**Región AWS**: us-east-2 (Ohio)
**Account ID**: 503767748192

---

## 📊 Arquitectura Desplegada

### Flujo de Datos Completo

```
ESP32 Thread Border Router
    │
    ├─► MQTT Topic: thread/sensores
    │   Payload: {"id":"sensor_1","temp":28.5,"hum":45.0,"press":1015.0,"gas":450.0}
    │
    └─► AWS IoT Core (3 reglas paralelas)
        │
        ├─► Rule: SensorToDynamoDB
        │   └─► DynamoDB: wildfire-sensor-data (datos raw)
        │
        ├─► Rule: SensorToEnricher
        │   └─► Lambda: wildfire-data-enricher
        │       ├─► Calcula heat_index (índice de calor)
        │       ├─► Calcula dew_point (punto de rocío)
        │       ├─► Calcula risk_score (0-100)
        │       └─► DynamoDB: wildfire-sensor-data (datos enriquecidos)
        │
        └─► Rule: SensorToS3Archive
            └─► S3: wildfire-sensor-archive-503767748192
                └─► Archival largo plazo (Glacier después de 90 días)

EventBridge (cada 5 minutos)
    │
    └─► Lambda: wildfire-ml-processor
        ├─► Lee última hora de datos de DynamoDB
        ├─► Calcula estadísticas (mean, stddev)
        ├─► Detecta anomalías usando Z-score (threshold: 2.5σ)
        ├─► Clasifica severidad (LOW, MODERATE, HIGH, CRITICAL)
        └─► DynamoDB: wildfire-alerts

API Gateway: https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
    │
    ├─► GET /realtime
    │   └─► Lambda: wildfire-api-realtime
    │       └─► Query últimos 5 minutos de DynamoDB
    │
    ├─► GET /historical?from={iso}&to={iso}&interval={5m|1h|1d}
    │   └─► Lambda: wildfire-api-historical
    │       └─► Query rango de fechas + agregación por intervalo
    │
    └─► GET /alerts?deviceId={id}&severity={level}
        └─► Lambda: wildfire-api-alerts
            └─► Query anomalías detectadas por ML

Frontend React (Dashboard)
    └─► Consume API REST con X-Api-Key authentication
```

---

## 🎯 Recursos AWS Desplegados

### 1. IAM Roles (4 roles)

| Role | Propósito | Permisos |
|------|-----------|----------|
| `IoTDynamoDBRole` | IoT → DynamoDB | dynamodb:PutItem, dynamodb:BatchWriteItem |
| `IoTS3Role` | IoT → S3 | s3:PutObject |
| `IoTLambdaRole` | IoT → Lambda | lambda:InvokeFunction |
| `WildfireLambdaRole` | Lambda execution | DynamoDB R/W, S3 R/W, CloudWatch Logs, X-Ray |

### 2. DynamoDB Tables (2 tablas)

#### wildfire-sensor-data
- **Tipo**: Tabla principal de datos de sensores
- **Partition Key**: `device_id` (String)
- **Sort Key**: `timestamp` (Number, epoch milliseconds)
- **TTL**: Activado en campo `ttl` (90 días automático)
- **Billing**: PAY_PER_REQUEST
- **Datos almacenados**:
  - Raw: `device_id`, `temperature`, `humidity`, `pressure`, `gas_concentration`, `timestamp`, `ttl`
  - Enriched: Incluye además `heat_index`, `dew_point`, `risk_score`, `enriched_at`

#### wildfire-alerts
- **Tipo**: Tabla de alertas/anomalías ML
- **Partition Key**: `alert_id` (String, UUID)
- **Sort Key**: `timestamp` (Number)
- **TTL**: Activado (30 días)
- **Billing**: PAY_PER_REQUEST
- **Estructura**:
```json
{
  "alert_id": "uuid-v4",
  "timestamp": 1764143735326,
  "device_id": "sensor_1",
  "anomaly_type": "TEMP_SPIKE" | "TEMP_DROP" | "HUMIDITY_SPIKE" | "GAS_SPIKE" | ...,
  "severity": "LOW" | "MODERATE" | "HIGH" | "CRITICAL",
  "metrics": {
    "metric_name": "temperature",
    "current_value": 35.2,
    "mean": 28.5,
    "stddev": 2.1,
    "z_score": 3.19
  },
  "ttl": 1772000000,
  "detected_at": "2025-11-26T08:00:00.000Z"
}
```

### 3. S3 Bucket

**Nombre**: `wildfire-sensor-archive-503767748192`
- **Propósito**: Archival largo plazo de todos los mensajes MQTT
- **Lifecycle**: Transición a Glacier Flexible Retrieval después de 90 días
- **Formato**: JSON files con UUID como key
- **Tamaño promedio**: ~90 bytes por mensaje

### 4. IoT Rules (3 reglas)

| Rule Name | SQL Statement | Action |
|-----------|---------------|--------|
| `SensorToDynamoDB` | `SELECT id as device_id, temp as temperature, hum as humidity, press as pressure, gas as gas_concentration, timestamp() as timestamp, (timestamp() + 7776000000) as ttl FROM 'thread/sensores'` | DynamoDB PutItem |
| `SensorToEnricher` | `SELECT * FROM 'thread/sensores'` | Lambda Invoke |
| `SensorToS3Archive` | `SELECT *, timestamp() as ts FROM 'thread/sensores'` | S3 PutObject |

### 5. Lambda Functions (5 funciones)

#### wildfire-data-enricher
- **Runtime**: Node.js 20.x
- **Memory**: 256 MB
- **Timeout**: 30s
- **Trigger**: IoT Rule `SensorToEnricher`
- **Función**:
  - Calcula `heat_index` usando fórmula de Steadman
  - Calcula `dew_point` usando fórmula de Magnus
  - Calcula `risk_score` (0-100) basado en temp, humidity, gas
  - Escribe a DynamoDB con datos enriquecidos
- **Fórmulas**:
  ```javascript
  // Heat Index (Steadman)
  heatIndex = c1 + c2*T + c3*RH + c4*T*RH + c5*T² + c6*RH² + c7*T²*RH + c8*T*RH² + c9*T²*RH²

  // Dew Point (Magnus)
  dewPoint = (243.04 * (ln(RH/100) + ((17.625*T)/(243.04+T)))) /
             (17.625 - (ln(RH/100) + ((17.625*T)/(243.04+T))))

  // Risk Score (0-100)
  tempFactor = min(35, max(0, (temp - 20) * 1.75))      // Max 35 puntos
  humidityFactor = min(35, max(0, (80 - humidity) * 0.875))  // Max 35 puntos
  gasFactor = min(30, max(0, gas * 0.06))               // Max 30 puntos
  riskScore = round(tempFactor + humidityFactor + gasFactor)
  ```

#### wildfire-ml-processor
- **Runtime**: Node.js 20.x
- **Memory**: 512 MB
- **Timeout**: 60s
- **Trigger**: EventBridge Rule (cada 5 minutos)
- **Algoritmo**: Z-score anomaly detection
  1. **Query datos**: Última hora de readings de DynamoDB
  2. **Calcular stats**: Mean y StdDev por device_id y métrica
  3. **Latest readings**: Últimos 5 minutos
  4. **Calculate Z-score**: `z = (current_value - mean) / stddev`
  5. **Threshold**: Si `|z| > 2.5`, marcar como anomalía
  6. **Severity levels**:
     - LOW: 2.5 - 3.0 sigma (99.4% de datos normales fuera)
     - MODERATE: 3.0 - 3.5 sigma (99.73%)
     - HIGH: 3.5 - 4.0 sigma (99.95%)
     - CRITICAL: > 4.0 sigma (99.99%)
  7. **Store**: Guardar anomalías en tabla `wildfire-alerts`
- **Métricas monitoreadas**: temperature, humidity, gas_concentration, risk_score

#### wildfire-api-realtime
- **Runtime**: Node.js 20.x
- **Memory**: 256 MB
- **Timeout**: 30s
- **Endpoint**: `GET /realtime?deviceId={id}`
- **Función**: Query últimos 5 minutos de sensor data
- **Response**:
```json
{
  "readings": [
    {
      "device_id": "sensor_1",
      "temperature": 28.5,
      "humidity": 45,
      "pressure": 1015,
      "gas_concentration": 450,
      "heat_index": 28.54,
      "dew_point": 15.39,
      "risk_score": 73,
      "timestamp": 1764143735326,
      "enriched_at": "2025-11-26T07:55:35.326Z"
    }
  ],
  "count": 1,
  "timestamp": "2025-11-26T08:00:00.000Z"
}
```

#### wildfire-api-historical
- **Runtime**: Node.js 20.x
- **Memory**: 256 MB
- **Timeout**: 30s
- **Endpoint**: `GET /historical?from={iso}&to={iso}&interval={5m|15m|1h|6h|1d}&deviceId={id}`
- **Función**: Query rango histórico con agregación
- **Aggregation**: Calcula avg, min, max, count por intervalo de tiempo
- **Response**:
```json
{
  "data": [
    {
      "device_id": "sensor_1",
      "time": "2025-11-26T07:00:00.000Z",
      "timestamp": 1764140400000,
      "metrics": {
        "temperature": { "avg": 28.5, "max": 30.1, "min": 27.2, "count": 12 },
        "humidity": { "avg": 45.3, "max": 48.0, "min": 42.5, "count": 12 },
        "gas_concentration": { "avg": 445.2, "max": 480, "min": 410, "count": 12 },
        "risk_score": { "avg": 72.5, "max": 78, "min": 68, "count": 12 }
      },
      "sample_count": 12
    }
  ],
  "count": 1,
  "query": {
    "from": "2025-11-26T07:00:00Z",
    "to": "2025-11-26T08:00:00Z",
    "interval": "1h",
    "deviceId": "sensor_1"
  }
}
```

#### wildfire-api-alerts
- **Runtime**: Node.js 20.x
- **Memory**: 256 MB
- **Timeout**: 30s
- **Endpoint**: `GET /alerts?deviceId={id}&severity={LOW|MODERATE|HIGH|CRITICAL}&limit={n}`
- **Función**: Query anomalías detectadas por ML
- **Response**:
```json
{
  "data": [
    {
      "alert_id": "uuid",
      "timestamp": 1764143735326,
      "device_id": "sensor_1",
      "anomaly_type": "TEMP_SPIKE",
      "severity": "HIGH",
      "metrics": {
        "metric_name": "temperature",
        "current_value": 35.2,
        "mean": 28.5,
        "stddev": 2.1,
        "z_score": 3.19
      },
      "detected_at": "2025-11-26T08:00:00.000Z"
    }
  ],
  "count": 1,
  "stats": {
    "total": 1,
    "bySeverity": { "LOW": 0, "MODERATE": 0, "HIGH": 1, "CRITICAL": 0 },
    "byType": { "TEMP_SPIKE": 1 }
  }
}
```

### 6. API Gateway

**REST API ID**: `hutgdr9cdb`
**Stage**: `prod`
**Base URL**: `https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod`

#### Endpoints

| Method | Path | Lambda | Auth |
|--------|------|--------|------|
| GET | `/realtime` | wildfire-api-realtime | API Key |
| GET | `/historical` | wildfire-api-historical | API Key |
| GET | `/alerts` | wildfire-api-alerts | API Key |

#### API Key

**Key Name**: `WildfireSensorAPIKey`
**Key Value**: `uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC`

**Usage Plan**:
- Name: `WildfireSensorUsagePlan`
- Throttle: 50 requests/second (burst: 100)
- Quota: 10,000 requests/day

#### Testing

```bash
# Realtime data
curl -X GET \
  "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/realtime" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"

# Historical data (last hour)
curl -X GET \
  "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/historical?from=2025-11-26T07:00:00Z&to=2025-11-26T08:00:00Z&interval=1h" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"

# Alerts
curl -X GET \
  "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/alerts" \
  -H "x-api-key: uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC"
```

### 7. EventBridge

**Rule Name**: `wildfire-ml-processor-schedule`
**Schedule**: `rate(5 minutes)`
**Target**: Lambda `wildfire-ml-processor`
**Purpose**: Ejecutar detección de anomalías cada 5 minutos

### 8. CloudWatch

#### Log Groups (automáticos)
- `/aws/lambda/wildfire-data-enricher`
- `/aws/lambda/wildfire-ml-processor`
- `/aws/lambda/wildfire-api-realtime`
- `/aws/lambda/wildfire-api-historical`
- `/aws/lambda/wildfire-api-alerts`

#### Custom Metrics (Namespace: WildfireSensor)
- `TotalAnomaliesDetected` - Contador de anomalías por ejecución ML
- `AnomaliesDetected` - Por device_id y anomaly_type
- `MLProcessorSuccess` / `MLProcessorFailure` - Success rate del ML processor
- `APILatency` - Latencia por endpoint (realtime, historical, alerts)
- `APIErrors` - Errores por endpoint

#### X-Ray Tracing
Activo en todas las Lambdas para distributed tracing

---

## 📈 Costos Estimados (mensual)

### Escenario: 5 sensores, 1 lectura/minuto cada uno

| Servicio | Volumen | Costo Estimado |
|----------|---------|----------------|
| **DynamoDB** | 216,000 writes/mes, 50,000 reads/mes | $0.27 write + $0.006 read = **$0.28/mes** |
| **Lambda** | 225,000 invocations/mes, 512MB avg | $0.20 requests + $0.50 compute = **$0.70/mes** |
| **API Gateway** | 10,000 requests/mes | **$0.035/mes** |
| **S3** | 216,000 objects (19.4 MB), Glacier después 90d | $0.023 storage + $0.005 PUT = **$0.03/mes** |
| **IoT Core** | 216,000 messages/mes | $0.108 (primeros 1M gratis en 12 meses) = **$0.11/mes** |
| **EventBridge** | 8,640 invocations/mes | Gratis (< 1M) = **$0/mes** |
| **CloudWatch Logs** | ~100 MB/mes | $0.05 ingestion + $0.03 storage = **$0.08/mes** |
| **X-Ray** | 225,000 traces/mes | $0.50 (primero 100K gratis) = **$0.63/mes** |
| **TOTAL** | | **≈ $1.88/mes** |

**Con Free Tier (12 meses)**:
- Lambda: 1M requests + 400,000 GB-s compute gratuitos
- DynamoDB: 25 GB storage + 25 WCU/RCU gratuitos
- API Gateway: 1M requests gratuitos/mes (12 meses)
- S3: 5 GB storage + 20,000 GET + 2,000 PUT gratuitos
- IoT Core: 500,000 messages/mes gratuitos (12 meses)

**Costo real con Free Tier**: **< $0.50/mes en el primer año**

---

## 🔧 Troubleshooting

### Ver logs de Lambda
```bash
# Data enricher
aws logs tail /aws/lambda/wildfire-data-enricher --follow --region us-east-2

# ML processor
aws logs tail /aws/lambda/wildfire-ml-processor --follow --region us-east-2

# API realtime
aws logs tail /aws/lambda/wildfire-api-realtime --follow --region us-east-2
```

### Verificar datos en DynamoDB
```bash
# Scan sensor data (últimos items)
aws dynamodb scan \
  --table-name wildfire-sensor-data \
  --limit 5 \
  --region us-east-2

# Query por device_id específico
aws dynamodb query \
  --table-name wildfire-sensor-data \
  --key-condition-expression "device_id = :id" \
  --expression-attribute-values '{":id":{"S":"sensor_1"}}' \
  --limit 10 \
  --region us-east-2

# Scan alerts
aws dynamodb scan \
  --table-name wildfire-alerts \
  --limit 10 \
  --region us-east-2
```

### Verificar archivos en S3
```bash
# Listar archivos
aws s3 ls s3://wildfire-sensor-archive-503767748192/ --region us-east-2

# Ver contenido de un archivo
aws s3 cp s3://wildfire-sensor-archive-503767748192/{filename}.json - --region us-east-2
```

### Invocar Lambda manualmente
```bash
# Test ML processor
aws lambda invoke \
  --function-name wildfire-ml-processor \
  --region us-east-2 \
  /tmp/ml-response.json

cat /tmp/ml-response.json
```

### Publicar mensaje de prueba
```bash
# Crear payload
echo '{"id":"sensor_test","temp":32.0,"hum":30.0,"press":1010.0,"gas":600.0}' > /tmp/test.json

# Convertir a base64
PAYLOAD=$(python3 -c "import json, base64; print(base64.b64encode(open('/tmp/test.json','rb').read()).decode())")

# Publicar
aws iot-data publish \
  --topic "thread/sensores" \
  --payload "$PAYLOAD" \
  --region us-east-2
```

### Verificar CloudWatch Metrics
```bash
# Ver métricas de anomalías
aws cloudwatch get-metric-statistics \
  --namespace WildfireSensor \
  --metric-name TotalAnomaliesDetected \
  --start-time $(date -u -d '1 hour ago' +%Y-%m-%dT%H:%M:%S) \
  --end-time $(date -u +%Y-%m-%dT%H:%M:%S) \
  --period 300 \
  --statistics Sum \
  --region us-east-2
```

---

## 🎓 Explicación del Sistema (Para Aprender)

### ¿Por qué DynamoDB en lugar de Timestream?

**Timestream** fue la elección inicial porque está optimizado para time-series data (datos de sensores). Sin embargo:
- Amazon anunció "end-of-sale" en LiveAnalytics - ya no acepta nuevos clientes
- Alternativa: Timestream for InfluxDB (más caro, más complejo)
- **Solución**: Migrar a DynamoDB

**DynamoDB** funciona bien para nuestro caso porque:
- Partition key = `device_id` → datos de cada sensor se agrupan físicamente
- Sort key = `timestamp` → ordenamiento automático por tiempo
- TTL integrado → borra datos antiguos automáticamente (90 días)
- Pay-per-request → no pagamos por capacidad no usada
- Query eficiente con KeyConditionExpression

### ¿Cómo funciona el ML Processor?

No es "Machine Learning" tradicional (no hay entrenamiento de modelo), sino **Anomaly Detection estadística**:

1. **Z-score** mide cuántas desviaciones estándar está un valor de la media:
   ```
   z = (valor_actual - media) / desviación_estándar
   ```

2. **Distribución Normal**:
   - 68% de datos están dentro de ±1σ (1 desviación estándar)
   - 95% dentro de ±2σ
   - 99.7% dentro de ±3σ
   - Si |z| > 2.5, el valor está fuera del 99.4% de datos "normales" → ANOMALÍA

3. **Ejemplo**:
   - Sensor normalmente mide 25°C con variación de ±2°C (stddev = 2)
   - De repente mide 32°C
   - Z-score = (32 - 25) / 2 = 3.5
   - 3.5 > 2.5 → **ANOMALÍA HIGH**

4. **Ventajas**:
   - No necesita datos históricos de entrenamiento
   - Se adapta automáticamente a cada sensor (cada device_id tiene su propia media/stddev)
   - Funciona en tiempo real
   - Detecta tanto spikes (subidas) como drops (caídas)

### ¿Por qué Lambda en lugar de EC2?

**Lambda** es "serverless" - no tienes que administrar servidores:
- Escalamiento automático (0 a 1000s instancias)
- Pagas solo por tiempo de ejecución (milisegundos)
- AWS se encarga de parches, updates, disponibilidad
- Integración nativa con IoT, DynamoDB, EventBridge

**EC2** sería más caro y complejo:
- Tendrías que mantener servidor 24/7 (incluso sin tráfico)
- Auto-scaling manual
- Parches de seguridad manuales
- Costo mínimo ~$10/mes (t2.micro)

### ¿Por qué 3 IoT Rules en paralelo?

Las IoT Rules son "event-driven" - cuando llega un mensaje, disparan múltiples acciones **en paralelo**:

1. **SensorToDynamoDB**: Storage inmediato de datos raw
   - Si Lambda falla, al menos tenemos los datos crudos
   - Permite queries rápidos sin procesar

2. **SensorToEnricher**: Enriquecimiento de datos
   - Calcula métricas derivadas (heat_index, risk_score)
   - Si falla, datos raw siguen guardados (rule #1)
   - Escribe segunda fila en DynamoDB con datos enriquecidos

3. **SensorToS3Archive**: Backup/archival
   - Compliance: datos originales sin modificar
   - Recovery: si DynamoDB falla, tenemos backup en S3
   - Análisis futuro: data lake para Big Data analytics

**Redundancia + Especialización = Sistema robusto**

### ¿Por qué API Gateway en lugar de Lambda URLs?

**Lambda Function URLs**:
- Simple: 1 URL por función
- Sin throttling/rate limiting
- Sin API keys
- Sin usage plans
- Sin logging avanzado

**API Gateway**:
- 1 dominio, múltiples endpoints (/realtime, /historical, /alerts)
- Throttling: protege contra DDoS
- API Keys: autenticación simple
- Usage Plans: limita requests por cliente
- Request/Response transformation
- CORS automático
- CloudWatch integration
- Caché (opcional)

Para producción, API Gateway es estándar de la industria.

### ¿Cómo funciona X-Ray Tracing?

**X-Ray** es "distributed tracing" - sigue una request a través de múltiples servicios:

```
Request → API Gateway (150ms)
           └→ Lambda (120ms)
               ├→ DynamoDB Query (45ms)
               ├→ DynamoDB Query (38ms)
               └→ Response (5ms)
```

**Trace ID** único viaja con cada request. Permite:
- Ver qué servicio es lento (bottleneck)
- Detectar errores en cadena
- Debugging de sistemas distribuidos
- Service map visual

### ¿Qué es CloudWatch Embedded Metric Format?

Logs normales:
```javascript
console.log("Anomaly detected, count: 5");
```

**EMF (Embedded Metric Format)**:
```javascript
console.log(JSON.stringify({
  "_aws": {
    "CloudWatchMetrics": [{
      "Namespace": "WildfireSensor",
      "Metrics": [{"Name": "AnomaliesDetected", "Unit": "Count"}]
    }]
  },
  "AnomaliesDetected": 5
}));
```

CloudWatch automáticamente:
1. Parsea el log
2. Crea metric `WildfireSensor/AnomaliesDetected`
3. Permite crear alarmas: "Alert si AnomaliesDetected > 10"
4. Dashboards con gráficas

**Sin costo extra** - métricas custom normalmente cuestan $0.30/mes cada una, EMF es gratis.

---

## 📱 Frontend (Pendiente)

El frontend React está configurado en `/home/Roberto800/Projects/Thread_BR/aws-wildfire-dashboard/frontend` con:

**Archivo `.env` creado**:
```env
REACT_APP_API_ENDPOINT=https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
REACT_APP_API_KEY=uys3PLl6rZ1YhN8XwsI601d4SEFwlH4L3mpmnZoC
REACT_APP_REFRESH_INTERVAL=5000
REACT_APP_MAX_DEVICES=20
```

### Deployment manual del Frontend

**Opción 1: AWS Amplify Hosting (Recomendado)**

```bash
# Instalar Amplify CLI
npm install -g @aws-amplify/cli

# Configurar (si no lo has hecho)
amplify configure

# Desde la carpeta frontend
cd /path/to/aws-wildfire-dashboard/frontend

# Inicializar Amplify
amplify init
# Name: wildfire-dashboard
# Environment: prod
# Editor: VS Code (o tu preferencia)
# Type: JavaScript
# Framework: React
# Source: src
# Distribution: build
# Build: npm run build
# Start: npm start

# Agregar hosting
amplify add hosting
# Select: Hosting with Amplify Console
# Type: Manual deployment

# Publicar
amplify publish
```

**Opción 2: S3 + CloudFront**

```bash
cd /path/to/aws-wildfire-dashboard/frontend

# Build (desde Linux nativo o Mac, no WSL)
npm install
npm run build

# Crear bucket S3
aws s3 mb s3://wildfire-dashboard-frontend --region us-east-2

# Configurar como website
aws s3 website s3://wildfire-dashboard-frontend \
  --index-document index.html \
  --error-document index.html

# Subir archivos
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

# URL: http://wildfire-dashboard-frontend.s3-website.us-east-2.amazonaws.com
```

**Opción 3: Vercel (más simple)**

```bash
# Instalar Vercel CLI
npm install -g vercel

# Desde carpeta frontend
cd /path/to/aws-wildfire-dashboard/frontend

# Deploy
vercel --prod

# Vercel te dará una URL: https://wildfire-dashboard-xxxxx.vercel.app
```

### Problema con WSL

El error que encontramos:
```
Error: Cannot find module 'C:\Windows\package.json'
```

Causa: `react-scripts` usa `cmd.exe` en WSL, que interpreta paths incorrectamente.

Soluciones:
1. **Usar Linux nativo** (Ubuntu VM o dual boot)
2. **Usar Docker**:
```bash
cd /path/to/aws-wildfire-dashboard/frontend

# Build con Docker
docker run --rm -v $(pwd):/app -w /app node:20 bash -c "npm install && npm run build"

# Archivos en ./build/
```
3. **Usar Windows nativo** (Node.js instalado en Windows, no en WSL)

---

## ✅ Checklist de Verificación

### Backend (✅ COMPLETO)
- [x] IAM Roles creados
- [x] DynamoDB tables creadas (sensor-data + alerts)
- [x] S3 bucket creado
- [x] IoT Rules creadas (3)
- [x] Lambda data-enricher desplegada
- [x] Lambda ml-processor desplegada
- [x] Lambda api-realtime desplegada
- [x] Lambda api-historical desplegada
- [x] Lambda api-alerts desplegada
- [x] EventBridge rule configurada
- [x] API Gateway con 3 endpoints
- [x] API Key generada
- [x] Usage Plan configurado
- [x] X-Ray tracing activado
- [x] CloudWatch metrics EMF configurado

### Tests Realizados (✅ VERIFICADO)
- [x] Publicar mensaje MQTT de prueba
- [x] Verificar datos en DynamoDB (raw + enriched)
- [x] Verificar archivo en S3
- [x] Risk score calculado correctamente (73 para datos test)
- [x] API /realtime retorna datos
- [x] API /historical retorna agregaciones
- [x] API /alerts retorna (vacío inicialmente, correcto)

### Frontend (⚠️ PENDIENTE)
- [x] Archivo .env configurado con API credentials
- [ ] Build exitoso (bloqueado por WSL)
- [ ] Deploy a hosting (pendiente de build)

---

## 🚀 Próximos Pasos

1. **Desplegar Frontend** (ver sección "Deployment manual del Frontend" arriba)

2. **Test End-to-End**:
   - ESP32 publica datos reales
   - Verificar en dashboard que aparecen
   - Esperar 5-10 minutos para primera ejecución de ML processor
   - Verificar si aparecen anomalías

3. **Configurar Alarmas** (opcional):
```bash
# Alarma cuando hay anomalías CRITICAL
aws cloudwatch put-metric-alarm \
  --alarm-name wildfire-critical-anomalies \
  --alarm-description "Alert on CRITICAL severity anomalies" \
  --metric-name AnomaliesDetected \
  --namespace WildfireSensor \
  --statistic Sum \
  --period 300 \
  --evaluation-periods 1 \
  --threshold 1 \
  --comparison-operator GreaterThanOrEqualToThreshold \
  --dimensions Name=Severity,Value=CRITICAL \
  --region us-east-2
```

4. **Optimizaciones futuras**:
   - Agregar GSI en wildfire-alerts por device_id + timestamp
   - Implementar DynamoDB Streams → EventBridge → SNS para notificaciones real-time
   - CloudFront delante de API Gateway para caché
   - Cognito para autenticación de usuarios (en lugar de API Key)

---

## 📚 Recursos de Aprendizaje

### Conceptos clave cubiertos:
- **Serverless Architecture**: Lambda, API Gateway, DynamoDB
- **Event-Driven Systems**: IoT Rules, EventBridge, Lambda triggers
- **Time-Series Data**: DynamoDB con timestamp como sort key, TTL
- **Anomaly Detection**: Z-score, statistical methods
- **Distributed Tracing**: X-Ray
- **Observability**: CloudWatch Logs, Metrics, Alarms
- **RESTful APIs**: API Gateway, Lambda integration
- **Data Enrichment**: ETL pipeline con Lambda
- **Data Lifecycle**: Hot storage (DynamoDB) → Cold storage (S3 Glacier)

### Documentación AWS relevante:
- DynamoDB Best Practices: https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/best-practices.html
- Lambda Best Practices: https://docs.aws.amazon.com/lambda/latest/dg/best-practices.html
- IoT Rules: https://docs.aws.amazon.com/iot/latest/developerguide/iot-rules.html
- API Gateway: https://docs.aws.amazon.com/apigateway/latest/developerguide/

---

**Sistema desplegado por**: Claude Code
**Fecha**: 2025-11-26
**Total Lambda Functions**: 5
**Total API Endpoints**: 3
**Costo estimado**: < $2/mes (< $0.50 con Free Tier)
