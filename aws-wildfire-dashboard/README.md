# 🔥 AWS Wildfire Detection System

Sistema completo de detección temprana de incendios forestales usando Thread Border Router (ESP32-S3), AWS IoT Core, Machine Learning y dashboard web en tiempo real.

---

## 📋 Descripción General

Este proyecto implementa una arquitectura serverless en AWS para:
- **Recolectar** datos de sensores ambientales (temperatura, humedad, presión, gas) vía MQTT
- **Almacenar** datos en DynamoDB (tiempo real) y S3 (archivo histórico)
- **Analizar** con ML para detectar anomalías y calcular riesgo de incendio
- **Visualizar** en dashboard web React con gráficas históricas y alertas

---

## 🏗️ Arquitectura AWS

```
ESP32 Thread BR → AWS IoT Core → DynamoDB + Lambda ML → API Gateway → React Dashboard
                                        ↓
                                    S3 Archive
                                        ↓
                                CloudWatch + X-Ray
```

### Servicios Utilizados

- **AWS IoT Core**: Recepción de mensajes MQTT (topic: `thread/sensores`)
- **Amazon DynamoDB**:
  - `wildfire-sensor-data`: Datos de sensores (90 días TTL)
  - `wildfire-alerts`: Alertas ML (30 días TTL)
- **Amazon S3**: Archivo de largo plazo con transición a Glacier (90 días)
- **AWS Lambda**: 5 funciones serverless
  - `wildfire-data-enricher`: Calcula heat_index, dew_point, risk_score
  - `wildfire-ml-processor`: Detección de anomalías (Z-score)
  - `wildfire-api-realtime`: API para datos recientes
  - `wildfire-api-historical`: API para datos históricos con agregación
  - `wildfire-api-alerts`: API para anomalías detectadas
- **Amazon EventBridge**: Trigger del ML processor cada 5 minutos
- **API Gateway**: API REST con 3 endpoints + API Key authentication
- **CloudWatch + X-Ray**: Monitoreo, logs y distributed tracing

**Región**: `us-east-2` (Ohio)

**Costo estimado**: $1.88/mes (5 sensores) | < $0.50/mes con Free Tier

---

## 📊 Estado del Proyecto

### ✅ Backend - 100% Desplegado y Funcional

- [x] IAM Roles y permisos
- [x] DynamoDB tables (sensor-data + alerts)
- [x] S3 bucket con lifecycle policies
- [x] IoT Rules (3 reglas en paralelo)
- [x] Lambda data-enricher con cálculos de riesgo
- [x] Lambda ml-processor con detección de anomalías
- [x] Lambda API handlers (realtime + historical + alerts)
- [x] EventBridge schedule (cada 5 minutos)
- [x] API Gateway con 3 endpoints
- [x] API Key y Usage Plan
- [x] X-Ray tracing
- [x] CloudWatch metrics (EMF)

**API Base URL**: `https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod`

**API Key**: Ver archivo `frontend/.env`

### ⚠️ Frontend - Pendiente

El frontend está configurado pero falta el build y deployment. Ver **[PENDING-TASKS.md](PENDING-TASKS.md)** para instrucciones completas.

---

## 🚀 Inicio Rápido

### Prerequisitos

1. **ESP32-S3 Border Router** publicando a AWS IoT Core topic `thread/sensores`
2. Payload esperado:
   ```json
   {"id":"sensor_1","temp":28.5,"hum":45.0,"press":1015.0,"gas":450.0}
   ```

### Verificar que el Sistema Funciona

**1. Test API realtime**:
```bash
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/realtime" \
  -H "x-api-key: <TU_API_KEY>"
```

**2. Test API historical** (últimas 24 horas):
```bash
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/historical?from=2025-11-26T00:00:00Z&to=2025-11-26T23:59:59Z&interval=1h" \
  -H "x-api-key: <TU_API_KEY>"
```

**3. Test API alerts**:
```bash
curl "https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod/alerts" \
  -H "x-api-key: <TU_API_KEY>"
```

**4. Verificar datos en DynamoDB**:
```bash
aws dynamodb scan \
  --table-name wildfire-sensor-data \
  --limit 5 \
  --region us-east-2
```

**5. Verificar archivos en S3**:
```bash
aws s3 ls s3://wildfire-sensor-archive-503767748192/ --region us-east-2
```

---

## 📂 Estructura del Proyecto

