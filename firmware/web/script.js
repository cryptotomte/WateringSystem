/**
 * JavaScript for WateringSystem web interface
 * Handles API communication, UI updates, and user interactions
 */

// Configuration
const API_CONFIG = {
    ENDPOINT: '/api/v1',  // Versioned HTTP API v1 prefix (feature 009 contract)
    REFRESH_INTERVAL: 5000, // 5 seconds - fast sensor updates
    STATUS_REFRESH_INTERVAL: 5000, // 5 seconds - system status updates (same as sensors for water level responsiveness)
    CHART_REFRESH_INTERVAL: 120000, // 2 minutes - chart updates
};

// Global state
const appState = {
    connected: false,
    autoWateringEnabled: false,
    isWatering: false,
    lastUpdate: null,
    refreshTimer: null,
    statusRefreshTimer: null,
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
        reservoirSection: document.getElementById('reservoir-section'),
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
        otaUpdateBtn: document.getElementById('ota-update-btn'),

        // Modals and notifications
        notificationContainer: document.getElementById('notification-container'),
        confirmationModal: document.getElementById('confirmation-modal'),
        modalMessage: document.getElementById('modal-message'),
        modalCancel: document.getElementById('modal-cancel'),
        modalConfirm: document.getElementById('modal-confirm'),
        closeModal: document.getElementById('modal-cancel'), // Using cancel button as close

        // Theme toggle
        themeToggle: document.getElementById('theme-toggle'),

        // Loading states
        sensorsLoading: document.getElementById('sensors-loading'),
        sensorsContent: document.getElementById('sensors-content'),
        sensorsSkeleton: document.getElementById('sensors-skeleton'),
        chartLoading: document.getElementById('chart-loading'),
        chartSkeleton: document.getElementById('chart-skeleton')
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

    // Initialize theme from localStorage or system preference
    initTheme();
    console.log('Theme initialized');

    // Set up event listeners
    setupEventListeners();
    console.log('Event listeners set up');

    // Load settings
    loadSettings();
    console.log('Settings loaded');

    // Initialize chart
    initChart();
    console.log('Chart initialized');

    // Initialize chart options (populate reading dropdown based on selected sensor)
    updateChartOptions();
    console.log('Chart options initialized');

    // Fetch initial data
    console.log('Starting initial data fetch...');
    fetchSensorData();
    fetchSystemStatus();
    // v1: config lives at its own endpoint (was inside /status)
    fetchConfig();
    // Note: fetchHistoricalData() is called by updateChartOptions() above

    // Start auto-refresh
    startAutoRefresh();
    console.log('Auto-refresh started');

    // Show welcome notification
    showNotification('Welcome to WateringSystem', 'The system is ready to monitor and control your plants.', 'info');
    console.log('initApp() completed');
}

/**
 * Initialize theme from localStorage or system preference
 */
function initTheme() {
    const savedTheme = localStorage.getItem('theme');
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;

    if (savedTheme === 'light') {
        document.documentElement.classList.remove('dark');
    } else if (savedTheme === 'dark') {
        document.documentElement.classList.add('dark');
    } else {
        // Use system preference if no saved preference
        if (prefersDark) {
            document.documentElement.classList.add('dark');
        } else {
            document.documentElement.classList.remove('dark');
        }
    }
}

/**
 * Toggle between dark and light theme
 */
function toggleTheme() {
    const isDark = document.documentElement.classList.toggle('dark');
    localStorage.setItem('theme', isDark ? 'dark' : 'light');

    // Update chart colors if chart exists
    if (appState.chart) {
        updateChartTheme();
    }
}

/**
 * Update chart colors when theme changes
 */
