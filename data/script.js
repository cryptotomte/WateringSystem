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
    },    reservoir: {
        enabled: false,
        autoLevelControlEnabled: false,
        pumpRunning: false,
        lowLevelDetected: false,
        highLevelDetected: false
    }
};

// DOM elements - will be populated when DOM is ready
let elements = {}

/**
 * Populate DOM element references
 */
function populateElements() {
    console.log('Populating DOM element references...');
    
    elements = {
        // Status displays
        connectionStatus: document.getElementById('connection-status'),
        wateringStatus: document.getElementById('watering-status'),
        lastUpdateTime: document.getElementById('last-update-time'),
        
        // Environmental readings
        envTemperature: document.getElementById('env-temperature'),
        envHumidity: document.getElementById('env-humidity'),
        envPressure: document.getElementById('env-pressure'),
        
        // Soil readings
        soilMoisture: document.getElementById('soil-moisture'),
        soilTemperature: document.getElementById('soil-temperature'),
        soilPh: document.getElementById('soil-ph'),
        soilEc: document.getElementById('soil-ec'),
        soilNpk: document.getElementById('soil-npk'),
        
        // Control buttons and inputs
        refreshDataBtn: document.getElementById('refresh-data'),
        wateringDurationInput: document.getElementById('watering-duration-input'),
        startWateringBtn: document.getElementById('start-watering'),
        stopWateringBtn: document.getElementById('stop-watering'),
        
        // Auto watering
        autoWateringStatus: document.getElementById('auto-watering-status'),
        autoWateringToggle: document.getElementById('auto-watering-toggle'),
          // Reservoir controls
        reservoirStatus: document.getElementById('reservoir-status'),
        reservoirToggle: document.getElementById('reservoir-toggle'),
        reservoirAutoLevelStatus: document.getElementById('reservoir-auto-level-status'),
        reservoirAutoLevelToggle: document.getElementById('reservoir-auto-level-toggle'),
        waterLevelText: document.getElementById('water-level-text'),
        waterLevelIndicator: document.getElementById('water-level-indicator'),
        reservoirPumpStatus: document.getElementById('reservoir-pump-status'),
        reservoirDurationInput: document.getElementById('reservoir-duration-input'),
        startReservoirPumpBtn: document.getElementById('start-reservoir-pump'),
        stopReservoirPumpBtn: document.getElementById('stop-reservoir-pump'),
        
        // Chart controls
        chartSensor: document.getElementById('chart-sensor'),
        chartReading: document.getElementById('chart-reading'),
        chartTimeRange: document.getElementById('chart-time-range'),
        dataChart: document.getElementById('data-chart'),
        
        // Settings
        settingsForm: document.getElementById('settings-form'),
        moistureThresholdLow: document.getElementById('moisture-threshold-low'),
        moistureThresholdHigh: document.getElementById('moisture-threshold-high'),
        wateringDurationSetting: document.getElementById('watering-duration-setting'),
        minWateringInterval: document.getElementById('min-watering-interval'),
        
        // System info
        systemIp: document.getElementById('system-ip'),
        storageUsage: document.getElementById('storage-usage'),
        
        // Modals and notifications
        notificationContainer: document.getElementById('notification-container'),
        confirmationModal: document.getElementById('confirmation-modal'),
        modalMessage: document.getElementById('modal-message'),
        modalCancel: document.getElementById('modal-cancel'),
        modalConfirm: document.getElementById('modal-confirm'),
        closeModal: document.getElementById('modal-cancel') // Using cancel button as close
    };
    
    // Log any missing elements
    const missingElements = [];
    for (const [key, element] of Object.entries(elements)) {
        if (!element) {
            missingElements.push(key);
        }
    }
    
    if (missingElements.length > 0) {
        console.warn('Missing DOM elements:', missingElements);
    } else {
        console.log('All DOM elements found successfully');
    }
}

/**
 * Initialize the application
 */
