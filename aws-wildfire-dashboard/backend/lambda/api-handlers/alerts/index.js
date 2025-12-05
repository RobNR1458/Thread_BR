const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient, QueryCommand, ScanCommand } = require('@aws-sdk/lib-dynamodb');
const AWSXRay = require('aws-xray-sdk-core');

const dynamodbClient = AWSXRay.captureAWSv3Client(new DynamoDBClient({ region: 'us-east-2' }));
const dynamodb = DynamoDBDocumentClient.from(dynamodbClient);

const DYNAMODB_TABLE = process.env.DYNAMODB_TABLE || 'wildfire-alerts';

/**
 * API Handler: Get ML-Generated Alerts
 *
 * Endpoint: GET /alerts?deviceId={id}&severity={low|moderate|high|critical}&limit={n}
 *
 * Query Parameters:
 * - deviceId (optional): Filter by specific device
 * - severity (optional): Filter by severity level (LOW, MODERATE, HIGH, CRITICAL)
 * - limit (optional): Maximum number of results (default: 50, max: 100)
 *
 * Returns recent ML-generated anomaly alerts from DynamoDB.
 */
exports.handler = async (event) => {
  const startTime = Date.now();

  console.log('Alerts API called', { event });

  // Extract query parameters
  const deviceId = event.queryStringParameters?.deviceId;
  const severity = event.queryStringParameters?.severity?.toUpperCase();
  const limit = Math.min(parseInt(event.queryStringParameters?.limit || '50'), 100);

  // Validate severity if provided
  const validSeverities = ['LOW', 'MODERATE', 'HIGH', 'CRITICAL'];
  if (severity && !validSeverities.includes(severity)) {
    return {
      statusCode: 400,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      },
      body: JSON.stringify({
        error: 'Bad Request',
        message: `Invalid severity. Valid values: ${validSeverities.join(', ')}`
      })
    };
  }

  try {
    let alerts = [];

    // Build scan params
    const params = {
      TableName: DYNAMODB_TABLE,
      Limit: limit
    };

    // Build filter expression
    const filterParts = [];
    const expressionAttributeValues = {};

    if (deviceId) {
      filterParts.push('device_id = :deviceId');
      expressionAttributeValues[':deviceId'] = deviceId;
    }

    if (severity) {
      filterParts.push('severity = :severity');
      expressionAttributeValues[':severity'] = severity;
    }

    if (filterParts.length > 0) {
      params.FilterExpression = filterParts.join(' AND ');
      params.ExpressionAttributeValues = expressionAttributeValues;
    }

    console.log('Scanning DynamoDB with params:', JSON.stringify(params));

    const response = await dynamodb.send(new ScanCommand(params));
    alerts = response.Items || [];

    // Sort by timestamp descending
    alerts.sort((a, b) => b.timestamp - a.timestamp);

    // Calculate alert statistics
    const stats = {
      total: alerts.length,
      bySeverity: {
        LOW: alerts.filter(a => a.severity === 'LOW').length,
        MODERATE: alerts.filter(a => a.severity === 'MODERATE').length,
        HIGH: alerts.filter(a => a.severity === 'HIGH').length,
        CRITICAL: alerts.filter(a => a.severity === 'CRITICAL').length
      },
      byType: {}
    };

    // Count by anomaly type
    for (const alert of alerts) {
      stats.byType[alert.anomaly_type] = (stats.byType[alert.anomaly_type] || 0) + 1;
    }

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
      "Endpoint": "alerts",
      "APILatency": duration,
      "level": "INFO"
    }));

    return {
      statusCode: 200,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Headers': 'Content-Type,X-Amz-Date,Authorization,X-Api-Key,X-Amz-Security-Token',
        'Access-Control-Allow-Methods': 'GET,OPTIONS'
      },
      body: JSON.stringify({
        data: alerts,
        count: alerts.length,
        stats: stats,
        query: {
          deviceId: deviceId || 'all',
          severity: severity || 'all',
          limit: limit
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
      "Endpoint": "alerts",
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
