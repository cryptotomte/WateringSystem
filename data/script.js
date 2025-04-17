/**
 * JavaScript for WateringSystem web interface
 * Handles API communication, UI updates, and user interactions
 */

// Configuration
const API_CONFIG = {
    ENDPOINT: '',  // Changed from '/api' to empty string
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
    },
    reservoir: {
        enabled: false,
        pumpRunning: false,
        lowLevelDetected: false,
        highLevelDetected: false
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

    // Reservoir control
    reservoirToggle: document.getElementById('reservoir-toggle'),
    reservoirStatus: document.getElementById('reservoir-status'),
    waterLevelIndicator: document.getElementById('water-level-indicator'),
    waterLevelText: document.getElementById('water-level-text'),
    reservoirPumpStatus: document.getElementById('reservoir-pump-status'),
    startReservoirPumpBtn: document.getElementById('start-reservoir-pump'),
    stopReservoirPumpBtn: document.getElementById('stop-reservoir-pump'),
    reservoirDurationInput: document.getElementById('reservoir-duration-input'),

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

    // Reservoir controls
    elements.reservoirToggle.addEventListener('change', (e) => {
        setReservoirPumpEnabled(e.target.checked);
    });

    elements.startReservoirPumpBtn.addEventListener('click', () => {
        showConfirmation(
            'Are you sure you want to start filling the reservoir?',
            () => startReservoirFilling(elements.reservoirDurationInput.value)
        );
    });

    elements.stopReservoirPumpBtn.addEventListener('click', () => {
        showConfirmation(
            'Are you sure you want to stop the reservoir pump?',
            stopReservoirPump
        );
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
        console.log('Fetching sensor data from API...');
        const response = await fetch(`${API_CONFIG.ENDPOINT}/sensors`);

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        console.log('Received sensor data:', data);

        updateSensorDisplay(data);
        appState.lastUpdate = new Date();

        // Update last update time display
        if (elements.lastUpdateTime) {
            elements.lastUpdateTime.textContent = appState.lastUpdate.toLocaleTimeString();
        }
    } catch (error) {
        console.error('Error fetching sensor data:', error);
        updateConnectionStatus(false);
    }
}

/**
 * Update the sensor readings on the UI
 * @param {Object} data - Sensor data from the API
 */
function updateSensorDisplay(data) {
    console.log('Updating sensor display with:', data);

    // Update environmental sensor readings
    if (data.environmental) {
        console.log('Environmental data:', data.environmental);
        if (data.environmental.success) {
            console.log('Setting environmental values in DOM elements');

            if (elements.envTemperature) {
                const valueElement = elements.envTemperature.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.temperature.toFixed(1);
                    console.log('Updated temperature to:', data.environmental.temperature.toFixed(1));
                } else {
                    console.warn('Temperature value element not found!');
                }
            } else {
                console.warn('envTemperature element not found!');
            }

            if (elements.envHumidity) {
                const valueElement = elements.envHumidity.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.humidity.toFixed(1);
                    console.log('Updated humidity to:', data.environmental.humidity.toFixed(1));
                } else {
                    console.warn('Humidity value element not found!');
                }
            } else {
                console.warn('envHumidity element not found!');
            }

            if (elements.envPressure) {
                const valueElement = elements.envPressure.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.pressure.toFixed(1);
                    console.log('Updated pressure to:', data.environmental.pressure.toFixed(1));
                } else {
                    console.warn('Pressure value element not found!');
                }
            } else {
                console.warn('envPressure element not found!');
            }
        } else {
            console.warn('Environmental sensor read failed:', data.environmental.error);
        }
    } else {
        console.warn('No environmental data in response!');
    }

    // Update soil sensor readings
    if (data.soil) {
        console.log('Soil data:', data.soil);
        if (data.soil.success) {
            console.log('Setting soil values in DOM elements');

            if (elements.soilMoisture) {
                const valueElement = elements.soilMoisture.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.moisture.toFixed(1);
                    console.log('Updated soil moisture to:', data.soil.moisture.toFixed(1));
                }
            }

            if (elements.soilTemperature) {
                const valueElement = elements.soilTemperature.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.temperature.toFixed(1);
                    console.log('Updated soil temperature to:', data.soil.temperature.toFixed(1));
                }
            }

            if (elements.soilPh) {
                const valueElement = elements.soilPh.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.ph.toFixed(1);
                    console.log('Updated soil pH to:', data.soil.ph.toFixed(1));
                }
            }

            if (elements.soilEc) {
                const valueElement = elements.soilEc.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.ec.toFixed(1);
                    console.log('Updated soil EC to:', data.soil.ec.toFixed(1));
                }
            }

            // Only update NPK if present in data and element exists
            if (elements.soilNpk && data.soil.nitrogen !== undefined) {
                const valueElement = elements.soilNpk.querySelector('.reading-value');
                if (valueElement) {
                    const n = data.soil.nitrogen !== undefined ? data.soil.nitrogen.toFixed(1) : '--';
                    const p = data.soil.phosphorus !== undefined ? data.soil.phosphorus.toFixed(1) : '--';
                    const k = data.soil.potassium !== undefined ? data.soil.potassium.toFixed(1) : '--';
                    valueElement.textContent = `N:${n} P:${p} K:${k}`;
                    elements.soilNpk.classList.remove('hidden');
                }
            }
        } else {
            console.warn('Soil sensor read failed:', data.soil.error);
        }
    } else {
        console.warn('No soil data in response!');
    }

    // Update connection status
    updateConnectionStatus(true);
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
    appState.isWatering = data.pumpRunning;
    updateWateringStatus(data.pumpRunning);

    // Update auto watering status
    appState.autoWateringEnabled = data.wateringEnabled;
    elements.autoWateringToggle.checked = data.wateringEnabled;
    elements.autoWateringStatus.textContent = data.wateringEnabled ? 'Enabled' : 'Disabled';

    // Update reservoir status if available
    if (data.reservoir) {
        appState.reservoir.enabled = data.reservoir.enabled;
        appState.reservoir.pumpRunning = data.reservoir.pumpRunning;
        appState.reservoir.lowLevelDetected = data.reservoir.lowLevelDetected;
        appState.reservoir.highLevelDetected = data.reservoir.highLevelDetected;

        updateReservoirStatus(data.reservoir);
    }

    // Update system info
    if (data.network) {
        elements.systemIp.textContent = data.network.ip || '--';
    }

    if (data.storage) {
        const usedPercent = ((data.storage.usedKB / data.storage.totalKB) * 100).toFixed(1);
        elements.storageUsage.textContent = `${usedPercent}% (${data.storage.usedKB} KB / ${data.storage.totalKB} KB)`;
    }

    // Update settings if available
    if (data.config) {
        appState.settings = {
            moistureThresholdLow: data.config.moistureThresholdLow,
            moistureThresholdHigh: data.config.moistureThresholdHigh,
            wateringDuration: data.config.wateringDuration,
            minWateringInterval: data.config.minWateringInterval
        };
        updateSettingsForm(appState.settings);
    }
}

