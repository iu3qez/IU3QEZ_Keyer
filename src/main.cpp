#include <Arduino.h>
#include "gpio_config.h"
#include "neopixel_debug.h"
#include "sidetone_generator.h"
#include "power_amp.h"
#include "i2c_scanner.h"

GPIO_Config gpio;
NeoPixel_Debug neopixel;
SidetoneGenerator sidetone;
PowerAmplifier powerAmp;

// Task FreeRTOS per generazione audio su Core 1
void audioTask(void* parameter) {
  while (true) {
    sidetone.task();
    // Yielding automatico in task() durante i2s_write blocking
  }
}

void setup() {
  // Inizializzazione
  Serial.begin(115200);
  delay(10000);
  Serial.println("\n=== IU3QEZ Keyer CW HST - ESP32-S3 ===\n");
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
  Serial.println("Inizializzando Wire su GPIO11/10 (SDA/SCL)...");
  Wire.begin(11, 10);  // SDA=GPIO11, SCL=GPIO10
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

  sidetone.setFrequency(600);
  sidetone.setVolume(80);

  // Inizializza Power Amplifier (usa I2C già inizializzato)
  Serial.println("\nInizializzazione Power Amplifier...");
  if (!powerAmp.begin()) {
    Serial.println("ERRORE: Power Amplifier init fallito!");
  } else {
    powerAmp.enable();
    Serial.println("Power Amplifier ABILITATO");
  }

  // TASK AUDIO DISABILITATO - race condition tra core
  // Scriviamo I2S direttamente dal loop di Core 0
  Serial.println("\nATTENZIONE: I2S gestito da Core 0 (evita race condition)");

  Serial.println("\nSTEP 5/5: Setup completato!");
  Serial.println("Premere paddle per test audio...\n");
  Serial.flush();
}

void loop() {
  static uint32_t loopCount = 0;
  static uint32_t lastPrint = 0;
  static uint32_t lastNeoPixel = 0;
  static bool lastTestInput = false;

  loopCount++;

  // Alimenta watchdog per evitare crash
  yield();

  // TEST TCA9555: Leggi Extend_IO10 (P1.1)
  bool testInput = powerAmp.readTestInput();
  if (testInput != lastTestInput) {
    Serial.printf("TCA9555 Extend_IO10: %s\n", testInput ? "HIGH" : "LOW");
    Serial.flush();
    lastTestInput = testInput;
  }

  // Stampa heartbeat ogni 10 secondi (ridotto da 5 per sicurezza)
  if (millis() - lastPrint > 10000) {
    Serial.printf("Loop alive: %lu iter\n", loopCount);
    Serial.flush();
    lastPrint = millis();
    loopCount = 0;  // Reset counter per evitare overflow
  }

  // Test lettura paddle (raw, senza debouncing)
  static bool lastDot = false;
  static bool lastDash = false;
  static bool lastKeying = false;

  bool dot = gpio.readDotPaddle();
  bool dash = gpio.readDashPaddle();
  bool keying = dot || dash;

  // Determina stato per NeoPixel
  KeyerState state = STATE_IDLE;
  if (dot && dash) {
    state = STATE_BOTH_PRESSED;
  } else if (dot) {
    state = STATE_DOT_PRESSED;
  } else if (dash) {
    state = STATE_DASH_PRESSED;
  }
  neopixel.setState(state);

  // Debug paddle state changes (con flush per evitare buffer overflow)
  if (dot != lastDot) {
    Serial.printf("DOT: %s\n", dot ? "PRESS" : "REL");
    Serial.flush();
    lastDot = dot;
  }

  if (dash != lastDash) {
    Serial.printf("DASH: %s\n", dash ? "PRESS" : "REL");
    Serial.flush();
    lastDash = dash;
  }

  // Sincronizza key output e sidetone
  if (keying != lastKeying) {
    gpio.setKeyOutput(keying);

    if (keying) {
      sidetone.start();  // Attiva sidetone
    } else {
      sidetone.stop();   // Disattiva sidetone
    }

    lastKeying = keying;
  }

  // Alimenta I2S (scrive samples anche quando sidetone è off = silenzio)
  sidetone.task();

  // Aggiorna NeoPixel solo ogni 20ms per ridurre carico
  if (millis() - lastNeoPixel > 20) {
    neopixel.update();
    lastNeoPixel = millis();
  }

  // NIENTE delay() - i2s_write già introduce delay sufficiente
  // delay(20);
}