function updateChartTheme() {
    if (!appState.chart) return;

    const isDarkMode = document.documentElement.classList.contains('dark');
    const textColor = isDarkMode ? '#cbd5e1' : '#4b5563';
    const gridColor = isDarkMode ? 'rgba(100, 116, 139, 0.2)' : 'rgba(203, 213, 225, 0.5)';

    appState.chart.options.scales.x.grid.color = gridColor;
    appState.chart.options.scales.y.grid.color = gridColor;
    appState.chart.options.scales.x.ticks.color = textColor;
    appState.chart.options.scales.y.ticks.color = textColor;
    appState.chart.options.scales.x.title.color = textColor;
    appState.chart.options.scales.y.title.color = textColor;
    appState.chart.options.plugins.legend.labels.color = isDarkMode ? '#e2e8f0' : '#334155';

    appState.chart.update();
}

/**
 * Show loading state for sensors
 */
function showSensorsLoading() {
    if (elements.sensorsLoading) elements.sensorsLoading.classList.remove('hidden');
}

/**
 * Hide loading state for sensors
 */
function hideSensorsLoading() {
    if (elements.sensorsLoading) elements.sensorsLoading.classList.add('hidden');
}

/**
 * Show loading state for chart
 */
function showChartLoading() {
    if (elements.chartLoading) elements.chartLoading.classList.remove('hidden');
    if (elements.chartSkeleton) elements.chartSkeleton.classList.remove('hidden');
}

/**
 * Hide loading state for chart
 */
function hideChartLoading() {
    if (elements.chartLoading) elements.chartLoading.classList.add('hidden');
    if (elements.chartSkeleton) elements.chartSkeleton.classList.add('hidden');
}

/**
 * Set up event listeners for UI elements
 */
function setupEventListeners() {
    // Theme toggle
    if (elements.themeToggle) {
        elements.themeToggle.addEventListener('click', toggleTheme);
    }

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
    });

    // Reservoir controls — v1 has no enable/disable or auto-level-control
    // endpoints (the reservoir is a plain pump + level sensors), so only the
    // manual start/stop controls remain; the enable + auto-level toggles are
    // hidden in index.html and no longer wired.
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

    // Firmware OTA (contract stub until PR-13)
    if (elements.otaUpdateBtn) {
        elements.otaUpdateBtn.addEventListener('click', triggerOtaUpdate);
    }

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

    if (appState.statusRefreshTimer) {
        clearInterval(appState.statusRefreshTimer);
    }

    if (appState.chartRefreshTimer) {
        clearInterval(appState.chartRefreshTimer);
    }

    // Set new timers with different intervals
    appState.refreshTimer = setInterval(() => {
        fetchSensorData();
    }, API_CONFIG.REFRESH_INTERVAL);

    appState.statusRefreshTimer = setInterval(() => {
        fetchSystemStatus();
    }, API_CONFIG.STATUS_REFRESH_INTERVAL);

    appState.chartRefreshTimer = setInterval(() => {
        fetchHistoricalData();
    }, API_CONFIG.CHART_REFRESH_INTERVAL);
}

/**
 * Fetch current sensor data from the API
 */
async function fetchSensorData() {
    console.log('fetchSensorData() called');
    showSensorsLoading();
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
    } finally {
        hideSensorsLoading();
    }
}

/**
 * Update the sensor readings on the UI
 * @param {Object} data - Sensor data from the API
 */