function initApp() {
    console.log('initApp() called');
    
    // Populate DOM element references first
    populateElements();
    console.log('DOM elements populated');
    
    // Set up event listeners
    setupEventListeners();
    console.log('Event listeners set up');

    // Load settings
    loadSettings();
    console.log('Settings loaded');

    // Initialize chart
    initChart();
    console.log('Chart initialized');

    // Fetch initial data
    console.log('Starting initial data fetch...');
    fetchSensorData();
    fetchSystemStatus();
    fetchHistoricalData();

    // Start auto-refresh
    startAutoRefresh();
    console.log('Auto-refresh started');

    // Show welcome notification
    showNotification('Welcome to WateringSystem', 'The system is ready to monitor and control your plants.', 'info');
    console.log('initApp() completed');
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
        const duration = elements.wateringDurationInput.value;
        startWatering(duration);

        /* Original code with confirmation modal:
        showConfirmation(
            'Are you sure you want to start watering?',
            () => startWatering(elements.wateringDurationInput.value)
        );
        */
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
    });    // Reservoir controls
    elements.reservoirToggle.addEventListener('change', (e) => {
        setReservoirPumpEnabled(e.target.checked);
    });

    elements.reservoirAutoLevelToggle.addEventListener('change', (e) => {
        setReservoirAutoLevelControl(e.target.checked);
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
    console.log('fetchSensorData() called');
    try {
        console.log('Fetching from:', `${API_CONFIG.ENDPOINT}/sensors`);
        const response = await fetch(`${API_CONFIG.ENDPOINT}/sensors`);

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        console.log('Sensor data received:', data);
        updateSensorDisplay(data);
        appState.lastUpdate = new Date();

        // Update last update time display
        if (elements.lastUpdateTime) {
            elements.lastUpdateTime.textContent = appState.lastUpdate.toLocaleTimeString();
        }
        console.log('fetchSensorData() completed successfully');
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
    // Update environmental sensor readings
    if (data.environmental) {
        if (data.environmental.success) {
            if (elements.envTemperature) {
                const valueElement = elements.envTemperature.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.temperature.toFixed(1);
                }
            }

            if (elements.envHumidity) {
                const valueElement = elements.envHumidity.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.humidity.toFixed(1);
                }
            }

            if (elements.envPressure) {
                const valueElement = elements.envPressure.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.environmental.pressure.toFixed(1);
                }
            }
        } else {
            console.warn('Environmental sensor read failed:', data.environmental.error);
        }
    }

    // Update soil sensor readings
    if (data.soil) {
        if (data.soil.success) {
            if (elements.soilMoisture) {
                const valueElement = elements.soilMoisture.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.moisture.toFixed(1);
                }
            }

            if (elements.soilTemperature) {
                const valueElement = elements.soilTemperature.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.temperature.toFixed(1);
                }
            }

            if (elements.soilPh) {
                const valueElement = elements.soilPh.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.ph.toFixed(1);
                }
            }

            if (elements.soilEc) {
                const valueElement = elements.soilEc.querySelector('.reading-value');
                if (valueElement) {
                    valueElement.textContent = data.soil.ec.toFixed(1);
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
    }

    // Update connection status
    updateConnectionStatus(true);
}

/**
 * Fetch system status from the API
 */
