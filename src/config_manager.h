#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// Forward declarations
class KeyerLogic;
class SidetoneGenerator;

// Struttura configurazione
struct KeyerConfig {
    // Keyer parameters
    uint8_t wpm;              // 5-100
    uint8_t mode;             // 0-3
    uint8_t window_up;        // 0-99%
    uint8_t window_down;      // 0-99%
    uint32_t debounce;        // 100-10000 μs

    // Sidetone parameters
    uint8_t volume;           // 0-100%
    uint16_t frequency;       // 300-800 Hz

    // Defaults
    static const uint8_t DEFAULT_WPM = 20;
    static const uint8_t DEFAULT_MODE = 2;  // Iambic B
    static const uint8_t DEFAULT_WINDOW_UP = 45;
    static const uint8_t DEFAULT_WINDOW_DOWN = 5;
    static const uint32_t DEFAULT_DEBOUNCE = 1000;
    static const uint8_t DEFAULT_VOLUME = 70;
    static const uint16_t DEFAULT_FREQUENCY = 600;
};

class ConfigManager {
public:
    ConfigManager();

    // Inizializzazione
    bool begin();

    // Carica configurazione da NVS (o usa defaults)
    bool load();

    // Salva configurazione corrente in NVS
    bool save();

    // Reset ai valori di default
    void resetToDefaults();

    // Apply configurazione agli oggetti
    void applyToKeyer(KeyerLogic* keyer);
    void applyToSidetone(SidetoneGenerator* sidetone);

    // Leggi configurazione da oggetti
    void readFromKeyer(KeyerLogic* keyer);
    void readFromSidetone(SidetoneGenerator* sidetone);

    // Accesso diretto configurazione
    KeyerConfig& getConfig() { return _config; }

    // Check se NVS ha dati salvati
    bool hasSavedConfig();

private:
    Preferences _prefs;
    KeyerConfig _config;

    const char* NVS_NAMESPACE = "keyer";
};

#endif // CONFIG_MANAGER_H