function updateSensorDisplay(data) {
    // Null-safe writer for a `.reading-value` element. v1 fields are nullable
    // (present but null until a reader is valid); never call .toFixed on null.
    const setReading = (element, value, digits = 1) => {
        if (!element) return;
        const valueElement = element.querySelector('.reading-value');
        if (!valueElement) return;
        valueElement.textContent = (typeof value === 'number' && isFinite(value))
            ? value.toFixed(digits)
            : '--';
    };

    // Update environmental sensor readings (v1: `valid` flag + nullable fields)
    if (data.environmental) {
        const env = data.environmental;
        if (env.valid) {
            setReading(elements.envTemperature, env.temperature);
            setReading(elements.envHumidity, env.humidity);
            setReading(elements.envPressure, env.pressure);
        } else {
            // Placeholder when the reading is not valid
            setReading(elements.envTemperature, null);
            setReading(elements.envHumidity, null);
            setReading(elements.envPressure, null);
        }
    }

    // Update soil sensor readings (v1: `valid` flag — false until PR-11 wires
    // the periodic reader — + nullable fields; NPK present only when reported)
    if (data.soil) {
        const soil = data.soil;
        if (soil.valid) {
            setReading(elements.soilMoisture, soil.moisture);
            setReading(elements.soilTemperature, soil.temperature);
            setReading(elements.soilPh, soil.ph);
            setReading(elements.soilEc, soil.ec);

            // Show NPK only when present in data; hide/blank it otherwise so a
            // stale N:.. P:.. K:.. never lingers when the field drops out.
            if (elements.soilNpk) {
                const valueElement = elements.soilNpk.querySelector('.reading-value');
                if (soil.nitrogen !== undefined && soil.nitrogen !== null) {
                    if (valueElement) {
                        const fmt = (v) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(1) : '--';
                        valueElement.textContent = `N:${fmt(soil.nitrogen)} P:${fmt(soil.phosphorus)} K:${fmt(soil.potassium)}`;
                    }
                    elements.soilNpk.classList.remove('hidden');
                } else {
                    if (valueElement) valueElement.textContent = 'N:-- P:-- K:--';
                    elements.soilNpk.classList.add('hidden');
                }
            }
        } else {
            // Placeholder when the reading is not valid
            setReading(elements.soilMoisture, null);
            setReading(elements.soilTemperature, null);
            setReading(elements.soilPh, null);
            setReading(elements.soilEc, null);
            // Blank + re-hide NPK so a previously shown value is not read as live.
            if (elements.soilNpk) {
                const valueElement = elements.soilNpk.querySelector('.reading-value');
                if (valueElement) valueElement.textContent = 'N:-- P:-- K:--';
                elements.soilNpk.classList.add('hidden');
            }
        }
    }

    // Reservoir level marks (v1 /sensors `level` = { low, high }, each
    // { valid, waterPresent }). Drives the reservoir water-level display.
    if (data.level) {
        updateWaterLevelDisplay(data.level);
    }

    // Update connection status
    updateConnectionStatus(true);
}

/**
 * Update the reservoir water-level display from the two /sensors level marks.
 * v1 exposes two discrete sensors (low + high), each with a `valid` flag; an
 * invalid mark is a settling/unknown state and MUST NOT render as wet or dry.
 * @param {Object} level - { low: {valid, waterPresent}, high: {valid, waterPresent} }
 */
