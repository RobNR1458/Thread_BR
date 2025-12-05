const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient, ScanCommand, PutCommand } = require('@aws-sdk/lib-dynamodb');
const { v4: uuidv4 } = require('uuid');
const AWSXRay = require('aws-xray-sdk-core');

const ddbClient = AWSXRay.captureAWSv3Client(new DynamoDBClient({ region: 'us-east-2' }));
const docClient = DynamoDBDocumentClient.from(ddbClient);

const SENSOR_DATA_TABLE = process.env.SENSOR_DATA_TABLE || 'wildfire-sensor-data';
const ALERTS_TABLE = process.env.ALERTS_TABLE || 'wildfire-alerts';
const Z_SCORE_THRESHOLD = parseFloat(process.env.Z_SCORE_THRESHOLD || '2.5');

/**
 * ML Processor Lambda - Anomaly Detection & Risk Scoring
 *
 * Triggered by: EventBridge (every 5 minutes)
 *
 * Algorithm:
 * 1. Query last 1 hour of enriched data from DynamoDB
 * 2. Calculate mean and standard deviation for each metric per device
 * 3. Get latest readings (last 5 minutes)
 * 4. Calculate Z-scores: z = (value - mean) / stddev
 * 5. If |z| > 2.5, flag as anomaly
 * 6. Store anomalies in DynamoDB alerts table with severity level
 * 7. Emit CloudWatch metrics
 */
exports.handler = async (event) => {
  const subsegment = AWSXRay.getSegment()?.addNewSubsegment('MLProcessor');

  console.log('ML Processor started', { event });

  try {
    const anomalies = [];

    // Get all sensor data from last hour
    const oneHourAgo = Date.now() - (60 * 60 * 1000);
    const fiveMinutesAgo = Date.now() - (5 * 60 * 1000);

    const scanResult = await docClient.send(new ScanCommand({
      TableName: SENSOR_DATA_TABLE,
      FilterExpression: '#ts >= :one_hour_ago',
      ExpressionAttributeNames: {
        '#ts': 'timestamp'
      },
      ExpressionAttributeValues: {
        ':one_hour_ago': oneHourAgo
      }
    }));

    const allReadings = scanResult.Items || [];

    if (allReadings.length === 0) {
      console.log('No data found in last hour, skipping anomaly detection');
      return {
        statusCode: 200,
        body: JSON.stringify({ anomaliesDetected: 0, message: 'No data to process' })
      };
    }

    // Group readings by device_id
    const deviceReadings = {};
    allReadings.forEach(reading => {
      if (!deviceReadings[reading.device_id]) {
        deviceReadings[reading.device_id] = [];
      }
      deviceReadings[reading.device_id].push(reading);
    });

    // Process each device
    for (const [deviceId, readings] of Object.entries(deviceReadings)) {
      // Calculate statistics for the last hour
      const stats = calculateStatistics(readings);

      // Get latest readings (last 5 minutes)
      const latestReadings = readings.filter(r => r.timestamp >= fiveMinutesAgo);

      if (latestReadings.length === 0) {
        console.log(`No recent data for device ${deviceId}`);
        continue;
      }

      // Check each latest reading for anomalies
      for (const reading of latestReadings) {
        // Check temperature anomalies
        if (reading.temperature && stats.temperature.count > 5) {
          const tempAnomaly = checkAnomaly(
            reading,
            'temperature',
            stats.temperature,
            'TEMP',
            deviceId
          );
          if (tempAnomaly) anomalies.push(tempAnomaly);
        }

        // Check humidity anomalies
        if (reading.humidity && stats.humidity.count > 5) {
          const humAnomaly = checkAnomaly(
            reading,
            'humidity',
            stats.humidity,
            'HUMIDITY',
            deviceId
          );
          if (humAnomaly) anomalies.push(humAnomaly);
        }

        // Check gas concentration anomalies
        if (reading.gas_concentration && stats.gas_concentration.count > 5) {
          const gasAnomaly = checkAnomaly(
            reading,
            'gas_concentration',
            stats.gas_concentration,
            'GAS',
            deviceId
          );
          if (gasAnomaly) anomalies.push(gasAnomaly);
        }

        // Check risk_score anomalies (if enriched data available)
        if (reading.risk_score && stats.risk_score.count > 5) {
          const riskAnomaly = checkAnomaly(
            reading,
            'risk_score',
            stats.risk_score,
            'RISK',
            deviceId
          );
          if (riskAnomaly) anomalies.push(riskAnomaly);
        }
      }
    }

    // Save anomalies to DynamoDB
    for (const anomaly of anomalies) {
      await docClient.send(new PutCommand({
        TableName: ALERTS_TABLE,
        Item: anomaly
      }));

      // Log individual anomaly metric
      console.log(JSON.stringify({
        "_aws": {
          "CloudWatchMetrics": [{
            "Namespace": "WildfireSensor",
            "Dimensions": [["DeviceId", "AnomalyType"]],
            "Metrics": [{ "Name": "AnomaliesDetected", "Unit": "Count" }]
          }]
        },
        "DeviceId": anomaly.device_id,
        "AnomalyType": anomaly.anomaly_type,
        "AnomaliesDetected": 1,
        "level": "WARN",
        "message": `Anomaly detected: ${anomaly.anomaly_type} for ${anomaly.device_id}`,
        "severity": anomaly.severity,
        "zScore": anomaly.metrics.z_score
      }));

      console.log(`Anomaly detected: ${JSON.stringify(anomaly)}`);
    }

    // Log summary
    console.log(`ML processing complete: ${anomalies.length} anomalies detected`);

    // Emit summary metric
    console.log(JSON.stringify({
      "_aws": {
        "CloudWatchMetrics": [{
          "Namespace": "WildfireSensor",
          "Metrics": [
            { "Name": "TotalAnomaliesDetected", "Unit": "Count" },
            { "Name": "MLProcessorSuccess", "Unit": "Count" }
          ]
        }]
      },
      "TotalAnomaliesDetected": anomalies.length,
      "MLProcessorSuccess": 1,
      "level": "INFO"
    }));

    if (subsegment) {
      subsegment.addAnnotation('anomaliesDetected', anomalies.length);
      subsegment.close();
    }

    return {
      statusCode: 200,
      body: JSON.stringify({
        anomaliesDetected: anomalies.length,
        anomalies: anomalies
      })
    };

  } catch (error) {
    console.error('Error in ML processor:', error);

    // Log error metric
    console.log(JSON.stringify({
      "_aws": {
        "CloudWatchMetrics": [{
          "Namespace": "WildfireSensor",
          "Metrics": [{ "Name": "MLProcessorFailure", "Unit": "Count" }]
        }]
      },
      "MLProcessorFailure": 1,
      "level": "ERROR",
      "error": error.message
    }));

    if (subsegment) {
      subsegment.addError(error);
      subsegment.close();
    }

    throw error;
  }
};

