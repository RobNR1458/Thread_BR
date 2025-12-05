import React, { useMemo } from 'react';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  TimeScale
} from 'chart.js';
import { Line } from 'react-chartjs-2';
import 'chartjs-adapter-date-fns';
import './HistoricalChart.css';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  TimeScale
);

const HistoricalChart = ({ data, timeRange }) => {
  const chartData = useMemo(() => {
    if (!data || data.length === 0) {
      return null;
    }

    // Group data by device_id
    const deviceGroups = data.reduce((acc, reading) => {
      const deviceId = reading.device_id;
      if (!acc[deviceId]) {
        acc[deviceId] = [];
      }
      acc[deviceId].push(reading);
      return acc;
    }, {});

    // Generate colors for devices
    const colors = [
      '#1976d2', // blue
      '#388e3c', // green
      '#f57c00', // orange
      '#d32f2f', // red
      '#7b1fa2', // purple
    ];

    const datasets = [];
    let colorIndex = 0;

    Object.entries(deviceGroups).forEach(([deviceId, readings]) => {
      const color = colors[colorIndex % colors.length];
      colorIndex++;

      // Temperature dataset
      datasets.push({
        label: `${deviceId} - Temperature`,
        data: readings.map(r => ({
          x: new Date(r.time),
          y: r.avg_temperature || r.temperature
        })),
        borderColor: color,
        backgroundColor: color + '20',
        borderWidth: 2,
        pointRadius: 2,
        tension: 0.1,
        yAxisID: 'y-temp'
      });

      // Humidity dataset
      datasets.push({
        label: `${deviceId} - Humidity`,
        data: readings.map(r => ({
          x: new Date(r.time),
          y: r.avg_humidity || r.humidity
        })),
        borderColor: color,
        backgroundColor: color + '20',
        borderWidth: 2,
        borderDash: [5, 5],
        pointRadius: 2,
        tension: 0.1,
        yAxisID: 'y-humidity',
        hidden: true // Hide by default
      });

      // Risk score dataset
      datasets.push({
        label: `${deviceId} - Risk Score`,
        data: readings.map(r => ({
          x: new Date(r.time),
          y: r.avg_risk_score || r.risk_score
        })),
        borderColor: '#d32f2f',
        backgroundColor: '#d32f2f20',
        borderWidth: 2,
        borderDash: [2, 2],
        pointRadius: 2,
        tension: 0.1,
        yAxisID: 'y-risk',
        hidden: true // Hide by default
      });
    });

    return {
      datasets
    };
  }, [data]);

  const options = {
    responsive: true,
    maintainAspectRatio: false,
    interaction: {
      mode: 'index',
      intersect: false,
    },
    plugins: {
      legend: {
        position: 'top',
        labels: {
          usePointStyle: true,
          padding: 15,
          font: {
            size: 12
          }
        }
      },
      title: {
        display: false
      },
      tooltip: {
        callbacks: {
          label: function(context) {
            let label = context.dataset.label || '';
            if (label) {
              label += ': ';
            }
            if (context.parsed.y !== null) {
              if (label.includes('Temperature')) {
                label += context.parsed.y.toFixed(1) + '°C';
              } else if (label.includes('Humidity')) {
                label += context.parsed.y.toFixed(1) + '%';
              } else if (label.includes('Risk')) {
                label += context.parsed.y.toFixed(0) + '/100';
              } else {
                label += context.parsed.y.toFixed(2);
              }
            }
            return label;
          }
        }
      }
    },
    scales: {
      x: {
        type: 'time',
        time: {
          unit: timeRange === '1h' ? 'minute' :
                timeRange === '6h' ? 'hour' :
                timeRange === '24h' ? 'hour' : 'day',
          displayFormats: {
            minute: 'HH:mm',
            hour: 'HH:mm',
            day: 'MMM dd'
          }
        },
        title: {
          display: true,
          text: 'Time'
        },
        grid: {
          color: '#e0e0e0'
        }
      },
      'y-temp': {
        type: 'linear',
        display: true,
        position: 'left',
        title: {
          display: true,
          text: 'Temperature (°C)'
        },
        grid: {
          color: '#e0e0e0'
        }
      },
      'y-humidity': {
        type: 'linear',
        display: true,
        position: 'right',
        title: {
          display: true,
          text: 'Humidity (%)'
        },
        grid: {
          drawOnChartArea: false,
        },
        min: 0,
        max: 100
      },
      'y-risk': {
        type: 'linear',
        display: false,
        min: 0,
        max: 100
      }
    }
  };

  if (!chartData) {
    return (
      <div className="chart-container">
        <div className="no-data">
          No historical data available for the selected time range
        </div>
      </div>
    );
  }

  return (
    <div className="chart-container">
      <Line data={chartData} options={options} />
      <div className="chart-info">
        <p>Click legend items to show/hide metrics</p>
      </div>
    </div>
  );
};

export default HistoricalChart;