function updateWaterLevelDisplay(level) {
    if (!elements.waterLevelText || !elements.waterLevelIndicator) return;
    const low = level.low || {};
    const high = level.high || {};
    const isDarkMode = document.documentElement.classList.contains('dark');
    const blueColor = isDarkMode ? 'bg-blue-400' : 'bg-blue-600';
    const redColor = isDarkMode ? 'bg-red-400' : 'bg-red-600';
    const grayColor = isDarkMode ? 'bg-gray-500' : 'bg-gray-400';

    let levelText, levelPercentage, barColorClass;
    if (high.valid && high.waterPresent) {
        levelText = 'Full'; levelPercentage = 100; barColorClass = blueColor;
    } else if (low.valid && low.waterPresent) {
        levelText = 'OK'; levelPercentage = 60; barColorClass = blueColor;
    } else if (low.valid && !low.waterPresent) {
        levelText = 'Low'; levelPercentage = 10; barColorClass = redColor;
    } else {
        // One or both marks not yet valid (settling) — never show wet/dry.
        levelText = 'Settling…'; levelPercentage = 0; barColorClass = grayColor;
    }

    elements.waterLevelIndicator.style.width = `${levelPercentage}%`;
    const colorClassesToRemove = ['bg-blue-600', 'bg-yellow-500', 'bg-red-600', 'bg-gray-400', 'bg-blue-400', 'bg-amber-400', 'bg-red-400', 'bg-gray-500', 'bg-blue-500'];
    elements.waterLevelIndicator.classList.remove(...colorClassesToRemove);
    elements.waterLevelIndicator.classList.add(barColorClass);
    elements.waterLevelText.textContent = levelText;
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
        // (v1: `wifi` replaces the legacy `network` block)
        const wifiConnected = data.wifi && data.wifi.connected;
        console.log('DEBUG: WiFi data:', data.wifi);
        console.log('DEBUG: WiFi connected:', wifiConnected);
        updateConnectionStatus(wifiConnected);

        // Pump running state moved out of /status into /pumps (v1). Fold the
        // pump fetch into the status refresh cycle so it stays live.
        fetchPumpStatus();
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
    // Automatic-mode indicator. v1 status carries `mode` ("automatic"/"manual")
    // derived server-side from wateringEnabled — the legacy flat
    // `wateringEnabled` field is gone from /status.
    if (data.mode !== undefined) {
        const automatic = data.mode === 'automatic';
        appState.autoWateringEnabled = automatic;
        if (elements.autoWateringToggle) {
            elements.autoWateringToggle.checked = automatic;
        }
        if (elements.autoWateringStatus) {
            elements.autoWateringStatus.textContent = automatic ? 'Enabled' : 'Disabled';
        }
    }

    // Update system info (v1: `wifi` replaces the legacy `network` block).
    if (data.wifi) {
        elements.systemIp.textContent = data.wifi.ip || '--';
    }

    // Storage (v1: byte counts + percentUsed; the legacy body reported KB).
    if (data.storage && typeof data.storage.totalBytes === 'number' && data.storage.totalBytes > 0) {
        const usedKB = Math.round((data.storage.usedBytes || 0) / 1024);
        const totalKB = Math.round(data.storage.totalBytes / 1024);
        const usedPercent = (typeof data.storage.percentUsed === 'number')
            ? data.storage.percentUsed.toFixed(1)
            : (((data.storage.usedBytes || 0) / data.storage.totalBytes) * 100).toFixed(1);
        elements.storageUsage.textContent = `${usedPercent}% (${usedKB} KB / ${totalKB} KB)`;
    }

    // NOTE: pump running state now comes from GET /api/v1/pumps (see
    // fetchPumpStatus) and config values from GET /api/v1/config (see
    // fetchConfig); /status no longer carries pumpRunning/remainingTime/
    // wateringEnabled/config/reservoir, so they are read there instead.
}

/**
 * Fetch pump running state from the API.
 * v1 moved pump running state out of /status into GET /api/v1/pumps.
 */
