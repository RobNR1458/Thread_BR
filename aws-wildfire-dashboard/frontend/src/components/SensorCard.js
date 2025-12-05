import React from 'react';
import './SensorCard.css';

const SensorCard = ({ data, isSelected, onSelect }) => {
  const {
    device_id,
    temperature,
    humidity,
    pressure,
    gas_concentration,
    risk_score,
    heat_index,
    dew_point,
    time
  } = data;

  // Determine risk level based on risk_score (0-100)
  const getRiskLevel = (score) => {
    if (score >= 70) return { level: 'CRITICAL', color: '#d32f2f' };
    if (score >= 50) return { level: 'HIGH', color: '#f57c00' };
    if (score >= 30) return { level: 'MODERATE', color: '#fbc02d' };
    return { level: 'LOW', color: '#388e3c' };
  };

  const risk = getRiskLevel(risk_score);

  // Format timestamp
  const formatTime = (timestamp) => {
    if (!timestamp) return 'N/A';
    const date = new Date(timestamp);
    return date.toLocaleTimeString();
  };

  return (
    <div
      className={`sensor-card ${isSelected ? 'selected' : ''}`}
      onClick={onSelect}
      style={{ borderLeftColor: risk.color }}
    >
      <div className="card-header">
        <h3>{device_id}</h3>
        <span className="timestamp">{formatTime(time)}</span>
      </div>

      <div className="risk-indicator" style={{ backgroundColor: risk.color }}>
        <span className="risk-level">{risk.level}</span>
        <span className="risk-score">{risk_score}/100</span>
      </div>

      <div className="metrics-grid">
        <div className="metric">
          <span className="metric-icon">🌡️</span>
          <div className="metric-data">
            <span className="metric-label">Temperature</span>
            <span className="metric-value">{temperature.toFixed(1)}°C</span>
          </div>
        </div>

        <div className="metric">
          <span className="metric-icon">💧</span>
          <div className="metric-data">
            <span className="metric-label">Humidity</span>
            <span className="metric-value">{humidity.toFixed(1)}%</span>
          </div>
        </div>

        <div className="metric">
          <span className="metric-icon">🌪️</span>
          <div className="metric-data">
            <span className="metric-label">Pressure</span>
            <span className="metric-value">{pressure.toFixed(0)} hPa</span>
          </div>
        </div>

        <div className="metric">
          <span className="metric-icon">☁️</span>
          <div className="metric-data">
            <span className="metric-label">Gas</span>
            <span className="metric-value">{gas_concentration.toFixed(0)} ppm</span>
          </div>
        </div>
      </div>

      {heat_index && (
        <div className="calculated-metrics">
          <div className="calc-metric">
            <span className="calc-label">Heat Index:</span>
            <span className="calc-value">{heat_index.toFixed(1)}°C</span>
          </div>
          {dew_point && (
            <div className="calc-metric">
              <span className="calc-label">Dew Point:</span>
              <span className="calc-value">{dew_point.toFixed(1)}°C</span>
            </div>
          )}
        </div>
      )}

      {isSelected && (
        <div className="selected-indicator">
          ✓ Selected for charts
        </div>
      )}
    </div>
  );
};

export default SensorCard;
