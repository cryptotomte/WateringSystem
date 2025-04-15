/**
 * JavaScript for WateringSystem web interface
 * Handles API communication, UI updates, and user interactions
 */

// Configuration
const API_CONFIG = {
    ENDPOINT: '/api',
    REFRESH_INTERVAL: 30000, // 30 seconds
    CHART_REFRESH_INTERVAL: 300000, // 5 minutes
};

// Global state
const appState = {
    connected: false,
    autoWateringEnabled: false,
    isWatering: false,
    lastUpdate: null,
    refreshTimer: null,
    chartRefreshTimer: null,
    chart: null,
    settings: {
        moistureThresholdLow: 20,
        moistureThresholdHigh: 60,
        wateringDuration: 20,
        minWateringInterval: 6
    }
};

// DOM elements
const elements = {
    // Status indicators
    connectionStatus: document.getElementById('connection-status'),
    wateringStatus: document.getElementById('watering-status'),
    
    // Sensor readings
    envTemperature: document.getElementById('env-temperature'),
    envHumidity: document.getElementById('env-humidity'),
    envPressure: document.getElementById('env-pressure'),
    soilMoisture: document.getElementById('soil-moisture'),
    soilTemperature: document.getElementById('soil-temperature'),
    soilPh: document.getElementById('soil-ph'),
    soilEc: document.getElementById('soil-ec'),
    soilNpk: document.getElementById('soil-npk'),
    lastUpdateTime: document.getElementById('last-update-time'),
    
    // Control buttons
    refreshDataBtn: document.getElementById('refresh-data'),
    startWateringBtn: document.getElementById('start-watering'),
    stopWateringBtn: document.getElementById('stop-watering'),
    wateringDurationInput: document.getElementById('watering-duration-input'),
    autoWateringToggle: document.getElementById('auto-watering-toggle'),
    autoWateringStatus: document.getElementById('auto-watering-status'),
    
    // Settings form
    settingsForm: document.getElementById('settings-form'),
    moistureThresholdLow: document.getElementById('moisture-threshold-low'),
    moistureThresholdHigh: document.getElementById('moisture-threshold-high'),
    wateringDurationSetting: document.getElementById('watering-duration-setting'),
    minWateringInterval: document.getElementById('min-watering-interval'),
    
    // Chart controls
    chartSensor: document.getElementById('chart-sensor'),
    chartReading: document.getElementById('chart-reading'),
    chartTimeRange: document.getElementById('chart-time-range'),
    dataChart: document.getElementById('data-chart'),
    
    // System info
    systemIp: document.getElementById('system-ip'),
    storageUsage: document.getElementById('storage-usage'),
    
    // Modal
    confirmationModal: document.getElementById('confirmation-modal'),
    modalMessage: document.getElementById('modal-message'),
    modalConfirm: document.getElementById('modal-confirm'),
    modalCancel: document.getElementById('modal-cancel'),
    closeModal: document.querySelector('.close-modal'),
    
    // Notifications
    notificationContainer: document.getElementById('notification-container')
};

/**
 * Initialize the application
 */
function initApp() {
    // Set up event listeners
    setupEventListeners();
    
    // Load settings
    loadSettings();
    
    // Initialize chart
    initChart();
    
    // Fetch initial data
    fetchSensorData();
    fetchSystemStatus();
    fetchHistoricalData();
    
    // Start auto-refresh
    startAutoRefresh();
    
    // Show welcome notification
    showNotification('Welcome to WateringSystem', 'The system is ready to monitor and control your plants.', 'info');
}

/**
 * Set up event listeners for UI elements
 */