async function fetchPumpStatus() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/pumps`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        const data = await response.json();
        updatePumpStatus(data);
    } catch (error) {
        console.error('Error fetching pump status:', error);
    }
}

/**
 * Update pump running indicators from the /pumps list.
 * @param {Object} data - Pump list response { success, pumps: [...] }
 */
function updatePumpStatus(data) {
    const pumps = (data && Array.isArray(data.pumps)) ? data.pumps : [];

    // The plant pump drives the main watering status indicator. v1 /pumps
    // exposes `running` + `currentRunTimeMs` but no remaining/target duration,
    // so we drive the running indicator only (no synthetic countdown).
    const plant = pumps.find(p => p && p.name === 'plant');
    if (plant) {
        appState.isWatering = plant.running;
        updateWateringStatus(plant.running);
    }

    // Reservoir pump is absent on single-pump rev2 nodes — hide the entire
    // reservoir section when /pumps has no `reservoir` entry, otherwise drive
    // its running indicator + manual controls from the pump state.
    const reservoir = pumps.find(p => p && p.name === 'reservoir');
    if (elements.reservoirSection) {
        elements.reservoirSection.classList.toggle('hidden', !reservoir);
    }
    if (reservoir) {
        appState.reservoir.pumpRunning = reservoir.running;
        updateReservoirPumpIndicator(reservoir.running);
    }
}

/**
 * Update the reservoir pump running indicator + manual control button states
 * from the /pumps `reservoir` entry. v1 has no enable/auto-level concept, so
 * the manual controls are always available when the pump exists.
 * @param {boolean} running - Whether the reservoir pump is currently running
 */
function updateReservoirPumpIndicator(running) {
    if (elements.reservoirDurationInput) elements.reservoirDurationInput.disabled = false;
    if (elements.startReservoirPumpBtn) elements.startReservoirPumpBtn.disabled = running;
    if (elements.stopReservoirPumpBtn) elements.stopReservoirPumpBtn.disabled = !running;

    if (!elements.reservoirPumpStatus) return;
    const dot = elements.reservoirPumpStatus.querySelector('.status-dot');
    const text = elements.reservoirPumpStatus.querySelector('.status-text');
    if (!dot || !text) return;
    dot.classList.remove('status-inactive', 'status-active', 'status-running', 'status-warning');
    if (running) {
        dot.classList.add('status-running');
        text.textContent = 'Pump Running';
    } else {
        dot.classList.add('status-active');
        text.textContent = 'Pump Ready';
    }
}

/**
 * Fetch current configuration from the API.
 * v1 moved config out of /status into a dedicated GET /api/v1/config.
 */
async function fetchConfig() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/config`);
        if (!response.ok) {
            throw new Error(`HTTP error ${response.status}`);
        }
        const data = await response.json();
        if (data && data.success) {
            // Map v1 field names/units into the existing form model. The
            // duration/interval values are SECONDS here (were possibly labeled
            // in other units in the old UI); the save-side conversions/labels
            // are a separate later mission.
            appState.settings = {
                moistureThresholdLow: data.moistureThresholdLow,
                moistureThresholdHigh: data.moistureThresholdHigh,
                wateringDuration: data.wateringDurationS,
                minWateringInterval: data.minWateringIntervalS
            };
            updateSettingsForm(appState.settings);
        }
    } catch (error) {
        console.error('Error fetching configuration:', error);
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

// NOTE: the legacy `updateReservoirStatus()` (enable/auto-level/level-guess
// display) was removed in the v1 adaptation — the reservoir is now a plain
// pump (running indicator via updateReservoirPumpIndicator from /pumps) plus
// the two level marks (updateWaterLevelDisplay from /sensors). There is no
// enable/disable or auto-level-control concept in the v1 contract.

/**
 * Read a readable message from a v1 error response. v1 errors are
 * `{ success:false, error:"..." }` with a 4xx/5xx status. Falls back to the
 * HTTP status when the body is missing/unparseable.
 * @param {Response} response - the failed fetch Response
 * @returns {Promise<string>} a human-readable error message
 */
async function readApiError(response) {
    try {
        const data = await response.json();
        if (data && typeof data.error === 'string' && data.error) {
            return data.error;
        }
    } catch (e) {
        // fall through to the status-based message
    }
    return `HTTP error ${response.status}`;
}

/**
 * Enable or disable automatic-mode watering via the v1 config endpoint.
 * v1 has no `/control/auto`; the mode is derived server-side from
 * `wateringEnabled`, so we POST that config field as JSON.
 * @param {boolean} enabled - Whether automatic watering should be enabled
 */
async function setAutoWatering(enabled) {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ wateringEnabled: enabled })
        });

        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        // v1 returns the resulting ConfigResponse (no `message` field); a 2xx
        // is success. Re-fetch status so the derived mode indicator updates.
        appState.autoWateringEnabled = enabled;
        elements.autoWateringStatus.textContent = enabled ? 'Enabled' : 'Disabled';
        showNotification('Auto-Watering', `Auto-watering has been ${enabled ? 'enabled' : 'disabled'}.`, 'info');
        fetchSystemStatus();
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

    // v1 config body uses the contract field names/units and JSON. The form
    // fields already carry seconds (GET /config populates them from
    // wateringDurationS / minWateringIntervalS), so send seconds directly.
    const body = {
        moistureThresholdLow: settings.moistureThresholdLow,
        moistureThresholdHigh: settings.moistureThresholdHigh,
        wateringDurationS: settings.wateringDuration,
        minWateringIntervalS: settings.minWateringInterval
    };

    console.log('Sending settings to server:', body);
    console.log('Using endpoint:', `${API_CONFIG.ENDPOINT}/config`);

    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(body)
        });

        // v1 rejects an out-of-range field with a 4xx {success:false,error}.
        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        // Success returns the new ConfigResponse (no `message` field); a 2xx
        // is success on its own.
        appState.settings = settings;
        showNotification('Settings Saved', 'System settings have been updated.', 'success');
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

    // v1 takes a single `metric` name (e.g. env_temperature, soil_moisture)
    // built from the sensor group + reading, plus a named `range`. The two
    // dropdown values already map directly: `${sensor}_${reading}`.
    const metric = `${sensor}_${reading}`;

    showChartLoading();
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/history?metric=${encodeURIComponent(metric)}&range=${encodeURIComponent(timeRange)}`);
        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        // Response shape { success, timestamps, values, metric, ... }; empty
        // arrays (no data / soil not-yet-valid) render as an empty chart, not
        // an error (handled in updateChart).
        const data = await response.json();
        updateChart(data, `${sensor === 'env' ? 'Environmental' : 'Soil'} ${elements.chartReading.options[elements.chartReading.selectedIndex].text}`);
    } catch (error) {
        console.error('Error fetching historical data:', error);
        showNotification('Chart Error', `Failed to load historical data: ${error.message}`, 'error');
    } finally {
        hideChartLoading();
    }
}

/**
 * Update chart with new data
 * @param {Object} data - Historical data from API
 * @param {string} label - Chart label
 */
function updateChart(data, label) {
    if (!appState.chart) {
        console.error('Chart not initialized');
        return;
    }

    if (!data) {
        console.error('No data received for chart');
        showNotification('Chart Error', 'No data received from server.', 'error');
        return;
    }

    if (!data.timestamps || !data.values) {
        console.error('Invalid data format:', data);
        showNotification('Chart Error', 'Invalid data format received.', 'error');
        return;
    }

    if (data.timestamps.length === 0) {
        console.log('No historical data available for selected time range');
        // Clear chart and show message
        appState.chart.data.labels = [];
        appState.chart.data.datasets[0].data = [];
        appState.chart.data.datasets[0].label = `${label} (No data)`;
        appState.chart.update();
        showNotification('No Data', 'No historical data available for the selected time range.', 'info');
        return;
    }

    console.log(`Updating chart with ${data.timestamps.length} data points`);
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
    // /api/v1/history sends epoch SECONDS (see docs/api/openapi.yaml); the Date
    // constructor expects milliseconds, so convert here or points land near 1970.
    appState.chart.data.labels = data.timestamps.map(t => new Date(t * 1000));
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
    // v1 clamps durationS to 1..300 server-side; clamp client-side too.
    const durationVal = Math.min(300, Math.max(1, parseInt(duration) || 20));

    try {
        console.log(`Starting plant pump for ${durationVal} seconds (POST /pumps/plant)`);

        const response = await fetch(`${API_CONFIG.ENDPOINT}/pumps/plant`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ action: 'run', durationS: durationVal })
        });

        // 409 = pump already running; surface the contract message.
        if (response.status === 409) {
            const msg = await readApiError(response);
            showNotification('Watering', msg || 'Pump already running', 'warning');
            fetchPumpStatus();
            return;
        }
        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        // v1 returns the resulting PumpResponse (no `message` field).
        showNotification('Watering Started', `Watering started for ${durationVal} seconds.`, 'success');
        // Update UI immediately for better responsiveness, then re-fetch the
        // pump list to refresh the running indicator.
        updateWateringStatus(true);
        fetchPumpStatus();
    } catch (error) {
        console.error('Error starting watering:', error);
        showNotification('Watering Error', `Failed to start watering: ${error.message}`, 'error');
    }
}

/**
 * Stop watering via API (idempotent no-op if already stopped)
 */
async function stopWatering() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/pumps/plant`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ action: 'stop' })
        });

        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        showNotification('Watering Stopped', 'Watering has been stopped.', 'info');
        updateWateringStatus(false);
        fetchPumpStatus();
    } catch (error) {
        console.error('Error stopping watering:', error);
        showNotification('Watering Error', `Failed to stop watering: ${error.message}`, 'error');
    }
}