/**
 * Update the connection status display (Tailwind Version)
 * @param {boolean} connected - Whether the system is connected
 */
function updateConnectionStatus(connected) {
    const statusDot = elements.connectionStatus.querySelector('.status-dot');
    const statusText = elements.connectionStatus.querySelector('.status-text');

    if (connected) {
        // Use Tailwind classes or the custom classes defined in <style> or styles.css
        statusDot.classList.remove('status-disconnected');
        statusDot.classList.add('status-connected');
        statusText.textContent = 'Connected';
        statusText.classList.remove('text-red-600'); // Example if using Tailwind colors directly
        statusText.classList.add('text-gray-600');
    } else {
        statusDot.classList.remove('status-connected');
        statusDot.classList.add('status-disconnected');
        statusText.textContent = 'Disconnected';
        statusText.classList.remove('text-gray-600');
        statusText.classList.add('text-red-600'); // Example if using Tailwind colors directly
    }
    appState.connected = connected; // Update state
}

/**
 * Update the watering status display (Tailwind Version)
 * @param {boolean} isWatering - Whether the system is currently watering
 */
function updateWateringStatus(isWatering) {
    const statusDot = elements.wateringStatus.querySelector('.status-dot');
    const statusText = elements.wateringStatus.querySelector('.status-text');

    if (isWatering) {
        statusDot.classList.remove('status-inactive');
        statusDot.classList.add('status-active'); // Or 'status-running' if preferred
        statusText.textContent = 'Watering Active';
        elements.startWateringBtn.disabled = true;
        elements.stopWateringBtn.disabled = false;
    } else {
        statusDot.classList.remove('status-active'); // Or 'status-running'
        statusDot.classList.add('status-inactive');
        statusText.textContent = 'Watering Inactive';
        elements.startWateringBtn.disabled = false;
        elements.stopWateringBtn.disabled = true;
    }
    appState.isWatering = isWatering; // Update state
}

/**
 * Update the reservoir status display (Tailwind Version)
 * @param {Object} reservoirData - Reservoir status data from the API
 */
