// IU3QEZ CW HST Keyer - Web Interface JavaScript

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
    syncRangeInput('fade-in', 'fade-in-value');
    syncRangeInput('fade-out', 'fade-out-value');
    syncRangeInput('char_space_tolerance', 'char_space_tolerance-value');
    syncRangeInput('word_space_tolerance', 'word_space_tolerance-value');
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

        if (config.fade_in_ms !== undefined) {
            document.getElementById('fade-in').value = config.fade_in_ms;
            document.getElementById('fade-in-value').value = config.fade_in_ms;
        }

        if (config.fade_out_ms !== undefined) {
            document.getElementById('fade-out').value = config.fade_out_ms;
            document.getElementById('fade-out-value').value = config.fade_out_ms;
        }

        if (config.char_space_tolerance_dots !== undefined) {
            const value = Number(config.char_space_tolerance_dots).toFixed(1);
            document.getElementById('char_space_tolerance').value = value;
            document.getElementById('char_space_tolerance-value').value = value;
        }

        if (config.word_space_tolerance_dots !== undefined) {
            const value = Number(config.word_space_tolerance_dots).toFixed(1);
            document.getElementById('word_space_tolerance').value = value;
            document.getElementById('word_space_tolerance-value').value = value;
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
    const paramIds = ['wpm', 'mode', 'window-up', 'window-down', 'debounce', 'volume', 'frequency',
                      'fade-in', 'fade-out', 'char_space_tolerance', 'word_space_tolerance'];

    paramIds.forEach(id => {
        const element = document.getElementById(id);
        if (element) {
            element.addEventListener('change', applyConfigImmediate);
        }
    });
}

// Apply configuration immediately (without saving to flash)
async function applyConfigImmediate(suppressMessages = false) {
    const getInt = (id) => {
        const value = parseInt(document.getElementById(id).value, 10);
        return Number.isNaN(value) ? 0 : value;
    };

    const roundDotValue = (id) => {
        const raw = parseFloat(document.getElementById(id).value);
        if (Number.isNaN(raw)) {
            return 0;
        }
        return Math.round(raw * 10) / 10;
    };

    const formData = {
        wpm: getInt('wpm'),
        mode: getInt('mode'),
        window_up: getInt('window-up'),
        window_down: getInt('window-down'),
        debounce: getInt('debounce'),
        volume: getInt('volume'),
        frequency: getInt('frequency'),
        fade_in_ms: getInt('fade-in'),
        fade_out_ms: getInt('fade-out'),
        char_space_tolerance_dots: roundDotValue('char_space_tolerance'),
        word_space_tolerance_dots: roundDotValue('word_space_tolerance')
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
            return true;
        } else {
            if (!suppressMessages) {
                showMessage('Apply failed: ' + result.message, 'error');
            }
            return false;
        }

    } catch (error) {
        if (!suppressMessages) {
            showMessage('Error applying configuration: ' + error.message, 'error');
        }
        return false;
    }
}

// Save configuration to flash
async function handleSaveConfig() {
    const applied = await applyConfigImmediate(true);
    if (!applied) {
        console.warn('Immediate apply failed before saving configuration');
    }
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