// v1 has NO reservoir enable/disable and NO auto-level-control endpoint — the
// reservoir is a plain pump plus the level sensors. The legacy
// `setReservoirPumpEnabled()` and `setReservoirAutoLevelControl()` functions
// (POST /reservoir command verbs) were removed accordingly; only manual
// start/stop remain, retargeted to POST /api/v1/pumps/reservoir (same shape as
// the plant pump).

/**
 * Start the reservoir fill pump for a timed run via POST /pumps/reservoir.
 * @param {number} duration - Fill duration in seconds (clamped to 1..300)
 */
async function startReservoirFilling(duration) {
    // v1 durationS is a bounded timed run (1..300 s); there is no "0 = auto".
    const durationVal = Math.min(300, Math.max(1, parseInt(duration) || 60));

    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/pumps/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ action: 'run', durationS: durationVal })
        });

        // 409 = pump already running.
        if (response.status === 409) {
            const msg = await readApiError(response);
            showNotification('Reservoir', msg || 'Pump already running', 'warning');
            fetchPumpStatus();
            return;
        }
        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        showNotification('Reservoir Filling Started', `Reservoir filling started for ${durationVal} seconds.`, 'success');
        // Re-fetch the pump list to refresh the running indicator.
        fetchPumpStatus();
    } catch (error) {
        console.error('Error starting reservoir filling:', error);
        showNotification('Reservoir Error', `Failed to start reservoir filling: ${error.message}`, 'error');
    }
}

