const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient, ScanCommand } = require('@aws-sdk/lib-dynamodb');
const AWSXRay = require('aws-xray-sdk-core');

const ddbClient = AWSXRay.captureAWSv3Client(new DynamoDBClient({ region: 'us-east-2' }));
const docClient = DynamoDBDocumentClient.from(ddbClient);

const TABLE_NAME = process.env.DYNAMODB_TABLE || 'wildfire-sensor-data';

/**
 * API Handler: Get Historical Sensor Data with Aggregation
 *
 * Endpoint: GET /historical?deviceId={id}&from={iso}&to={iso}&interval={5m|1h|1d}
 *
 * Query Parameters:
 * - deviceId (optional): Filter by specific device
 * - from (required): Start timestamp (ISO 8601)
 * - to (required): End timestamp (ISO 8601)
 * - interval (optional): Aggregation interval - 5m, 1h, 1d (default: 1h)
 *
 * Returns aggregated time-series data for the specified time range.
 */
exports.handler = async (event) => {
  const startTime = Date.now();

  console.log('Historical API called', { event });

  // Extract and validate query parameters
  const deviceId = event.queryStringParameters?.deviceId;
  const from = event.queryStringParameters?.from;
  const to = event.queryStringParameters?.to;
  const interval = event.queryStringParameters?.interval || '1h';

  // Validate required parameters
  if (!from || !to) {
    return {
      statusCode: 400,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      },
      body: JSON.stringify({
        error: 'Bad Request',
        message: 'Required parameters: from (ISO 8601), to (ISO 8601)'
      })
    };
  }

  // Validate interval
  const validIntervals = ['5m', '15m', '1h', '6h', '1d'];
  if (!validIntervals.includes(interval)) {
    return {
      statusCode: 400,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      },
      body: JSON.stringify({
        error: 'Bad Request',
        message: `Invalid interval. Valid values: ${validIntervals.join(', ')}`
      })
    };
  }

  try {
    // Convert ISO timestamps to epoch milliseconds
    const fromTimestamp = new Date(from).getTime();
    const toTimestamp = new Date(to).getTime();

    if (isNaN(fromTimestamp) || isNaN(toTimestamp)) {
      return {
        statusCode: 400,
        headers: {
          'Content-Type': 'application/json',
          'Access-Control-Allow-Origin': '*'
        },
        body: JSON.stringify({
          error: 'Bad Request',
          message: 'Invalid ISO 8601 timestamp format'
        })
      };
    }

    // Build DynamoDB scan with filter
    let filterExpression = '#ts BETWEEN :from_ts AND :to_ts';
    let expressionAttributeNames = {
      '#ts': 'timestamp'
    };
    let expressionAttributeValues = {
      ':from_ts': fromTimestamp,
      ':to_ts': toTimestamp
    };

    if (deviceId) {
      filterExpression += ' AND device_id = :device_id';
      expressionAttributeValues[':device_id'] = deviceId;
    }

    console.log('Scanning DynamoDB:', { filterExpression, fromTimestamp, toTimestamp, deviceId });

    const result = await docClient.send(new ScanCommand({
      TableName: TABLE_NAME,
      FilterExpression: filterExpression,
      ExpressionAttributeNames: expressionAttributeNames,
      ExpressionAttributeValues: expressionAttributeValues
    }));

    const readings = result.Items || [];

    // Aggregate data based on interval
    const aggregated = aggregateByInterval(readings, interval);

    const duration = Date.now() - startTime;

    // Log API latency metric
    console.log(JSON.stringify({
      "_aws": {
        "CloudWatchMetrics": [{
          "Namespace": "WildfireSensor",
          "Dimensions": [["Endpoint"]],
          "Metrics": [{ "Name": "APILatency", "Unit": "Milliseconds" }]
        }]
      },
      "Endpoint": "historical",
      "APILatency": duration,
      "level": "INFO"
    }));

    return {
      statusCode: 200,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'GET,OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type,X-Api-Key'
      },
      body: JSON.stringify({
        data: aggregated,
        count: aggregated.length,
        query: {
          from: from,
          to: to,
          interval: interval,
          deviceId: deviceId || 'all'
        },
        timestamp: new Date().toISOString()
      })
    };

  } catch (error) {
    console.error('Error querying DynamoDB:', error);

    console.log(JSON.stringify({
      "_aws": {
        "CloudWatchMetrics": [{
          "Namespace": "WildfireSensor",
          "Dimensions": [["Endpoint"]],
          "Metrics": [{ "Name": "APIErrors", "Unit": "Count" }]
        }]
      },
      "Endpoint": "historical",
      "APIErrors": 1,
      "level": "ERROR",
      "error": error.message
    }));

    return {
      statusCode: 500,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      },
      body: JSON.stringify({
        error: 'Internal server error',
        message: error.message
      })
    };
  }
};

/**
 * Aggregate readings by time interval
 */
function aggregateByInterval(readings, interval) {
  // Convert interval to milliseconds
  const intervalMs = {
    '5m': 5 * 60 * 1000,
    '15m': 15 * 60 * 1000,
    '1h': 60 * 60 * 1000,
    '6h': 6 * 60 * 60 * 1000,
    '1d': 24 * 60 * 60 * 1000
  }[interval];

  // Group readings by device and time bucket
  const buckets = {};

  readings.forEach(reading => {
    // Calculate time bucket
    const timeBucket = Math.floor(reading.timestamp / intervalMs) * intervalMs;
    const key = `${reading.device_id}_${timeBucket}`;

    if (!buckets[key]) {
      buckets[key] = {
        device_id: reading.device_id,
        time: new Date(timeBucket).toISOString(),
        timestamp: timeBucket,
        readings: []
      };
    }

    buckets[key].readings.push(reading);
  });

  // Calculate aggregates for each bucket
  const aggregated = Object.values(buckets).map(bucket => {
    const temps = bucket.readings.map(r => r.temperature).filter(v => v !== undefined);
    const hums = bucket.readings.map(r => r.humidity).filter(v => v !== undefined);
    const press = bucket.readings.map(r => r.pressure).filter(v => v !== undefined);
    const gases = bucket.readings.map(r => r.gas_concentration).filter(v => v !== undefined);
    const risks = bucket.readings.map(r => r.risk_score).filter(v => v !== undefined);

    return {
      device_id: bucket.device_id,
      time: bucket.time,
      timestamp: bucket.timestamp,
      metrics: {
        temperature: calculateStats(temps),
        humidity: calculateStats(hums),
        pressure: calculateStats(press),
        gas_concentration: calculateStats(gases),
        risk_score: calculateStats(risks)
      },
      sample_count: bucket.readings.length
    };
  });

  // Sort by time descending
  return aggregated.sort((a, b) => b.timestamp - a.timestamp);
}

/**
 * Calculate statistics for a set of values
 */
function calculateStats(values) {
  if (values.length === 0) {
    return null;
  }

  const avg = values.reduce((sum, v) => sum + v, 0) / values.length;
  const max = Math.max(...values);
  const min = Math.min(...values);

  return {
    avg: parseFloat(avg.toFixed(2)),
    max: parseFloat(max.toFixed(2)),
    min: parseFloat(min.toFixed(2)),
    count: values.length
  };
}