function setupEventListeners() {
    // Refresh button
    elements.refreshDataBtn.addEventListener('click', () => {
        fetchSensorData();
        fetchSystemStatus();
    });
    
    // Watering controls
    elements.startWateringBtn.addEventListener('click', () => {
        showConfirmation(
            'Are you sure you want to start watering?',
            () => startWatering(elements.wateringDurationInput.value)
        );
    });
    
    elements.stopWateringBtn.addEventListener('click', () => {
        showConfirmation(
            'Are you sure you want to stop watering?',
            stopWatering
        );
    });
    
    // Auto watering toggle
    elements.autoWateringToggle.addEventListener('change', (e) => {
        setAutoWatering(e.target.checked);
    });
    
    // Settings form
    elements.settingsForm.addEventListener('submit', (e) => {
        e.preventDefault();
        saveSettings();
    });
    
    // Chart controls
    elements.chartSensor.addEventListener('change', updateChartOptions);
    elements.chartReading.addEventListener('change', fetchHistoricalData);
    elements.chartTimeRange.addEventListener('change', fetchHistoricalData);
    
    // Modal close
    elements.closeModal.addEventListener('click', hideConfirmation);
    elements.modalCancel.addEventListener('click', hideConfirmation);
    
    // Close when clicking outside the modal
    window.addEventListener('click', (e) => {
        if (e.target === elements.confirmationModal) {
            hideConfirmation();
        }
    });
}

/**
 * Start automatic data refresh
 */
function startAutoRefresh() {
    // Clear any existing timers
    if (appState.refreshTimer) {
        clearInterval(appState.refreshTimer);
    }
    
    if (appState.chartRefreshTimer) {
        clearInterval(appState.chartRefreshTimer);
    }
    
    // Set new timers
    appState.refreshTimer = setInterval(() => {
        fetchSensorData();
        fetchSystemStatus();
    }, API_CONFIG.REFRESH_INTERVAL);
    
    appState.chartRefreshTimer = setInterval(() => {
        fetchHistoricalData();
    }, API_CONFIG.CHART_REFRESH_INTERVAL);
}

/**
 * Fetch current sensor data from the API
 */
async function fetchSensorData() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/sensors`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        updateSensorDisplay(data);
        appState.connected = true;
        updateConnectionStatus(true);
        appState.lastUpdate = new Date();
        elements.lastUpdateTime.textContent = appState.lastUpdate.toLocaleTimeString();
    } catch (error) {
        console.error('Error fetching sensor data:', error);
        appState.connected = false;
        updateConnectionStatus(false);
        showNotification('Connection Error', 'Failed to fetch sensor data. Check your connection.', 'error');
    }
}

/**
 * Update the sensor readings on the UI
 * @param {Object} data - Sensor data from the API
 */
function updateSensorDisplay(data) {
    // Update environmental sensor readings
    if (data.environmental) {
        elements.envTemperature.textContent = data.environmental.temperature.toFixed(1);
        elements.envHumidity.textContent = data.environmental.humidity.toFixed(1);
        elements.envPressure.textContent = data.environmental.pressure.toFixed(1);
    }
    
    // Update soil sensor readings
    if (data.soil) {
        elements.soilMoisture.textContent = data.soil.moisture.toFixed(1);
        elements.soilTemperature.textContent = data.soil.temperature.toFixed(1);
        
        if (data.soil.ph !== undefined) {
            elements.soilPh.textContent = data.soil.ph.toFixed(1);
        }
        
        if (data.soil.ec !== undefined) {
            elements.soilEc.textContent = data.soil.ec.toFixed(1);
        }
        
        // Show NPK values if available
        if (data.soil.npk) {
            elements.soilNpk.classList.remove('hidden');
            elements.soilNpk.textContent = `${data.soil.npk.n}-${data.soil.npk.p}-${data.soil.npk.k}`;
        } else {
            elements.soilNpk.classList.add('hidden');
        }
    }
}

/**
 * Fetch system status from the API
 */
async function fetchSystemStatus() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/status`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        updateSystemStatus(data);
    } catch (error) {
        console.error('Error fetching system status:', error);
        showNotification('Status Error', 'Failed to fetch system status.', 'error');
    }
}

/**
 * Update system status display
 * @param {Object} data - System status data from the API
 */