function updateReservoirStatus(reservoirData) {
    // Update toggle state visually (Tailwind handles the input's appearance)
    elements.reservoirToggle.checked = reservoirData.enabled;
    elements.reservoirStatus.textContent = reservoirData.enabled ? 'Enabled' : 'Disabled';

    // Update buttons and input states based on reservoir status
    elements.startReservoirPumpBtn.disabled = !reservoirData.enabled || reservoirData.pumpRunning || reservoirData.highLevelDetected;
    elements.stopReservoirPumpBtn.disabled = !reservoirData.enabled || !reservoirData.pumpRunning;
    elements.reservoirDurationInput.disabled = !reservoirData.enabled;

    // Update water level indicator bar width and color
    let levelPercentage = 0;
    let levelText = 'Unknown';
    let barColorClass = 'bg-gray-400'; // Default color

    if (reservoirData.highLevelDetected) {
        levelPercentage = 100;
        levelText = 'Full';
        barColorClass = 'bg-blue-600'; // Tailwind blue
    } else if (reservoirData.lowLevelDetected) {
        // Assuming 'lowLevelDetected' means it's *not* critically low, maybe medium?
        // Adjust logic based on actual sensor meaning. Let's assume 50% for medium.
        levelPercentage = 50;
        levelText = 'Medium';
        barColorClass = 'bg-yellow-500'; // Tailwind yellow for medium/warning
    } else {
        // Assuming if neither high nor low is detected, it's critically low or empty
        levelPercentage = 10; // Show a small amount for 'low'
        levelText = 'Low';
        barColorClass = 'bg-red-600'; // Tailwind red for low/alert
    }

    // Apply width style
    elements.waterLevelIndicator.style.width = `${levelPercentage}%`;
    // Remove old color classes and add the new one
    elements.waterLevelIndicator.classList.remove('bg-gray-400', 'bg-blue-600', 'bg-yellow-500', 'bg-red-600');
    elements.waterLevelIndicator.classList.add(barColorClass);

    elements.waterLevelText.textContent = levelText;

    // Update reservoir pump status indicator dot and text
    const pumpStatusDot = elements.reservoirPumpStatus.querySelector('.status-dot');
    const pumpStatusText = elements.reservoirPumpStatus.querySelector('.status-text');

    // Remove previous status classes
    pumpStatusDot.classList.remove('status-inactive', 'status-active', 'status-running', 'status-warning');

    if (reservoirData.pumpRunning) {
        pumpStatusDot.classList.add('status-running'); // Use pulsing animation class
        pumpStatusText.textContent = 'Pump Running';
    } else if (reservoirData.enabled) {
        // Check level for warnings when pump is not running but feature is enabled
        if (levelPercentage <= 10) { // If level is low
            pumpStatusDot.classList.add('status-warning'); // Warning color
            pumpStatusText.textContent = 'Pump Ready (Level Low)';
        } else {
            pumpStatusDot.classList.add('status-active'); // Ready color (e.g., blue)
            pumpStatusText.textContent = 'Pump Ready';
        }
    } else {
        pumpStatusDot.classList.add('status-inactive'); // Inactive color (e.g., gray)
        pumpStatusText.textContent = 'Pump Inactive';
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
 * Show confirmation modal (Tailwind Version)
 * @param {string} message - Confirmation message
 * @param {Function} onConfirm - Function to call on confirmation
 */
function showConfirmation(message, onConfirm) {
    elements.modalMessage.textContent = message;
    // Use Tailwind classes to show the modal
    elements.confirmationModal.classList.remove('hidden');
    elements.confirmationModal.classList.add('flex'); // Use flex for centering

    // Remove any existing event listener to prevent duplicates
    const oldConfirmBtn = elements.modalConfirm;
    const newConfirmBtn = oldConfirmBtn.cloneNode(true);
    oldConfirmBtn.parentNode.replaceChild(newConfirmBtn, oldConfirmBtn);
    elements.modalConfirm = newConfirmBtn; // Update reference

    // Add new event listener
    elements.modalConfirm.addEventListener('click', () => {
        hideConfirmation();
        onConfirm(); // Execute the callback
    }, { once: true }); // Ensure the listener runs only once
}

/**
 * Hide confirmation modal (Tailwind Version)
 */
function hideConfirmation() {
    // Use Tailwind classes to hide the modal
    elements.confirmationModal.classList.add('hidden');
    elements.confirmationModal.classList.remove('flex');
}

/**
 * Show a notification (Tailwind Version)
 * @param {string} title - Notification title
 * @param {string} message - Notification message
 * @param {string} type - Notification type: 'success', 'error', 'warning', 'info'
 */
function showNotification(title, message, type = 'info') {
    const notification = document.createElement('div');
    // Base classes using Tailwind
    notification.className = 'p-4 rounded-md shadow-lg bg-white border-l-4 max-w-sm w-full';

    // Type-specific border color using Tailwind
    let borderColorClass = 'border-blue-500'; // Default to info
    if (type === 'success') borderColorClass = 'border-green-500';
    else if (type === 'error') borderColorClass = 'border-red-500';
    else if (type === 'warning') borderColorClass = 'border-yellow-500';
    notification.classList.add(borderColorClass);

    notification.innerHTML = `
        <div class="flex">
            <div class="flex-shrink-0">
                <!-- Optional Icon based on type -->
                <!-- <svg>...</svg> -->
            </div>
            <div class="ml-3">
                <p class="text-sm font-medium text-gray-900">${title}</p>
                <p class="mt-1 text-sm text-gray-500">${message}</p>
            </div>
            <div class="ml-auto pl-3">
                <div class="-mx-1.5 -my-1.5">
                    <button type="button" class="notification-close inline-flex bg-white rounded-md p-1.5 text-gray-400 hover:text-gray-500 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500">
                        <span class="sr-only">Dismiss</span>
                        <!-- Heroicon name: solid/x -->
                        <svg class="h-5 w-5" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true">
                            <path fill-rule="evenodd" d="M4.293 4.293a1 1 0 011.414 0L10 8.586l4.293-4.293a1 1 0 111.414 1.414L11.414 10l4.293 4.293a1 1 0 01-1.414 1.414L10 11.414l-4.293 4.293a1 1 0 01-1.414-1.414L8.586 10 4.293 5.707a1 1 0 010-1.414z" clip-rule="evenodd" />
                        </svg>
                    </button>
                </div>
            </div>
        </div>
    `;

    elements.notificationContainer.appendChild(notification);

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
 * Remove a notification (Tailwind Version)
 * @param {Element} notification - Notification element to remove
 */
function removeNotification(notification) {
    // Add a class to trigger fade-out animation (defined in styles.css or inline)
    notification.classList.add('fade-out');
    // Remove the element after the animation completes
    setTimeout(() => {
        if (notification.parentNode) { // Check if it hasn't already been removed
            notification.remove();
        }
    }, 300); // Match animation duration
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
 * Enable or disable the reservoir pump feature via API
 * @param {boolean} enabled - Whether the reservoir pump feature should be enabled
 */
async function setReservoirPumpEnabled(enabled) {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ command: enabled ? 'enable' : 'disable' })
        });

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        if (data.success) {
            appState.reservoir.enabled = enabled;
            showNotification('Reservoir Pump', `Reservoir pump feature has been ${enabled ? 'enabled' : 'disabled'}.`, 'info');

            // Update UI immediately for better responsiveness
            updateReservoirStatus({
                ...appState.reservoir,
                enabled: enabled
            });

            // Fetch full status to ensure everything is in sync
            fetchSystemStatus();
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error setting reservoir pump feature:', error);
        showNotification('Reservoir Error', `Failed to ${enabled ? 'enable' : 'disable'} reservoir pump feature: ${error.message}`, 'error');

        // Revert UI to previous state
        elements.reservoirToggle.checked = appState.reservoir.enabled;
    }
}

