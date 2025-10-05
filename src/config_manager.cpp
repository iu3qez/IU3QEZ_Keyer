#include "config_manager.h"
#include "keyer_logic.h"
#include "sidetone_generator.h"

ConfigManager::ConfigManager() {
    // Inizializza con defaults
    resetToDefaults();
}

bool ConfigManager::begin() {
    // Apri namespace NVS in modalità read-write
    if (!_prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("ERRORE: Apertura NVS fallita!");
        return false;
    }

    Serial.println("ConfigManager: NVS inizializzato");
    return true;
}

bool ConfigManager::load() {
    if (!hasSavedConfig()) {
        Serial.println("ConfigManager: Nessuna configurazione salvata, uso defaults");
        resetToDefaults();
        return false;
    }

    // Carica valori da NVS
    _config.wpm = _prefs.getUChar("wpm", KeyerConfig::DEFAULT_WPM);
    _config.mode = _prefs.getUChar("mode", KeyerConfig::DEFAULT_MODE);
    _config.window_up = _prefs.getUChar("window_up", KeyerConfig::DEFAULT_WINDOW_UP);
    _config.window_down = _prefs.getUChar("window_down", KeyerConfig::DEFAULT_WINDOW_DOWN);
    _config.debounce = _prefs.getUInt("debounce", KeyerConfig::DEFAULT_DEBOUNCE);
    _config.volume = _prefs.getUChar("volume", KeyerConfig::DEFAULT_VOLUME);
    _config.frequency = _prefs.getUShort("frequency", KeyerConfig::DEFAULT_FREQUENCY);

    Serial.println("ConfigManager: Configurazione caricata da NVS");
    Serial.printf("  WPM: %d, Mode: %d, Windows: %d%%/%d%%\n",
                  _config.wpm, _config.mode, _config.window_up, _config.window_down);
    Serial.printf("  Debounce: %lu us, Volume: %d%%, Freq: %d Hz\n",
                  _config.debounce, _config.volume, _config.frequency);

    return true;
}

bool ConfigManager::save() {
    // Salva tutti i parametri in NVS
    _prefs.putUChar("wpm", _config.wpm);
    _prefs.putUChar("mode", _config.mode);
    _prefs.putUChar("window_up", _config.window_up);
    _prefs.putUChar("window_down", _config.window_down);
    _prefs.putUInt("debounce", _config.debounce);
    _prefs.putUChar("volume", _config.volume);
    _prefs.putUShort("frequency", _config.frequency);

    // Marca che abbiamo salvato almeno una volta
    _prefs.putBool("initialized", true);

    Serial.println("ConfigManager: Configurazione salvata in NVS");
    Serial.printf("  WPM: %d, Mode: %d, Windows: %d%%/%d%%\n",
                  _config.wpm, _config.mode, _config.window_up, _config.window_down);
    Serial.printf("  Debounce: %lu us, Volume: %d%%, Freq: %d Hz\n",
                  _config.debounce, _config.volume, _config.frequency);

    return true;
}

void ConfigManager::resetToDefaults() {
    _config.wpm = KeyerConfig::DEFAULT_WPM;
    _config.mode = KeyerConfig::DEFAULT_MODE;
    _config.window_up = KeyerConfig::DEFAULT_WINDOW_UP;
    _config.window_down = KeyerConfig::DEFAULT_WINDOW_DOWN;
    _config.debounce = KeyerConfig::DEFAULT_DEBOUNCE;
    _config.volume = KeyerConfig::DEFAULT_VOLUME;
    _config.frequency = KeyerConfig::DEFAULT_FREQUENCY;

    Serial.println("ConfigManager: Reset a valori di default");
}

void ConfigManager::applyToKeyer(KeyerLogic* keyer) {
    if (keyer) {
        keyer->setWPM(_config.wpm);
        keyer->setMode(_config.mode);
        keyer->setMemoryWindow(_config.window_up, _config.window_down);
        keyer->setDebounce(_config.debounce);
    }
}

void ConfigManager::applyToSidetone(SidetoneGenerator* sidetone) {
    if (sidetone) {
        sidetone->setVolume(_config.volume);
        sidetone->setFrequency(_config.frequency);
    }
}

void ConfigManager::readFromKeyer(KeyerLogic* keyer) {
    if (keyer) {
        _config.wpm = keyer->getWPM();
        _config.mode = keyer->getMode();
        keyer->getMemoryWindow(&_config.window_up, &_config.window_down);
        _config.debounce = keyer->getDebounce();
    }
}

void ConfigManager::readFromSidetone(SidetoneGenerator* sidetone) {
    if (sidetone) {
        _config.volume = sidetone->getVolume();
        _config.frequency = sidetone->getFrequency();
    }
}

bool ConfigManager::hasSavedConfig() {
    return _prefs.getBool("initialized", false);
}
