import React, { useState } from 'react';
import './AlertsPanel.css';

const AlertsPanel = ({ alerts, onRefresh }) => {
  const [severityFilter, setSeverityFilter] = useState('all');
  const [typeFilter, setTypeFilter] = useState('all');

  // Filter alerts based on selections
  const filteredAlerts = alerts.filter(alert => {
    const matchesSeverity = severityFilter === 'all' || alert.severity === severityFilter;
    const matchesType = typeFilter === 'all' || alert.alert_type === typeFilter;
    return matchesSeverity && matchesType;
  });

  // Get unique alert types
  const alertTypes = [...new Set(alerts.map(a => a.alert_type))];

  // Severity color mapping
  const getSeverityColor = (severity) => {
    switch (severity) {
      case 'CRITICAL':
        return '#d32f2f';
      case 'HIGH':
        return '#f57c00';
      case 'MODERATE':
        return '#fbc02d';
      case 'LOW':
        return '#388e3c';
      default:
        return '#757575';
    }
  };

  // Alert type icon mapping
  const getAlertIcon = (type) => {
    switch (type) {
      case 'temperature_anomaly':
        return '🌡️';
      case 'humidity_anomaly':
        return '💧';
      case 'gas_anomaly':
        return '☁️';
      case 'pressure_anomaly':
        return '🌪️';
      default:
        return '⚠️';
    }
  };

  // Format timestamp
  const formatTime = (timestamp) => {
    if (!timestamp) return 'N/A';
    const date = new Date(timestamp);
    const now = new Date();
    const diffMs = now - date;
    const diffMins = Math.floor(diffMs / 60000);

    if (diffMins < 1) return 'Just now';
    if (diffMins < 60) return `${diffMins}m ago`;
    if (diffMins < 1440) return `${Math.floor(diffMins / 60)}h ago`;
    return date.toLocaleDateString() + ' ' + date.toLocaleTimeString();
  };

  // Format metric name
  const formatMetric = (metric) => {
    return metric.replace('_', ' ').replace(/\b\w/g, l => l.toUpperCase());
  };

  return (
    <div className="alerts-panel">
      <div className="alerts-header">
        <div className="alerts-title">
          <h3>Anomaly Alerts</h3>
          <span className="alert-count">
            {filteredAlerts.length} alert{filteredAlerts.length !== 1 ? 's' : ''}
          </span>
        </div>
        <button className="refresh-btn" onClick={onRefresh}>
          🔄 Refresh
        </button>
      </div>

      <div className="alerts-filters">
        <div className="filter-group">
          <label>Severity:</label>
          <select
            value={severityFilter}
            onChange={(e) => setSeverityFilter(e.target.value)}
          >
            <option value="all">All Severities</option>
            <option value="CRITICAL">Critical</option>
            <option value="HIGH">High</option>
            <option value="MODERATE">Moderate</option>
            <option value="LOW">Low</option>
          </select>
        </div>

        <div className="filter-group">
          <label>Type:</label>
          <select
            value={typeFilter}
            onChange={(e) => setTypeFilter(e.target.value)}
          >
            <option value="all">All Types</option>
            {alertTypes.map(type => (
              <option key={type} value={type}>
                {formatMetric(type)}
              </option>
            ))}
          </select>
        </div>
      </div>

      <div className="alerts-list">
        {filteredAlerts.length === 0 ? (
          <div className="no-alerts">
            <p>No alerts match the current filters</p>
          </div>
        ) : (
          filteredAlerts.map((alert, index) => (
            <div
              key={alert.alert_id || index}
              className="alert-card"
              style={{ borderLeftColor: getSeverityColor(alert.severity) }}
            >
              <div className="alert-header-row">
                <div className="alert-type">
                  <span className="alert-icon">{getAlertIcon(alert.alert_type)}</span>
                  <span className="alert-type-text">{formatMetric(alert.alert_type)}</span>
                </div>
                <span
                  className="severity-badge"
                  style={{ backgroundColor: getSeverityColor(alert.severity) }}
                >
                  {alert.severity}
                </span>
              </div>

              <div className="alert-device">
                <strong>Device:</strong> {alert.device_id}
              </div>

              <div className="alert-message">
                {alert.message}
              </div>

              <div className="alert-details">
                {alert.metric_name && (
                  <div className="detail-item">
                    <span className="detail-label">Metric:</span>
                    <span className="detail-value">{formatMetric(alert.metric_name)}</span>
                  </div>
                )}

                {alert.current_value !== undefined && (
                  <div className="detail-item">
                    <span className="detail-label">Value:</span>
                    <span className="detail-value">{alert.current_value.toFixed(2)}</span>
                  </div>
                )}

                {alert.expected_range && (
                  <div className="detail-item">
                    <span className="detail-label">Expected:</span>
                    <span className="detail-value">{alert.expected_range}</span>
                  </div>
                )}

                {alert.z_score !== undefined && (
                  <div className="detail-item">
                    <span className="detail-label">Z-Score:</span>
                    <span className="detail-value">{alert.z_score.toFixed(2)}σ</span>
                  </div>
                )}
              </div>

              <div className="alert-timestamp">
                {formatTime(alert.timestamp)}
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
};

export default AlertsPanel;