/**
 * Calculate statistics (mean, stddev) for all metrics in readings
 */
function calculateStatistics(readings) {
  const metrics = ['temperature', 'humidity', 'gas_concentration', 'risk_score'];
  const stats = {};

  metrics.forEach(metric => {
    const values = readings
      .map(r => r[metric])
      .filter(v => v !== undefined && v !== null && !isNaN(v));

    if (values.length > 0) {
      const mean = values.reduce((sum, v) => sum + v, 0) / values.length;
      const variance = values.reduce((sum, v) => sum + Math.pow(v - mean, 2), 0) / values.length;
      const stddev = Math.sqrt(variance);

      stats[metric] = {
        mean,
        stddev,
        count: values.length,
        min: Math.min(...values),
        max: Math.max(...values)
      };
    } else {
      stats[metric] = {
        mean: 0,
        stddev: 0,
        count: 0,
        min: 0,
        max: 0
      };
    }
  });

  return stats;
}

/**
 * Check if a reading value is anomalous
 */
function checkAnomaly(reading, metricName, stats, anomalyPrefix, deviceId) {
  const currentValue = reading[metricName];

  // Need at least some variation to detect anomalies
  if (stats.stddev === 0 || stats.count < 5) {
    return null;
  }

  const zScore = (currentValue - stats.mean) / stats.stddev;

  if (Math.abs(zScore) > Z_SCORE_THRESHOLD) {
    const anomalyType = determineAnomalyType(anomalyPrefix, zScore);
    const severity = calculateSeverity(Math.abs(zScore));

    return {
      alert_id: uuidv4(),
      timestamp: reading.timestamp || Date.now(),
      device_id: deviceId,
      anomaly_type: anomalyType,
      severity: severity,
      metrics: {
        metric_name: metricName,
        current_value: parseFloat(currentValue.toFixed(2)),
        mean: parseFloat(stats.mean.toFixed(2)),
        stddev: parseFloat(stats.stddev.toFixed(2)),
        z_score: parseFloat(zScore.toFixed(2))
      },
      ttl: Math.floor(Date.now() / 1000) + (30 * 24 * 60 * 60),  // 30 days
      detected_at: new Date().toISOString()
    };
  }

  return null;
}

/**
 * Determine anomaly type based on metric and Z-score direction
 */
function determineAnomalyType(prefix, zScore) {
  if (prefix === 'TEMP') {
    return zScore > 0 ? 'TEMP_SPIKE' : 'TEMP_DROP';
  } else if (prefix === 'HUMIDITY') {
    return zScore > 0 ? 'HUMIDITY_SPIKE' : 'HUMIDITY_DROP';
  } else if (prefix === 'GAS') {
    return zScore > 0 ? 'GAS_SPIKE' : 'GAS_DROP';
  } else if (prefix === 'RISK') {
    return zScore > 0 ? 'RISK_SPIKE' : 'RISK_DROP';
  }
  return `${prefix}_ANOMALY`;
}

/**
 * Calculate severity level based on Z-score magnitude
 *
 * Severity Levels:
 * - LOW: 2.5 - 3.0 sigma (unusual but not critical)
 * - MODERATE: 3.0 - 3.5 sigma (concerning)
 * - HIGH: 3.5 - 4.0 sigma (serious)
 * - CRITICAL: > 4.0 sigma (extreme)
 */
function calculateSeverity(absZScore) {
  if (absZScore >= 4.0) return 'CRITICAL';
  if (absZScore >= 3.5) return 'HIGH';
  if (absZScore >= 3.0) return 'MODERATE';
  return 'LOW';
}