/**
 * Start reservoir filling via API
 * @param {number} duration - Filling duration in seconds (0 for automatic stop at high level)
 */
async function startReservoirFilling(duration) {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                command: 'start',
                duration: parseInt(duration) || 0
            })
        });

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        if (data.success) {
            if (parseInt(duration) > 0) {
                showNotification('Reservoir Filling Started', `Reservoir filling started for ${duration} seconds.`, 'success');
            } else {
                showNotification('Reservoir Filling Started', 'Reservoir filling started (will stop automatically when full).', 'success');
            }

            // Update UI immediately for better responsiveness
            updateReservoirStatus({
                ...appState.reservoir,
                pumpRunning: true
            });

            // Fetch full status to ensure everything is in sync
            fetchSystemStatus();
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error starting reservoir filling:', error);
        showNotification('Reservoir Error', `Failed to start reservoir filling: ${error.message}`, 'error');
    }
}

/**
 * Stop reservoir pump via API
 */
async function stopReservoirPump() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ command: 'stop' })
        });

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        if (data.success) {
            showNotification('Reservoir Pump Stopped', 'Reservoir pump has been stopped.', 'info');

            // Update UI immediately for better responsiveness
            updateReservoirStatus({
                ...appState.reservoir,
                pumpRunning: false
            });

            // Fetch full status to ensure everything is in sync
            fetchSystemStatus();
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error stopping reservoir pump:', error);
        showNotification('Reservoir Error', `Failed to stop reservoir pump: ${error.message}`, 'error');
    }
}

// Initialize the application when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', initApp);