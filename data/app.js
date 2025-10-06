// IU3QEZ CW QRS2HST Keyer - Web Interface JavaScript

const API_BASE = '';
const STATUS_REFRESH_INTERVAL = 500; // ms

let statusTimer = null;

// Mode names for display
const MODE_NAMES = {
    0: 'Straight Key',
    1: 'Iambic A',
    2: 'Iambic B',
    3: 'Ultimatic'
};

// State names for display
const STATE_NAMES = {
    0: 'IDLE',
    1: 'DOT ACTIVE',
    2: 'DASH ACTIVE',
    3: 'INTER-ELEMENT',
    4: 'INTER-CHAR'
};

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    initializeRangeInputs();
    loadConfiguration();
    startStatusPolling();

    // Add listeners for immediate apply
    addImmediateApplyListeners();

    // Save button
    document.getElementById('btn-save').addEventListener('click', handleSaveConfig);

    // Reset button
    document.getElementById('btn-reset').addEventListener('click', handleResetConfig);

    // Refresh button
    document.getElementById('btn-refresh').addEventListener('click', function() {
        loadConfiguration();
        updateStatus();
    });
});

// Sync range inputs with number inputs
function initializeRangeInputs() {
    syncRangeInput('wpm', 'wpm-value');
    syncRangeInput('window-up', 'window-up-value');
    syncRangeInput('window-down', 'window-down-value');
    syncRangeInput('debounce', 'debounce-value');
    syncRangeInput('volume', 'volume-value');
    syncRangeInput('frequency', 'frequency-value');
}

function syncRangeInput(rangeId, numberId) {
    const range = document.getElementById(rangeId);
    const number = document.getElementById(numberId);

    range.addEventListener('input', function() {
        number.value = range.value;
    });

    number.addEventListener('input', function() {
        range.value = number.value;
    });
}

// Load current configuration from ESP32
async function loadConfiguration() {
    try {
        const response = await fetch(`${API_BASE}/api/config`);
        const config = await response.json();

        // Update form fields
        if (config.wpm !== undefined) {
            document.getElementById('wpm').value = config.wpm;
            document.getElementById('wpm-value').value = config.wpm;
        }

        if (config.mode !== undefined) {
            document.getElementById('mode').value = config.mode;
        }

        if (config.window_up !== undefined) {
            document.getElementById('window-up').value = config.window_up;
            document.getElementById('window-up-value').value = config.window_up;
        }

        if (config.window_down !== undefined) {
            document.getElementById('window-down').value = config.window_down;
            document.getElementById('window-down-value').value = config.window_down;
        }

        if (config.debounce !== undefined) {
            document.getElementById('debounce').value = config.debounce;
            document.getElementById('debounce-value').value = config.debounce;
        }

        if (config.volume !== undefined) {
            document.getElementById('volume').value = config.volume;
            document.getElementById('volume-value').value = config.volume;
        }

        if (config.frequency !== undefined) {
            document.getElementById('frequency').value = config.frequency;
            document.getElementById('frequency-value').value = config.frequency;
        }

    } catch (error) {
        showMessage('Error loading configuration: ' + error.message, 'error');
    }
}

// Update status display
async function updateStatus() {
    try {
        const response = await fetch(`${API_BASE}/api/status`);
        const status = await response.json();

        // Update status values
        document.getElementById('status-wpm').textContent = status.wpm || '--';
        document.getElementById('status-mode').textContent = MODE_NAMES[status.mode] || '--';
        document.getElementById('status-state').textContent = STATE_NAMES[status.state] || '--';

        // Update keying LED
        const keyingLed = document.getElementById('keying-led');
        const keyingText = document.getElementById('keying-text');
        if (status.keying) {
            keyingLed.classList.add('active');
            keyingText.textContent = 'ON';
        } else {
            keyingLed.classList.remove('active');
            keyingText.textContent = 'OFF';
        }

        // Update DOT LED
        const dotLed = document.getElementById('dot-led');
        const dotText = document.getElementById('dot-text');
        if (status.dot_pressed) {
            dotLed.classList.add('active');
            dotText.textContent = 'DOWN';
        } else {
            dotLed.classList.remove('active');
            dotText.textContent = 'UP';
        }

        // Update DASH LED
        const dashLed = document.getElementById('dash-led');
        const dashText = document.getElementById('dash-text');
        if (status.dash_pressed) {
            dashLed.classList.add('active');
            dashText.textContent = 'DOWN';
        } else {
            dashLed.classList.remove('active');
            dashText.textContent = 'UP';
        }

    } catch (error) {
        console.error('Error updating status:', error);
    }
}

// Start polling status
function startStatusPolling() {
    if (statusTimer) {
        clearInterval(statusTimer);
    }

    updateStatus(); // Initial update
    statusTimer = setInterval(updateStatus, STATUS_REFRESH_INTERVAL);
}

// Add listeners for immediate apply on change
function addImmediateApplyListeners() {
    const paramIds = ['wpm', 'mode', 'window-up', 'window-down', 'debounce', 'volume', 'frequency'];

    paramIds.forEach(id => {
        const element = document.getElementById(id);
        if (element) {
            element.addEventListener('change', applyConfigImmediate);
        }
    });
}

// Apply configuration immediately (without saving to flash)
async function applyConfigImmediate() {
    const formData = {
        wpm: parseInt(document.getElementById('wpm').value),
        mode: parseInt(document.getElementById('mode').value),
        window_up: parseInt(document.getElementById('window-up').value),
        window_down: parseInt(document.getElementById('window-down').value),
        debounce: parseInt(document.getElementById('debounce').value),
        volume: parseInt(document.getElementById('volume').value),
        frequency: parseInt(document.getElementById('frequency').value)
    };

    try {
        const response = await fetch(`${API_BASE}/api/config`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(formData)
        });

        const result = await response.json();

        if (result.success) {
            // Success - no message, immediate feedback
            updateStatus(); // Refresh status
        } else {
            showMessage('Apply failed: ' + result.message, 'error');
        }

    } catch (error) {
        console.error('Error applying configuration:', error);
    }
}

// Save configuration to flash
async function handleSaveConfig() {
    try {
        const response = await fetch(`${API_BASE}/api/config/save`, {
            method: 'POST'
        });

        const result = await response.json();

        if (result.success) {
            showMessage('Configuration saved to flash!', 'success');
        } else {
            showMessage('Save failed: ' + result.message, 'error');
        }

    } catch (error) {
        showMessage('Error saving configuration: ' + error.message, 'error');
    }
}

// Reset configuration to defaults
async function handleResetConfig() {
    if (!confirm('Reset all settings to factory defaults?')) {
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/api/config/reset`, {
            method: 'POST'
        });

        const result = await response.json();

        if (result.success) {
            showMessage('Configuration reset to defaults!', 'success');
            loadConfiguration(); // Reload from server
            updateStatus();
        } else {
            showMessage('Reset failed: ' + result.message, 'error');
        }

    } catch (error) {
        showMessage('Error resetting configuration: ' + error.message, 'error');
    }
}

// Show message to user
function showMessage(text, type) {
    const messageDiv = document.getElementById('message');
    messageDiv.textContent = text;
    messageDiv.className = 'message ' + type;
    messageDiv.style.display = 'block';

    // Auto-hide after 5 seconds
    setTimeout(function() {
        messageDiv.style.display = 'none';
    }, 5000);
}

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    if (statusTimer) {
        clearInterval(statusTimer);
    }
});
