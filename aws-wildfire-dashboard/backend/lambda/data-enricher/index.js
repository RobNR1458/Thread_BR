const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient, PutCommand } = require('@aws-sdk/lib-dynamodb');
const AWSXRay = require('aws-xray-sdk-core');

// Wrap AWS SDK clients with X-Ray for distributed tracing
const ddbClient = AWSXRay.captureAWSv3Client(new DynamoDBClient({ region: process.env.AWS_REGION || 'us-east-2' }));
const docClient = DynamoDBDocumentClient.from(ddbClient);

const TABLE_NAME = process.env.DYNAMODB_TABLE || 'wildfire-sensor-data';

/**
 * Lambda handler for enriching sensor data with calculated metrics
 *
 * Input event from IoT Rule:
 * {
 *   "id": "sensor_1",
 *   "temp": 22.45,
 *   "hum": 55.60,
 *   "press": 1010.33,
 *   "gas": 400.50
 * }
 *
 * Calculates and adds:
 * - heat_index: Feels-like temperature based on temp + humidity
 * - dew_point: Temperature at which water vapor condenses
 * - risk_score: Wildfire risk score (0-100)
 */
exports.handler = async (event) => {
  const startTime = Date.now();

  // Log incoming event for debugging
  console.log('Received sensor data:', JSON.stringify(event));

  const { id, temp, hum, press, gas } = event;

  // Validate input data
  if (!id || temp === undefined || hum === undefined || press === undefined || gas === undefined) {
    console.error('Invalid input data:', event);
    throw new Error('Missing required sensor fields: id, temp, hum, press, gas');
  }

  try {
    // Calculate enriched metrics
    const heatIndex = calculateHeatIndex(temp, hum);
    const dewPoint = calculateDewPoint(temp, hum);
    const riskScore = calculateWildfireRisk(temp, hum, gas);

    const timestamp = Date.now();
    const ttl = Math.floor(timestamp / 1000) + (90 * 24 * 60 * 60); // 90 days TTL

    // Prepare enriched data for DynamoDB
    const enrichedData = {
      device_id: id,
      timestamp: timestamp,
      ttl: ttl,
      temperature: temp,
      humidity: hum,
      pressure: press,
      gas_concentration: gas,
      heat_index: heatIndex,
      dew_point: dewPoint,
      risk_score: riskScore,
      enriched_at: new Date().toISOString()
    };

    // Write to DynamoDB
    await docClient.send(new PutCommand({
      TableName: TABLE_NAME,
      Item: enrichedData
    }));

    // CloudWatch Embedded Metric Format for observability
    console.log(JSON.stringify({
      _aws: {
        Timestamp: timestamp,
        CloudWatchMetrics: [{
          Namespace: 'WildfireDetection',
          Dimensions: [['device_id']],
          Metrics: [
            { Name: 'RiskScore', Unit: 'None' },
            { Name: 'Temperature', Unit: 'None' },
            { Name: 'Humidity', Unit: 'Percent' },
            { Name: 'ProcessingTime', Unit: 'Milliseconds' }
          ]
        }]
      },
      device_id: id,
      RiskScore: riskScore,
      Temperature: temp,
      Humidity: hum,
      ProcessingTime: Date.now() - startTime
    }));

    console.log(`Successfully enriched and stored data for device: ${id}, risk_score: ${riskScore}`);

    return {
      statusCode: 200,
      body: JSON.stringify({
        message: 'Data enriched and stored successfully',
        deviceId: id,
        riskScore: riskScore
      })
    };

  } catch (error) {
    console.error('Error enriching/storing data:', error);
    throw error;
  }
};

/**
 * Calculate heat index (feels-like temperature)
 * Using Steadman formula
 * @param {number} tempC - Temperature in Celsius
 * @param {number} humidity - Relative humidity (0-100)
 * @returns {number} Heat index in Celsius
 */
function calculateHeatIndex(tempC, humidity) {
  // Convert to Fahrenheit for calculation
  const tempF = (tempC * 9/5) + 32;

  // Steadman formula
  const hi = -42.379 +
             (2.04901523 * tempF) +
             (10.14333127 * humidity) -
             (0.22475541 * tempF * humidity) -
             (6.83783e-3 * tempF * tempF) -
             (5.481717e-2 * humidity * humidity) +
             (1.22874e-3 * tempF * tempF * humidity) +
             (8.5282e-4 * tempF * humidity * humidity) -
             (1.99e-6 * tempF * tempF * humidity * humidity);

  // Convert back to Celsius
  return (hi - 32) * 5/9;
}

/**
 * Calculate dew point temperature
 * Using Magnus formula
 * @param {number} tempC - Temperature in Celsius
 * @param {number} humidity - Relative humidity (0-100)
 * @returns {number} Dew point in Celsius
 */
function calculateDewPoint(tempC, humidity) {
  const a = 17.27;
  const b = 237.7;

  const alpha = ((a * tempC) / (b + tempC)) + Math.log(humidity / 100);
  const dewPoint = (b * alpha) / (a - alpha);

  return dewPoint;
}

/**
 * Calculate wildfire risk score (0-100)
 * Based on temperature, humidity, and gas concentration
 * @param {number} temp - Temperature in Celsius
 * @param {number} humidity - Relative humidity (0-100)
 * @param {number} gas - Gas concentration in ppm
 * @returns {number} Risk score (0-100)
 */
function calculateWildfireRisk(temp, humidity, gas) {
  // Temperature factor (0-35 points)
  // High risk when temp > 30°C
  const tempFactor = Math.min(35, Math.max(0, (temp - 20) * 1.75));

  // Humidity factor (0-35 points)
  // High risk when humidity < 30%
  const humidityFactor = Math.min(35, Math.max(0, (80 - humidity) * 0.875));

  // Gas concentration factor (0-30 points)
  // High risk when gas > 500 ppm
  const gasFactor = Math.min(30, Math.max(0, gas * 0.06));

  // Total risk score
  const riskScore = tempFactor + humidityFactor + gasFactor;

  return Math.round(riskScore);
}