```
aws-wildfire-dashboard/
├── README.md                        # Este archivo
├── DEPLOYMENT-SUMMARY.md            # 📖 Documentación técnica completa
├── PENDING-TASKS.md                 # ⚠️ Tareas pendientes (frontend)
│
├── backend/
│   ├── iot-rules/                   # IoT Rules (JSON)
│   │   ├── rule-to-dynamodb.json   # Storage directo
│   │   ├── rule-to-lambda-enricher.json  # Enriquecimiento
│   │   └── rule-to-s3-archive.json # Archival
│   │
│   ├── lambda/                      # Funciones Lambda (Node.js 20)
│   │   ├── data-enricher/          # ✅ Desplegada
│   │   ├── ml-processor/           # ✅ Desplegada
│   │   └── api-handlers/
│   │       ├── realtime/           # ✅ Desplegada
│   │       ├── historical/         # ✅ Desplegada
│   │       └── alerts/             # ✅ Desplegada
│   │
│   └── infrastructure/
│       └── cloudformation/          # Templates CloudFormation
│           ├── 01-dynamodb-sensor-data.yaml
│           ├── 02-dynamodb-alerts.yaml
│           ├── 03-s3.yaml
│           └── 04-iam-roles.yaml
│
└── frontend/                        # Aplicación React
    ├── .env                         # ✅ Configurado con API credentials
    ├── src/
    │   ├── components/
    │   │   ├── Dashboard.js
    │   │   ├── SensorCard.js
    │   │   ├── HistoricalChart.js
    │   │   └── AlertsPanel.js
    │   ├── services/
    │   │   └── api.js
    │   └── App.js
    └── package.json
```

---

## 📡 API Endpoints

### Base URL
```
https://hutgdr9cdb.execute-api.us-east-2.amazonaws.com/prod
```

### Autenticación
Todas las requests requieren header:
```
x-api-key: <API_KEY>
```

### Endpoints

#### 1. GET `/realtime`
Obtiene datos de sensores de los últimos 5 minutos.

**Query params** (opcional):
- `deviceId`: Filtrar por sensor específico

**Response**:
```json
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
    "timestamp": 1764143735326,
    "enriched_at": "2025-11-26T07:55:35.326Z"
  }],
  "count": 1
}
```

#### 2. GET `/historical`
Obtiene datos históricos con agregación por intervalos.

**Query params** (requeridos):
- `from`: ISO 8601 timestamp (ej: `2025-11-26T00:00:00Z`)
- `to`: ISO 8601 timestamp

**Query params** (opcionales):
- `interval`: `5m`, `15m`, `1h`, `6h`, `1d` (default: `1h`)
- `deviceId`: Filtrar por sensor

**Response**:
```json
{
  "data": [{
    "device_id": "sensor_1",
    "time": "2025-11-26T07:00:00.000Z",
    "timestamp": 1764140400000,
    "metrics": {
      "temperature": {"avg": 28.5, "max": 30.1, "min": 27.2, "count": 12},
      "humidity": {"avg": 45.3, "max": 48.0, "min": 42.5, "count": 12},
      "gas_concentration": {"avg": 445.2, "max": 480, "min": 410, "count": 12},
      "risk_score": {"avg": 72.5, "max": 78, "min": 68, "count": 12}
    },
    "sample_count": 12
  }],
  "count": 1
}
```

#### 3. GET `/alerts`
Obtiene anomalías detectadas por el ML processor.

**Query params** (opcionales):
- `deviceId`: Filtrar por sensor
- `severity`: `LOW`, `MODERATE`, `HIGH`, `CRITICAL`
- `limit`: Máximo de resultados (default: 50, max: 100)

**Response**:
```json
{
  "data": [{
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
  }],
  "stats": {
    "total": 1,
    "bySeverity": {"LOW": 0, "MODERATE": 0, "HIGH": 1, "CRITICAL": 0},
    "byType": {"TEMP_SPIKE": 1}
  }
}
```

---

## 🧪 Testing

### Simular Mensaje MQTT

```bash
# Crear payload JSON
echo '{"id":"sensor_test","temp":32.0,"hum":30.0,"press":1010.0,"gas":600.0}' > /tmp/test.json

# Convertir a base64
PAYLOAD=$(python3 -c "import json, base64; print(base64.b64encode(open('/tmp/test.json','rb').read()).decode())")

# Publicar a IoT Core
aws iot-data publish \
  --topic "thread/sensores" \
  --payload "$PAYLOAD" \
  --region us-east-2
```

Luego verifica:
1. Datos en DynamoDB (tabla `wildfire-sensor-data`)
2. Archivo en S3 (bucket `wildfire-sensor-archive-*`)
3. API `/realtime` retorna el dato

---

## 🔍 Monitoreo

### CloudWatch Logs