async function fetchSystemStatus() {
    console.log('fetchSystemStatus() called');
    try {
        console.log('Fetching from:', `${API_CONFIG.ENDPOINT}/status`);
        const response = await fetch(`${API_CONFIG.ENDPOINT}/status`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }        const data = await response.json();        console.log('System status received:', data);
        updateSystemStatus(data);
        
        // Update connection status based on actual WiFi connection from server data
        const wifiConnected = data.network && data.network.connected;
        console.log('DEBUG: Network data:', data.network);
        console.log('DEBUG: WiFi connected:', wifiConnected);
        updateConnectionStatus(wifiConnected);
        console.log('fetchSystemStatus() completed successfully');
    } catch (error) {
        console.error('Error fetching system status:', error);
        updateConnectionStatus(false);
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
    updateWateringStatus(data.pumpRunning, data.remainingTime);

    // Update auto watering status
    appState.autoWateringEnabled = data.wateringEnabled;
    elements.autoWateringToggle.checked = data.wateringEnabled;
    elements.autoWateringStatus.textContent = data.wateringEnabled ? 'Enabled' : 'Disabled';    // Update reservoir status if available
    if (data.reservoir) {
        appState.reservoir.enabled = data.reservoir.enabled;
        appState.reservoir.autoLevelControlEnabled = data.reservoir.autoLevelControlEnabled || false;
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
    console.log('updateConnectionStatus() called with:', connected);
    const statusDot = elements.connectionStatus.querySelector('.status-dot');
    const statusText = elements.connectionStatus.querySelector('.status-text');
    const isDarkMode = document.documentElement.classList.contains('dark');

    console.log('Status dot element:', statusDot);
    console.log('Status text element:', statusText);

    // Tailwind classes for status dots are already handled by .dark prefix in HTML/CSS
    // We just need to ensure text color is appropriate if not covered by a general rule.
    if (connected) {
        statusDot.classList.remove('status-disconnected');
        statusDot.classList.add('status-connected');
        statusText.textContent = 'Connected';
        console.log('Set status to connected');
        // Text color for status is now text-slate-400 in dark mode (from HTML)
        // and text-gray-600 in light mode (from HTML).
        // No specific JS change needed here if HTML is correctly styled.
    } else {
        statusDot.classList.remove('status-connected');
        statusDot.classList.add('status-disconnected');
        statusText.textContent = 'Disconnected';
        console.log('Set status to disconnected');
        // As above, HTML should handle text color.
    }
    appState.connected = connected;
    console.log('appState.connected set to:', appState.connected);
}

/**
 * Update the watering status display with timer
 * @param {boolean} isWatering - Whether the system is currently watering
 * @param {number} remainingTime - Remaining time in seconds (optional)
 */
function updateWateringStatus(isWatering, remainingTime) {
    const statusDot = elements.wateringStatus.querySelector('.status-dot');
    const statusText = elements.wateringStatus.querySelector('.status-text');
    const isDarkMode = document.documentElement.classList.contains('dark');

    if (isWatering) {
        statusDot.classList.remove('status-inactive');
        statusDot.classList.add('status-active'); // This class should be styled by .dark in CSS

        if (appState.clientCountdownTimer) {
            clearInterval(appState.clientCountdownTimer);
            appState.clientCountdownTimer = null;
        }

        if (remainingTime !== undefined && remainingTime > 0) {
            appState.countdownEndTime = Date.now() + (remainingTime * 1000);
            const textColorClass = isDarkMode ? 'text-blue-400' : 'text-blue-600'; // Adjusted for dark mode
            statusText.innerHTML = `Watering Active <span class="font-medium ${textColorClass}">(${remainingTime}s remaining)</span>`;

            appState.clientCountdownTimer = setInterval(() => {
                const now = Date.now();
                const remaining = Math.max(0, Math.ceil((appState.countdownEndTime - now) / 1000));
                if (remaining > 0) {
                    statusText.innerHTML = `Watering Active <span class="font-medium ${textColorClass}">(${remaining}s remaining)</span>`;
                } else {
                    clearInterval(appState.clientCountdownTimer);
                    appState.clientCountdownTimer = null;
                    fetchSystemStatus();
                }
            }, 1000);

            if (!appState.countdownServerTimer) {
                appState.countdownServerTimer = setInterval(() => {
                    fetchSystemStatus();
                }, 5000);
            }
        } else {
            statusText.textContent = 'Watering Active';
        }
        elements.startWateringBtn.disabled = true;
        elements.stopWateringBtn.disabled = false;
    } else {
        statusDot.classList.remove('status-active');
        statusDot.classList.add('status-inactive'); // This class should be styled by .dark in CSS
        statusText.textContent = 'Watering Inactive';
        elements.startWateringBtn.disabled = false;
        elements.stopWateringBtn.disabled = true;

        if (appState.clientCountdownTimer) {
            clearInterval(appState.clientCountdownTimer);
            appState.clientCountdownTimer = null;
        }
        if (appState.countdownServerTimer) {
            clearInterval(appState.countdownServerTimer);
            appState.countdownServerTimer = null;
        }
    }
    appState.isWatering = isWatering;
}

/**
 * Update the reservoir status display (Tailwind Version adapted for Dark Mode)
 * @param {Object} reservoirData - Reservoir status data from the API
 */
function updateReservoirStatus(reservoirData) {
    elements.reservoirToggle.checked = reservoirData.enabled;
    elements.reservoirStatus.textContent = reservoirData.enabled ? 'Enabled' : 'Disabled';

    // Update automatic level control toggle
    elements.reservoirAutoLevelToggle.checked = reservoirData.autoLevelControlEnabled || false;
    elements.reservoirAutoLevelStatus.textContent = reservoirData.autoLevelControlEnabled ? 'Enabled' : 'Disabled';
    elements.reservoirAutoLevelToggle.disabled = !reservoirData.enabled;

    elements.startReservoirPumpBtn.disabled = !reservoirData.enabled || reservoirData.pumpRunning || reservoirData.highLevelDetected;
    elements.stopReservoirPumpBtn.disabled = !reservoirData.enabled || !reservoirData.pumpRunning;
    elements.reservoirDurationInput.disabled = !reservoirData.enabled;

    let levelPercentage = 0;
    let levelText = 'Unknown';
    let barColorClass;
    const isDarkMode = document.documentElement.classList.contains('dark');    // Define color classes for different states
    const blueColor = isDarkMode ? 'bg-blue-400' : 'bg-blue-600';
    const yellowColor = isDarkMode ? 'bg-amber-400' : 'bg-yellow-500';
    const redColor = isDarkMode ? 'bg-red-400' : 'bg-red-600';
    const grayColor = isDarkMode ? 'bg-gray-500' : 'bg-gray-400';
    const brandAccent = 'bg-blue-500'; // Brand accent color

    if (reservoirData.highLevelDetected) {
        levelPercentage = 100;
        levelText = 'Full';
        barColorClass = brandAccent; // Use brand accent for full
    } else if (reservoirData.lowLevelDetected) {
        levelPercentage = 50; // Assuming low means not empty, but needs attention
        levelText = 'Medium';
        barColorClass = yellowColor;
    } else { // Neither high nor low detected, assume critically low or empty
        levelPercentage = 10;
        levelText = 'Low';
        barColorClass = redColor;
    }

    // If reservoir is disabled, show a neutral/empty state for the bar
    if (!reservoirData.enabled) {
        levelPercentage = 0;
        levelText = 'Disabled';
        barColorClass = grayColor;    }

    elements.waterLevelIndicator.style.width = `${levelPercentage}%`;
    // Remove all potential background color classes and add the new one
    const colorClassesToRemove = ['bg-blue-600', 'bg-yellow-500', 'bg-red-600', 'bg-gray-400', 'bg-blue-400', 'bg-amber-400', 'bg-red-400', 'bg-gray-500', 'bg-blue-500'];
    elements.waterLevelIndicator.classList.remove(...colorClassesToRemove);
    elements.waterLevelIndicator.classList.add(barColorClass);


    elements.waterLevelText.textContent = levelText;

    const pumpStatusDot = elements.reservoirPumpStatus.querySelector('.status-dot');
    const pumpStatusText = elements.reservoirPumpStatus.querySelector('.status-text');

    pumpStatusDot.classList.remove('status-inactive', 'status-active', 'status-running', 'status-warning');

    if (reservoirData.pumpRunning) {
        pumpStatusDot.classList.add('status-running');
        pumpStatusText.textContent = 'Pump Running';
    } else if (reservoirData.enabled) {
        if (levelPercentage <= 10 && levelText === 'Low') { // Check if level is critically low
            pumpStatusDot.classList.add('status-warning');
            pumpStatusText.textContent = 'Pump Ready (Level Low)';
        } else {
            pumpStatusDot.classList.add('status-active');
            pumpStatusText.textContent = 'Pump Ready';
        }
    } else {
        pumpStatusDot.classList.add('status-inactive');
        pumpStatusText.textContent = 'Pump Inactive (Disabled)';
    }
}

/**
 * Enable or disable auto-watering via API
 * @param {boolean} enabled - Whether auto-watering should be enabled
 */
async function setAutoWatering(enabled) {
    try {
        const formData = new URLSearchParams();
        formData.append('enabled', enabled.toString());
        
        const response = await fetch(`${API_CONFIG.ENDPOINT}/control/auto`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
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

    // Debug info - Log what we're about to send
    console.log('Sending settings to server:', settings);
    console.log('Using endpoint:', `${API_CONFIG.ENDPOINT}/config`);

    try {
        // Try with form data first (which is known to work in other endpoints)
        const formData = new URLSearchParams();
        formData.append('moistureThresholdLow', settings.moistureThresholdLow);
        formData.append('moistureThresholdHigh', settings.moistureThresholdHigh);
        formData.append('wateringDuration', settings.wateringDuration);
        formData.append('minWateringInterval', settings.minWateringInterval);

        console.log('Sending as URL-encoded form data');

        const response = await fetch(`${API_CONFIG.ENDPOINT}/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
        });

        // Debug info - Log response details
        console.log('Response status:', response.status);

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        console.log('Response data:', data);

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
    const isDarkMode = document.documentElement.classList.contains('dark');

    // Define colors based on dark mode
    const gridColor = isDarkMode ? 'rgba(100, 116, 139, 0.2)' : 'rgba(203, 213, 225, 0.5)'; // slate-600/slate-300 with opacity
    const textColor = isDarkMode ? '#cbd5e1' : '#4b5563'; // slate-300/slate-600
    const legendColor = isDarkMode ? '#e2e8f0' : '#334155'; // slate-200/slate-700
    const tooltipBgColor = isDarkMode ? 'rgba(30, 41, 59, 0.9)' : 'rgba(255, 255, 255, 0.9)'; // slate-800/white
    const tooltipTitleColor = isDarkMode ? '#f1f5f9' : '#1e293b'; // slate-100/slate-900
    const tooltipBodyColor = isDarkMode ? '#cbd5e1' : '#374151'; // slate-300/slate-700
    const datasetBorderColor = '#3b82f6'; // blue-500
    const datasetBackgroundColor = 'rgba(59, 130, 246, 0.2)'; // blue-500 with 20% opacity

    appState.chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Sensor Reading',
                data: [],
                borderColor: datasetBorderColor,
                backgroundColor: datasetBackgroundColor,
                borderWidth: 2,
                tension: 0.2,
                fill: true,
                pointBackgroundColor: datasetBorderColor,
                pointBorderColor: isDarkMode ? '#0f172a' : '#ffffff', // slate-900 or white for point borders
                pointHoverBackgroundColor: isDarkMode ? '#0f172a' : '#ffffff',
                pointHoverBorderColor: datasetBorderColor,
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
                        text: 'Time',
                        color: textColor
                    },
                    ticks: {
                        color: textColor,
                        maxRotation: 0,
                        autoSkipPadding: 20,
                    },
                    grid: {
                        color: gridColor,
                        borderColor: gridColor // Ensure border color also matches
                    }
                },
                y: {
                    beginAtZero: false, // Keep this dynamic based on data
                    title: {
                        display: true,
                        text: 'Value',
                        color: textColor
                    },
                    ticks: {
                        color: textColor
                    },
                    grid: {
                        color: gridColor,
                        borderColor: gridColor // Ensure border color also matches
                    }
                }
            },
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    labels: {
                        color: legendColor,
                        usePointStyle: true,
                        boxWidth: 8
                    }
                },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    backgroundColor: tooltipBgColor,
                    titleColor: tooltipTitleColor,
                    bodyColor: tooltipBodyColor,
                    borderColor: gridColor,
                    borderWidth: 1,
                    padding: 10,
                    cornerRadius: 4,
                    displayColors: true, // Show color box in tooltip
                    boxPadding: 3
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
    const isDarkMode = document.documentElement.classList.contains('dark');
    const textColor = isDarkMode ? '#cbd5e1' : '#4b5563'; // slate-300/slate-600

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

    // Update colors if mode changed (though initChart should handle initial setup)
    appState.chart.options.scales.x.title.color = textColor;
    appState.chart.options.scales.x.ticks.color = textColor;
    appState.chart.options.scales.y.title.color = textColor;
    appState.chart.options.scales.y.ticks.color = textColor;
    appState.chart.options.plugins.legend.labels.color = isDarkMode ? '#e2e8f0' : '#334155';


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
 * Show a notification (Tailwind Version adapted for Dark Mode)
 * @param {string} title - Notification title
 * @param {string} message - Notification message
 * @param {string} type - Notification type: 'success', 'error', 'warning', 'info'
 */
function showNotification(title, message, type = 'info') {
    const notification = document.createElement('div');
    const isDarkMode = document.documentElement.classList.contains('dark');

    // Base classes using Tailwind, adapted for dark mode
    let baseClasses = 'p-4 rounded-md shadow-lg border-l-4 max-w-sm w-full';
    let titleColorClass = isDarkMode ? 'text-slate-100' : 'text-gray-900';
    let messageColorClass = isDarkMode ? 'text-slate-300' : 'text-gray-500';
    let bgColorClass = isDarkMode ? 'bg-slate-700' : 'bg-white';
    let closeButtonHoverColor = isDarkMode ? 'text-slate-200' : 'text-gray-500';
    let closeButtonColor = isDarkMode ? 'text-slate-400' : 'text-gray-400';
    let closeButtonBg = isDarkMode ? 'bg-slate-700' : 'bg-white'; // Match notification bg
    let closeButtonFocusRing = isDarkMode ? 'focus:ring-offset-slate-700 focus:ring-brand-accent' : 'focus:ring-offset-white focus:ring-indigo-500';


    notification.className = `${baseClasses} ${bgColorClass}`;    // Type-specific border color using standard Tailwind classes
    let borderColorClass;
    switch (type) {
        case 'success':
            borderColorClass = isDarkMode ? 'border-green-400' : 'border-green-500';
            break;
        case 'error':
            borderColorClass = isDarkMode ? 'border-red-400' : 'border-red-500';
            break;
        case 'warning':
            borderColorClass = isDarkMode ? 'border-yellow-400' : 'border-yellow-500';
            break;
        case 'info':
        default:
            borderColorClass = isDarkMode ? 'border-blue-400' : 'border-blue-500';
            break;
    }
    notification.classList.add(borderColorClass);

    notification.innerHTML = `
        <div class="flex">
            <div class="flex-shrink-0">
                <!-- Optional Icon can be added here, styled for dark mode -->
            </div>
            <div class="ml-3">
                <p class="text-sm font-medium ${titleColorClass}">${title}</p>
                <p class="mt-1 text-sm ${messageColorClass}">${message}</p>
            </div>
            <div class="ml-auto pl-3">
                <div class="-mx-1.5 -my-1.5">
                    <button type="button" class="notification-close inline-flex ${closeButtonBg} rounded-md p-1.5 ${closeButtonColor} hover:${closeButtonHoverColor} focus:outline-none focus:ring-2 focus:ring-offset-2 ${closeButtonFocusRing}">
                        <span class="sr-only">Dismiss</span>
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
        // Convert duration to integer or use default
        const durationVal = parseInt(duration) || 20;

        // Create form data instead of JSON to avoid server-side crash
        const formData = new URLSearchParams();
        formData.append('duration', durationVal);

        console.log(`Starting watering for ${durationVal} seconds using form data`);

        const response = await fetch(`${API_CONFIG.ENDPOINT}/control/water/start`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
        });

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();

        if (data.success) {
            showNotification('Watering Started', `Watering started for ${durationVal} seconds.`, 'success');
            // Update UI immediately for better responsiveness
            updateWateringStatus(true);

            // Immediately fetch system status to get remaining time
            await fetchSystemStatus();
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
        // Use form data instead of JSON for better compatibility
        const formData = new URLSearchParams();
        formData.append('command', enabled ? 'enable' : 'disable');

        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
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
        // Use form data instead of JSON for better compatibility
        const formData = new URLSearchParams();
        formData.append('command', 'start');
        formData.append('duration', parseInt(duration) || 0);

        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
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
        const formData = new URLSearchParams();
        formData.append('command', 'stop');
        
        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
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

/**
 * Enable or disable automatic reservoir level control via API
 * @param {boolean} enabled - Whether automatic level control should be enabled
 */
async function setReservoirAutoLevelControl(enabled) {
    try {
        // Use form data for better compatibility
        const formData = new URLSearchParams();
        formData.append('command', enabled ? 'enable-auto-level' : 'disable-auto-level');

        const response = await fetch(`${API_CONFIG.ENDPOINT}/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: formData
        });

        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }

        const data = await response.json();
        if (data.success) {
            appState.reservoir.autoLevelControlEnabled = enabled;
            showNotification('Auto Level Control', `Automatic level control has been ${enabled ? 'enabled' : 'disabled'}.`, 'info');

            // Update UI immediately for better responsiveness
            updateReservoirStatus({
                ...appState.reservoir,
                autoLevelControlEnabled: enabled
            });

            // Fetch full status to ensure everything is in sync
            fetchSystemStatus();
        } else {
            throw new Error(data.message || 'Unknown error');
        }
    } catch (error) {
        console.error('Error setting automatic level control:', error);
        showNotification('Reservoir Error', `Failed to ${enabled ? 'enable' : 'disable'} automatic level control: ${error.message}`, 'error');

        // Revert UI to previous state
        elements.reservoirAutoLevelToggle.checked = appState.reservoir.autoLevelControlEnabled;
    }
}

// Initialize the application when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', function() {
    console.log('DOM loaded, initializing app...');
    initApp();
});