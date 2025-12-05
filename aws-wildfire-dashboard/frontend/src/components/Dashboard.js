import React, { useState, useEffect } from 'react';
import { fetchRealtime, fetchHistorical, fetchAlerts, getTimeRange } from '../services/api';
import SensorCard from './SensorCard';
import HistoricalChart from './HistoricalChart';
import AlertsPanel from './AlertsPanel';
import './Dashboard.css';

const Dashboard = () => {
  const [realtimeData, setRealtimeData] = useState([]);
  const [historicalData, setHistoricalData] = useState([]);
  const [alerts, setAlerts] = useState([]);
  const [selectedDevice, setSelectedDevice] = useState(null);
  const [timeRange, setTimeRange] = useState('1h');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [lastUpdate, setLastUpdate] = useState(null);

  // Fetch realtime data
  const loadRealtimeData = async () => {
    try {
      const data = await fetchRealtime();
      setRealtimeData(data.readings || []);
      setLastUpdate(new Date());
      setError(null);
    } catch (err) {
      console.error('Error loading realtime data:', err);
      setError('Failed to load sensor data');
    }
  };

  // Fetch historical data
  const loadHistoricalData = async () => {
    try {
      const range = getTimeRange(timeRange);
      const data = await fetchHistorical(selectedDevice, range.from, range.to, '5m');
      setHistoricalData(data.readings || []);
    } catch (err) {
      console.error('Error loading historical data:', err);
    }
  };

  // Fetch alerts
  const loadAlerts = async () => {
    try {
      const data = await fetchAlerts(selectedDevice, null, 50);
      setAlerts(data.alerts || []);
    } catch (err) {
      console.error('Error loading alerts:', err);
    }
  };

  // Initial load
  useEffect(() => {
    const loadAllData = async () => {
      setLoading(true);
      await Promise.all([
        loadRealtimeData(),
        loadHistoricalData(),
        loadAlerts()
      ]);
      setLoading(false);
    };
    loadAllData();
  }, []);

  // Auto-refresh realtime data every 5 seconds
  useEffect(() => {
    const refreshInterval = setInterval(loadRealtimeData, 5000);
    return () => clearInterval(refreshInterval);
  }, []);

  // Reload historical data when device or time range changes
  useEffect(() => {
    if (!loading) {
      loadHistoricalData();
    }
  }, [selectedDevice, timeRange]);

  // Reload alerts when device changes
  useEffect(() => {
    if (!loading) {
      loadAlerts();
    }
  }, [selectedDevice]);

  if (loading) {
    return (
      <div className="dashboard-loading">
        <div className="spinner"></div>
        <p>Loading wildfire detection system...</p>
      </div>
    );
  }

  return (
    <div className="dashboard">
      <header className="dashboard-header">
        <h1>🔥 Wildfire Detection System</h1>
        <div className="header-info">
          <span className="device-count">
            {realtimeData.length} device{realtimeData.length !== 1 ? 's' : ''} active
          </span>
          {lastUpdate && (
            <span className="last-update">
              Last update: {lastUpdate.toLocaleTimeString()}
            </span>
          )}
        </div>
      </header>

      {error && (
        <div className="error-banner">
          {error}
        </div>
      )}

      <div className="controls">
        <div className="control-group">
          <label htmlFor="device-filter">Device:</label>
          <select
            id="device-filter"
            value={selectedDevice || ''}
            onChange={(e) => setSelectedDevice(e.target.value || null)}
          >
            <option value="">All Devices</option>
            {realtimeData.map((reading) => (
              <option key={reading.device_id} value={reading.device_id}>
                {reading.device_id}
              </option>
            ))}
          </select>
        </div>

        <div className="control-group">
          <label htmlFor="time-range">Time Range:</label>
          <select
            id="time-range"
            value={timeRange}
            onChange={(e) => setTimeRange(e.target.value)}
          >
            <option value="1h">Last Hour</option>
            <option value="6h">Last 6 Hours</option>
            <option value="24h">Last 24 Hours</option>
            <option value="7d">Last 7 Days</option>
          </select>
        </div>
      </div>

      <section className="sensors-grid">
        <h2>Real-Time Sensors</h2>
        {realtimeData.length === 0 ? (
          <p className="no-data">No sensor data available</p>
        ) : (
          <div className="grid">
            {realtimeData.map((reading) => (
              <SensorCard
                key={reading.device_id}
                data={reading}
                isSelected={selectedDevice === reading.device_id}
                onSelect={() => setSelectedDevice(reading.device_id)}
              />
            ))}
          </div>
        )}
      </section>

      <section className="historical-section">
        <h2>
          Historical Data
          {selectedDevice && ` - ${selectedDevice}`}
        </h2>
        <HistoricalChart
          data={historicalData}
          timeRange={timeRange}
        />
      </section>

      <section className="alerts-section">
        <h2>
          ML Alerts
          {selectedDevice && ` - ${selectedDevice}`}
        </h2>
        <AlertsPanel
          alerts={alerts}
          onRefresh={loadAlerts}
        />
      </section>
    </div>
  );
};

export default Dashboard;