function updateSystemStatus(data) {
    // Update watering status
    appState.isWatering = data.isWatering;
    updateWateringStatus(data.isWatering);
    
    // Update auto watering status
    appState.autoWateringEnabled = data.autoWateringEnabled;
    elements.autoWateringToggle.checked = data.autoWateringEnabled;
    elements.autoWateringStatus.textContent = data.autoWateringEnabled ? 'Enabled' : 'Disabled';
    
    // Update system info
    if (data.system) {
        elements.systemIp.textContent = data.system.ip || '--';
        
        if (data.system.storage) {
            const usedPercent = ((data.system.storage.used / data.system.storage.total) * 100).toFixed(1);
            elements.storageUsage.textContent = `${usedPercent}% (${formatBytes(data.system.storage.used)} / ${formatBytes(data.system.storage.total)})`;
        }
    }
    
    // Update settings if available
    if (data.settings) {
        appState.settings = data.settings;
        updateSettingsForm(data.settings);
    }
}

/**
 * Update the connection status display
 * @param {boolean} connected - Whether the system is connected
 */
function updateConnectionStatus(connected) {
    if (connected) {
        elements.connectionStatus.classList.remove('disconnected');
        elements.connectionStatus.classList.add('connected');
        elements.connectionStatus.querySelector('.status-text').textContent = 'Connected';
    } else {
        elements.connectionStatus.classList.remove('connected');
        elements.connectionStatus.classList.add('disconnected');
        elements.connectionStatus.querySelector('.status-text').textContent = 'Disconnected';
    }
}

/**
 * Update the watering status display
 * @param {boolean} isWatering - Whether the system is currently watering
 */
function updateWateringStatus(isWatering) {
    if (isWatering) {
        elements.wateringStatus.classList.add('active');
        elements.wateringStatus.querySelector('.status-text').textContent = 'Watering Active';
        elements.startWateringBtn.disabled = true;
        elements.stopWateringBtn.disabled = false;
    } else {
        elements.wateringStatus.classList.remove('active');
        elements.wateringStatus.querySelector('.status-text').textContent = 'Watering Inactive';
        elements.startWateringBtn.disabled = false;
        elements.stopWateringBtn.disabled = true;
    }
}

/**
 * Start watering via API
 * @param {number} duration - Watering duration in seconds
 */
