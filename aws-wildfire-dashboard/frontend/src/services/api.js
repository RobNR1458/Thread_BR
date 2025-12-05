import axios from 'axios';

const API_BASE = process.env.REACT_APP_API_ENDPOINT;
const API_KEY = process.env.REACT_APP_API_KEY;

if (!API_BASE || !API_KEY) {
  console.error('⚠️ API configuration missing!');
  console.error('Please create a .env file with:');
  console.error('REACT_APP_API_ENDPOINT=your-api-endpoint');
  console.error('REACT_APP_API_KEY=your-api-key');
}

// Create axios instance with default configuration
const apiClient = axios.create({
  baseURL: API_BASE,
  headers: {
    'x-api-key': API_KEY,
    'Content-Type': 'application/json'
  },
  timeout: 10000 // 10 seconds
});

// Request interceptor for logging
apiClient.interceptors.request.use(
  (config) => {
    console.log(`API Request: ${config.method?.toUpperCase()} ${config.url}`);
    return config;
  },
  (error) => {
    console.error('API Request Error:', error);
    return Promise.reject(error);
  }
);

// Response interceptor for error handling
apiClient.interceptors.response.use(
  (response) => {
    console.log(`API Response: ${response.status} ${response.config.url}`);
    return response;
  },
  (error) => {
    console.error('API Response Error:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

/**
 * Fetch real-time sensor data
 * @param {string} deviceId - Optional device ID to filter
 * @returns {Promise<Object>} Response with sensor data
 */
export const fetchRealtime = async (deviceId = null) => {
  try {
    const params = deviceId ? { deviceId } : {};
    const response = await apiClient.get('/realtime', { params });
    return response.data;
  } catch (error) {
    console.error('Error fetching realtime data:', error);
    throw error;
  }
};

/**
 * Fetch historical sensor data with aggregation
 * @param {string} deviceId - Optional device ID
 * @param {string} from - Start time (ISO 8601)
 * @param {string} to - End time (ISO 8601)
 * @param {string} interval - Aggregation interval (5m, 1h, 1d)
 * @returns {Promise<Object>} Response with historical data
 */
export const fetchHistorical = async (deviceId, from, to, interval = '5m') => {
  try {
    const params = {
      from,
      to,
      interval
    };
    if (deviceId) {
      params.deviceId = deviceId;
    }
    const response = await apiClient.get('/historical', { params });
    return response.data;
  } catch (error) {
    console.error('Error fetching historical data:', error);
    throw error;
  }
};

/**
 * Fetch ML-generated alerts
 * @param {string} deviceId - Optional device ID
 * @param {string} severity - Optional severity filter (LOW, MODERATE, HIGH, CRITICAL)
 * @param {number} limit - Max number of results
 * @returns {Promise<Object>} Response with alerts
 */
export const fetchAlerts = async (deviceId = null, severity = null, limit = 50) => {
  try {
    const params = { limit };
    if (deviceId) params.deviceId = deviceId;
    if (severity) params.severity = severity;

    const response = await apiClient.get('/alerts', { params });
    return response.data;
  } catch (error) {
    console.error('Error fetching alerts:', error);
    throw error;
  }
};

/**
 * Get time range for queries
 * @param {string} range - Range identifier (1h, 6h, 24h, 7d)
 * @returns {Object} Object with from and to ISO timestamps
 */
export const getTimeRange = (range) => {
  const now = new Date();
  let from;

  switch (range) {
    case '1h':
      from = new Date(now - 60 * 60 * 1000);
      break;
    case '6h':
      from = new Date(now - 6 * 60 * 60 * 1000);
      break;
    case '24h':
      from = new Date(now - 24 * 60 * 60 * 1000);
      break;
    case '7d':
      from = new Date(now - 7 * 24 * 60 * 60 * 1000);
      break;
    default:
      from = new Date(now - 60 * 60 * 1000);
  }

  return {
    from: from.toISOString(),
    to: now.toISOString()
  };
};

export default {
  fetchRealtime,
  fetchHistorical,
  fetchAlerts,
  getTimeRange
};