```bash
# Logs del data enricher
aws logs tail /aws/lambda/wildfire-data-enricher --follow --region us-east-2

# Logs del ML processor
aws logs tail /aws/lambda/wildfire-ml-processor --follow --region us-east-2

# Logs de API realtime
aws logs tail /aws/lambda/wildfire-api-realtime --follow --region us-east-2
```

### CloudWatch Metrics

Namespace: `WildfireSensor`

- `TotalAnomaliesDetected`: Contador total de anomalías
- `AnomaliesDetected`: Por device_id y anomaly_type
- `MLProcessorSuccess` / `MLProcessorFailure`: Success rate
- `APILatency`: Latencia por endpoint
- `APIErrors`: Errores por endpoint

### X-Ray Tracing

Todas las Lambdas tienen X-Ray activo para distributed tracing. Ver en:
https://console.aws.amazon.com/xray/home?region=us-east-2

---

## 🎓 Cómo Funciona

### 1. Ingesta de Datos

```
ESP32 publica MQTT → IoT Core recibe → 3 IoT Rules en paralelo:
  1. Rule → DynamoDB (datos raw)
  2. Rule → Lambda enricher → DynamoDB (datos enriquecidos)
  3. Rule → S3 (backup/archival)
```

### 2. Enriquecimiento de Datos

Lambda `data-enricher` calcula:
- **Heat Index** (índice de calor): Fórmula de Steadman
- **Dew Point** (punto de rocío): Fórmula de Magnus
- **Risk Score** (0-100): Combina temperatura, humedad, gas

### 3. Detección de Anomalías

Lambda `ml-processor` ejecuta cada 5 minutos:
1. Query última hora de datos
2. Calcula mean + stddev por sensor y métrica
3. Calcula Z-score para lecturas recientes: `z = (value - mean) / stddev`
4. Si `|z| > 2.5`, marca como anomalía
5. Clasifica severidad:
   - LOW: 2.5 - 3.0σ
   - MODERATE: 3.0 - 3.5σ
   - HIGH: 3.5 - 4.0σ
   - CRITICAL: > 4.0σ

### 4. API y Visualización

API Gateway expone 3 endpoints que las Lambdas consumen desde DynamoDB y retornan JSON para el frontend React.

---

## 💰 Análisis de Costos

### Escenario: 5 sensores, 1 lectura/minuto

| Servicio | Volumen Mensual | Costo |
|----------|-----------------|-------|
| DynamoDB | 216K writes, 50K reads | $0.28 |
| Lambda | 225K invocations | $0.70 |
| API Gateway | 10K requests | $0.04 |
| S3 | 216K objects (20 MB) | $0.03 |
| IoT Core | 216K messages | $0.11 |
| EventBridge | 8.6K invocations | $0.00 |
| CloudWatch Logs | 100 MB | $0.08 |
| X-Ray | 225K traces | $0.63 |
| **TOTAL** | | **$1.88/mes** |

**Con Free Tier**: < $0.50/mes en el primer año

---

## 📚 Documentación Adicional

### 📖 [DEPLOYMENT-SUMMARY.md](DEPLOYMENT-SUMMARY.md)
Documentación técnica completa con:
- Explicación detallada de cada componente
- ¿Por qué DynamoDB? ¿Por qué Lambda? ¿Por qué API Gateway?
- Cómo funciona el ML processor (Z-score)
- Guías de troubleshooting
- Fórmulas matemáticas
- Arquitectura de datos

### ⚠️ [PENDING-TASKS.md](PENDING-TASKS.md)
Tareas pendientes y cómo completarlas:
- Problema del build en WSL
- 5 opciones para desplegar el frontend
- Instrucciones paso a paso
- Verificación de funcionamiento

---

## 🔧 Troubleshooting

### El ML processor no detecta anomalías

Es normal en las primeras horas. El ML necesita:
- Al menos 5 lecturas históricas por sensor
- Variación en los datos (stddev > 0)
- Esperar a que se ejecute (cada 5 minutos)

### La API retorna datos vacíos

Verifica que el ESP32 esté publicando:
```bash
# Ver si hay datos en DynamoDB
aws dynamodb scan --table-name wildfire-sensor-data --limit 5 --region us-east-2
```

### Errores de CORS en el frontend

CORS ya está configurado en API Gateway. Si ves errores:
- Verifica que usas `https://` (no `http://`)
- Verifica que el header es `x-api-key` (minúsculas)

---

## 📄 Licencia

Proyecto educativo de código abierto.

---

## 🤝 Equipo

Desarrollado como parte del proyecto Thread Border Router para detección de incendios forestales.

---

**Estado**: ✅ Backend 100% operativo | ⚠️ Frontend pendiente de deployment

**Última actualización**: 2025-11-26