async function startWatering(duration) {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/control/water/start`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ duration: parseInt(duration) || 20 })
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        if (data.success) {
            showNotification('Watering Started', `Watering started for ${duration} seconds.`, 'success');
            updateWateringStatus(true);
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error starting watering:', error);
        showNotification('Watering Error', `Failed to start watering: ${error.message}`, 'error');
    }
}

/**
 * Stop watering via API
 */
async function stopWatering() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/control/water/stop`, {
            method: 'POST'
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        if (data.success) {
            showNotification('Watering Stopped', 'Watering has been stopped.', 'info');
            updateWateringStatus(false);
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error stopping watering:', error);
        showNotification('Watering Error', `Failed to stop watering: ${error.message}`, 'error');
    }
}

/**
 * Enable or disable auto-watering via API
 * @param {boolean} enabled - Whether auto-watering should be enabled
 */
async function setAutoWatering(enabled) {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/control/auto`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ enabled })
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        if (data.success) {
            appState.autoWateringEnabled = enabled;
            elements.autoWateringStatus.textContent = enabled ? 'Enabled' : 'Disabled';
            showNotification('Auto-Watering', `Auto-watering has been ${enabled ? 'enabled' : 'disabled'}.`, 'info');
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error setting auto-watering:', error);
        showNotification('Auto-Watering Error', `Failed to ${enabled ? 'enable' : 'disable'} auto-watering: ${error.message}`, 'error');
        
        // Revert UI to previous state
        elements.autoWateringToggle.checked = appState.autoWateringEnabled;
    }
}

/**
 * Load system settings
 */
function loadSettings() {
    // This will be called by fetchSystemStatus, but we set the defaults here
    updateSettingsForm(appState.settings);
}

/**
 * Update settings form with values
 * @param {Object} settings - System settings
 */
function updateSettingsForm(settings) {
    elements.moistureThresholdLow.value = settings.moistureThresholdLow;
    elements.moistureThresholdHigh.value = settings.moistureThresholdHigh;
    elements.wateringDurationSetting.value = settings.wateringDuration;
    elements.minWateringInterval.value = settings.minWateringInterval;
    
    // Also update the manual watering duration
    elements.wateringDurationInput.value = settings.wateringDuration;
}

/**
 * Save settings via API
 */
async function saveSettings() {
    const settings = {
        moistureThresholdLow: parseInt(elements.moistureThresholdLow.value),
        moistureThresholdHigh: parseInt(elements.moistureThresholdHigh.value),
        wateringDuration: parseInt(elements.wateringDurationSetting.value),
        minWateringInterval: parseInt(elements.minWateringInterval.value)
    };
    
    // Validate settings
    if (settings.moistureThresholdLow >= settings.moistureThresholdHigh) {
        showNotification('Invalid Settings', 'Low moisture threshold must be less than high threshold.', 'error');
        return;
    }
    
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(settings)
        });
        
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        if (data.success) {
            appState.settings = settings;
            showNotification('Settings Saved', 'System settings have been updated.', 'success');
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error saving settings:', error);
        showNotification('Settings Error', `Failed to save settings: ${error.message}`, 'error');
    }
}

/**
 * Initialize the chart for displaying historical data
 */
function initChart() {
    const ctx = elements.dataChart.getContext('2d');
    appState.chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Sensor Reading',
                data: [],
                borderColor: '#2e7d32',
                backgroundColor: 'rgba(46, 125, 50, 0.1)',
                borderWidth: 2,
                tension: 0.2,
                fill: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    type: 'time',
                    time: {
                        unit: 'hour',
                        displayFormats: {
                            hour: 'HH:mm'
                        }
                    },
                    title: {
                        display: true,
                        text: 'Time'
                    }
                },
                y: {
                    beginAtZero: false,
                    title: {
                        display: true,
                        text: 'Value'
                    }
                }
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top'
                },
                tooltip: {
                    mode: 'index',
                    intersect: false
                }
            }
        }
    });
}

/**
 * Update chart options based on selected sensor/reading
 */
function updateChartOptions() {
    const sensor = elements.chartSensor.value;
    const readingSelect = elements.chartReading;
    
    // Clear options
    readingSelect.innerHTML = '';
    
    // Add appropriate options based on sensor type
    if (sensor === 'env') {
        addOption(readingSelect, 'temperature', 'Temperature');
        addOption(readingSelect, 'humidity', 'Humidity');
        addOption(readingSelect, 'pressure', 'Pressure');
    } else if (sensor === 'soil') {
        addOption(readingSelect, 'moisture', 'Moisture');
        addOption(readingSelect, 'temperature', 'Temperature');
        addOption(readingSelect, 'ph', 'pH');
        addOption(readingSelect, 'ec', 'EC');
    }
    
    // Trigger change to fetch new data
    fetchHistoricalData();
}

/**
 * Helper to add an option to a select element
 */
function addOption(selectElement, value, text) {
    const option = document.createElement('option');
    option.value = value;
    option.textContent = text;
    selectElement.appendChild(option);
}

/**
 * Fetch historical data for the chart
 */
async function fetchHistoricalData() {
    const sensor = elements.chartSensor.value;
    const reading = elements.chartReading.value;
    const timeRange = elements.chartTimeRange.value;
    
    if (!sensor || !reading || !timeRange) {
        return;
    }
    
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/history?sensor=${sensor}&reading=${reading}&range=${timeRange}`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        
        const data = await response.json();
        updateChart(data, `${sensor === 'env' ? 'Environmental' : 'Soil'} ${elements.chartReading.options[elements.chartReading.selectedIndex].text}`);
    } catch (error) {
        console.error('Error fetching historical data:', error);
        showNotification('Chart Error', 'Failed to load historical data.', 'error');
    }
}

