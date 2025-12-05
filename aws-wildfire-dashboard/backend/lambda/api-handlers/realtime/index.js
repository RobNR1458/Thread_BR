const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient, QueryCommand, ScanCommand } = require('@aws-sdk/lib-dynamodb');
const AWSXRay = require('aws-xray-sdk-core');

const ddbClient = AWSXRay.captureAWSv3Client(new DynamoDBClient({ region: 'us-east-2' }));
const docClient = DynamoDBDocumentClient.from(ddbClient);

const TABLE_NAME = process.env.DYNAMODB_TABLE || 'wildfire-sensor-data';

/**
 * API endpoint for real-time sensor data (last 5 minutes)
 * GET /realtime?deviceId={optional}
 */
exports.handler = async (event) => {
  console.log('Request:', JSON.stringify(event));

  const deviceId = event.queryStringParameters?.deviceId;
  const fiveMinutesAgo = Date.now() - (5 * 60 * 1000);

  try {
    let readings = [];

    if (deviceId) {
      // Query specific device
      const result = await docClient.send(new QueryCommand({
        TableName: TABLE_NAME,
        KeyConditionExpression: 'device_id = :device_id AND #ts >= :five_min_ago',
        ExpressionAttributeNames: {
          '#ts': 'timestamp'
        },
        ExpressionAttributeValues: {
          ':device_id': deviceId,
          ':five_min_ago': fiveMinutesAgo
        },
        ScanIndexForward: false,
        Limit: 1
      }));

      readings = result.Items || [];
    } else {
      // Scan all devices (less efficient but works for small datasets)
      const result = await docClient.send(new ScanCommand({
        TableName: TABLE_NAME,
        FilterExpression: '#ts >= :five_min_ago',
        ExpressionAttributeNames: {
          '#ts': 'timestamp'
        },
        ExpressionAttributeValues: {
          ':five_min_ago': fiveMinutesAgo
        }
      }));

      // Group by device_id and get latest per device
      const deviceMap = {};
      (result.Items || []).forEach(item => {
        if (!deviceMap[item.device_id] || item.timestamp > deviceMap[item.device_id].timestamp) {
          deviceMap[item.device_id] = item;
        }
      });

      readings = Object.values(deviceMap);
    }

    return {
      statusCode: 200,
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'GET,OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type,X-Api-Key'
      },
      body: JSON.stringify({
        readings: readings,
        count: readings.length,
        timestamp: new Date().toISOString()
      })
    };

  } catch (error) {
    console.error('Error querying data:', error);
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