/**
 * Stop the reservoir pump via POST /pumps/reservoir (idempotent).
 */
async function stopReservoirPump() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/pumps/reservoir`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ action: 'stop' })
        });

        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        showNotification('Reservoir Pump Stopped', 'Reservoir pump has been stopped.', 'info');
        fetchPumpStatus();
    } catch (error) {
        console.error('Error stopping reservoir pump:', error);
        showNotification('Reservoir Error', `Failed to stop reservoir pump: ${error.message}`, 'error');
    }
}

/**
 * Trigger a firmware OTA update. v1 answers a fixed 501 stub until PR-13; show
 * a clean "not available yet" message on 501 rather than an error/crash.
 */
async function triggerOtaUpdate() {
    try {
        const response = await fetch(`${API_CONFIG.ENDPOINT}/ota`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({})
        });

        if (response.status === 501) {
            showNotification('Firmware Update', 'OTA not available yet (PR-13).', 'info');
            return;
        }
        if (!response.ok) {
            throw new Error(await readApiError(response));
        }

        showNotification('Firmware Update', 'OTA update started.', 'success');
    } catch (error) {
        console.error('Error triggering OTA update:', error);
        showNotification('Firmware Update Error', `OTA request failed: ${error.message}`, 'error');
    }
}

// Initialize the application when the DOM is fully loaded
document.addEventListener('DOMContentLoaded', function() {
    console.log('DOM loaded, initializing app...');
    initApp();
});