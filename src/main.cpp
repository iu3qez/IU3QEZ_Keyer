#include <Arduino.h>
#include "settings.h"
#include "gpio_config.h"
#include "neopixel_debug.h"
#include "sidetone_generator.h"
#include "power_amp.h"
#include "keyer_logic.h"
#include "i2c_scanner.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "config_manager.h"

GPIO_Config gpio;
NeoPixel_Debug neopixel;
SidetoneGenerator sidetone;
PowerAmplifier powerAmp;
KeyerLogic keyer;
ConfigManager configMgr;
WiFiManager wifiManager;
WebServerManager webServer(&keyer, &sidetone, &configMgr);

// Callback keyer: chiamato quando cambia stato keying
void keyerCallback(bool keying) {
  // Imposta uscita KEY
  gpio.setKeyOutput(keying);

  // Controlla sidetone
  if (keying) {
    sidetone.start();
  } else {
    sidetone.stop();
  }

  // Aggiorna NeoPixel (stato visivo semplificato)
  if (keying) {
    neopixel.setState(STATE_KEYING_DOT);  // TODO: distinguere DOT/DASH
  } else {
    neopixel.setState(STATE_IDLE);
  }
}

void setup() {
  // Inizializzazione
  Serial.begin(115200);
  delay(10000);
  Serial.println("\n=== IU3QEZ Keyer CW QRS2HST - ESP32-S3 ===\n");
  Serial.flush();

  Serial.println("STEP 1/5: Inizializzazione GPIO...");
  Serial.flush();
  gpio.begin();
  Serial.println("GPIO OK");
  Serial.flush();

  Serial.println("STEP 2/5: Inizializzazione NeoPixel...");
  Serial.flush();
  neopixel.begin();
  Serial.println("NeoPixel OK");
  Serial.flush();

  Serial.println("STEP 3/5: Test ES8311 I2C");
  Serial.println("ATTENZIONE: Power Amplifier disabilitato");
  Serial.println("ATTENZIONE: I2S disabilitato");
  Serial.flush();

  // STEP 4: Inizializza I2C Wire per ES8311 e TCA9555
  Serial.println("STEP 4/5: I2C init...");
  Serial.printf("Inizializzando Wire su GPIO%d/%d (SDA/SCL)...\n", I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);
  Serial.println("Wire I2C inizializzato");
  Serial.flush();

  // Inizializza ES8311 PRIMA (inizializza I2C)
  Serial.println("\nInizializzazione ES8311...");
  if (!sidetone.begin()) {
    Serial.println("ERRORE: Inizializzazione ES8311 fallita!");
  } else {
    Serial.println("ES8311 inizializzato con successo!");
  }

  // Inizializza ConfigManager e carica configurazione da NVS
  Serial.println("\nInizializzazione ConfigManager...");
  if (!configMgr.begin()) {
    Serial.println("ERRORE: ConfigManager init fallito!");
  } else {
    configMgr.load();  // Carica da NVS o usa defaults
    Serial.println("ConfigManager inizializzato");
  }

  // Applica configurazione iniziale a sidetone
  configMgr.applyToSidetone(&sidetone);

  // Inizializza Power Amplifier (usa I2C già inizializzato)
  Serial.println("\nInizializzazione Power Amplifier...");
  if (!powerAmp.begin()) {
    Serial.println("ERRORE: Power Amplifier init fallito!");
  } else {
    powerAmp.enable();
    Serial.println("Power Amplifier ABILITATO");
  }

  // Avvia task audio su Core 1
  Serial.println("\nAvvio audio task su Core 1...");
  if (!sidetone.startAudioTask()) {
    Serial.println("ERRORE: Avvio audio task fallito!");
  } else {
    Serial.println("Audio task su Core 1 avviato con successo");
  }

  // Inizializza Keyer Logic con timer hardware
  Serial.println("\nInizializzazione Keyer Logic...");
  if (!keyer.begin(keyerCallback)) {
    Serial.println("ERRORE: Inizializzazione keyer fallita!");
  } else {
    // Applica configurazione salvata
    configMgr.applyToKeyer(&keyer);
    Serial.println("Keyer Logic inizializzato con successo");
    keyer.printStatus();
  }

  // Inizializza WiFi Access Point
  Serial.println("\nInizializzazione WiFi AP...");
  if (!wifiManager.begin()) {
    Serial.println("ERRORE: WiFi AP init fallito!");
  } else {
    Serial.println("WiFi AP avviato con successo");
  }

  // Inizializza Web Server
  Serial.println("\nInizializzazione Web Server...");
  if (!webServer.begin()) {
    Serial.println("ERRORE: Web Server init fallito!");
  } else {
    Serial.println("Web Server avviato con successo");
    Serial.printf("Accedi a: http://%s\n", wifiManager.getIP().toString().c_str());
  }

  Serial.println("\nSTEP 5/5: Setup completato!");
  Serial.println("Architettura:");
  Serial.println("  Core 0: Timer ISR (keyer logic) + GPIO ISR (paddle)");
  Serial.println("  Core 1: I2S audio generation task + WiFi/WebServer");
  Serial.println("  Curtis Mode B con finestra memoria attiva");
  Serial.println("Premere paddle per test CW...\n");
  Serial.printf("Web Interface: http://%s\n\n", wifiManager.getIP().toString().c_str());
  Serial.flush();
}

void loop() {
  static uint32_t lastPrint = 0;
  static uint32_t lastNeoPixel = 0;
  static uint32_t lastStatusPrint = 0;
  static uint32_t lastPaddleDebug = 0;

  // Alimenta watchdog
  yield();

  // DEBUG: Stampa stato paddle ogni 2 secondi
  if (millis() - lastPaddleDebug > 2000) {
    bool dot_raw = (digitalRead(DOT_PIN) == LOW);
    bool dash_raw = (digitalRead(DASH_PIN) == LOW);
    Serial.printf("[DEBUG] RAW: DOT=%d DASH=%d | ISR: _dot=%d _dash=%d | ISR_count: DOT=%lu DASH=%lu | State=%d Key=%d\n",
                  dot_raw, dash_raw,
                  keyer.getDotPressed(), keyer.getDashPressed(),
                  keyer.getDotISRCount(), keyer.getDashISRCount(),
                  keyer.getState(), keyer.isKeying());
    Serial.flush();
    lastPaddleDebug = millis();
  }

  // Stampa heartbeat ogni 10 secondi
  if (millis() - lastPrint > 10000) {
    Serial.printf("Keyer running: %d WPM\n", keyer.getWPM());
    Serial.flush();
    lastPrint = millis();
  }

  // Stampa status dettagliato ogni 30 secondi (solo se attivo)
  if (millis() - lastStatusPrint > 30000) {
    if (keyer.isKeying()) {
      keyer.printStatus();
    }
    lastStatusPrint = millis();
  }

  // Aggiorna NeoPixel solo ogni 20ms per ridurre carico
  if (millis() - lastNeoPixel > 20) {
    neopixel.update();
    lastNeoPixel = millis();
  }

  // Keyer logic gira su Timer ISR + GPIO ISR
  // Audio task gira su Core 1
  // Questo loop è solo per housekeeping

  delay(10);  // Rilascia CPU
}