/**
 * Update chart with new data
 * @param {Object} data - Historical data from API
 * @param {string} label - Chart label
 */
function updateChart(data, label) {
    if (!appState.chart || !data || !data.timestamps || !data.values) {
        return;
    }
    
    // Set units based on reading type
    let unit = '';
    const reading = elements.chartReading.value;
    
    switch (reading) {
        case 'temperature':
            unit = '°C';
            break;
        case 'humidity':
        case 'moisture':
            unit = '%';
            break;
        case 'pressure':
            unit = 'hPa';
            break;
        case 'ec':
            unit = 'µS/cm';
            break;
    }
    
    // Format time range for x-axis
    const timeRange = elements.chartTimeRange.value;
    let timeUnit = 'hour';
    let format = 'HH:mm';
    
    if (timeRange === '7d' || timeRange === '30d') {
        timeUnit = 'day';
        format = 'MMM dd';
    }
    
    // Update chart data
    appState.chart.data.labels = data.timestamps.map(t => new Date(t));
    appState.chart.data.datasets[0].data = data.values;
    appState.chart.data.datasets[0].label = `${label} (${unit})`;
    
    // Update scale options
    appState.chart.options.scales.x.time.unit = timeUnit;
    appState.chart.options.scales.x.time.displayFormats[timeUnit] = format;
    appState.chart.options.scales.y.title.text = `${label} (${unit})`;
    
    // Update chart
    appState.chart.update();
}

/**
 * Show confirmation modal
 * @param {string} message - Confirmation message
 * @param {Function} onConfirm - Function to call on confirmation
 */
function showConfirmation(message, onConfirm) {
    elements.modalMessage.textContent = message;
    elements.confirmationModal.classList.add('show');
    
    // Remove any existing event listener
    const oldConfirmBtn = elements.modalConfirm;
    const newConfirmBtn = oldConfirmBtn.cloneNode(true);
    oldConfirmBtn.parentNode.replaceChild(newConfirmBtn, oldConfirmBtn);
    elements.modalConfirm = newConfirmBtn;
    
    // Add new event listener
    elements.modalConfirm.addEventListener('click', () => {
        hideConfirmation();
        onConfirm();
    });
}

/**
 * Hide confirmation modal
 */
function hideConfirmation() {
    elements.confirmationModal.classList.remove('show');
}

/**
 * Show a notification
 * @param {string} title - Notification title
 * @param {string} message - Notification message
 * @param {string} type - Notification type: 'success', 'error', 'warning', 'info'
 */
function showNotification(title, message, type = 'info') {
    // Create notification element
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    
    notification.innerHTML = `
        <div class="notification-content">
            <div class="notification-title">${title}</div>
            <div class="notification-message">${message}</div>
        </div>
        <span class="notification-close">&times;</span>
    `;
    
    // Add to container
    elements.notificationContainer.appendChild(notification);
    
    // Add event listener for close button
    const closeBtn = notification.querySelector('.notification-close');
    closeBtn.addEventListener('click', () => {
        removeNotification(notification);
    });
    
    // Auto remove after 5 seconds
    setTimeout(() => {
        removeNotification(notification);
    }, 5000);
}

/**
 * Remove a notification
 * @param {Element} notification - Notification element to remove
 */
function removeNotification(notification) {
    notification.style.opacity = '0';
    setTimeout(() => {
        notification.remove();
    }, 300);
}

/**
 * Format bytes to human readable size
 * @param {number} bytes - Size in bytes
 * @param {number} decimals - Number of decimal places
 * @return {string} Formatted size
 */
function formatBytes(bytes, decimals = 1) {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
    
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

// Initialize the application when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', initApp);