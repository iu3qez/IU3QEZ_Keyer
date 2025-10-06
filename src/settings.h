#ifndef SETTINGS_H
#define SETTINGS_H

// ============================================================================
// IU3QEZ CW QRS2HST KEYER - CONFIGURAZIONE CENTRALIZZATA
// ============================================================================

// ----------------------------------------------------------------------------
// Optional project secrets (not tracked) override:
// Define WiFi credentials in include/wifi_credentials.h to keep them out of VCS.
// ----------------------------------------------------------------------------
#if defined(__has_include)
#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif
#endif

// === GPIO PADDLE INPUT ===
#define DOT_PIN         3   // GPIO3 - Paddle DOT (active LOW, pull-up)
#define DASH_PIN        4   // GPIO4 - Paddle DASH (active LOW, pull-up)
#define KEY_PIN         5   // GPIO5 - Key output

// === NEOPIXEL DEBUG ===
#define NEOPIXEL_PIN    38  // GPIO38 - NeoPixel WS2812
#define NUM_LEDS        7   // Numero LED

// === I2C BUS ===
#define I2C_SDA_PIN     11  // GPIO11 - SDA (condiviso ES8311 + TCA9555)
#define I2C_SCL_PIN     10  // GPIO10 - SCL

// === ES8311 AUDIO CODEC ===
#define ES8311_I2C_ADDR 0x18  // Indirizzo I2C ES8311

// === I2S AUDIO OUTPUT ===
#define I2S_MCLK_PIN    12  // GPIO12 - Master Clock
#define I2S_BCLK_PIN    13  // GPIO13 - Bit Clock
#define I2S_LRCK_PIN    14  // GPIO14 - Left/Right Clock
#define I2S_DOUT_PIN    16  // GPIO16 - Data Out to ES8311

#define I2S_SAMPLE_RATE 16000  // Sample rate in Hz
#define I2S_CHANNELS    2      // Stereo

// === TCA9555 GPIO EXPANDER ===
#define TCA9555_ADDR        0x20  // Indirizzo I2C TCA9555
#define I2C_USB_SEL_PIN     6     // P0.6 (EXIO7) - I2C/USB selector (HIGH=I2C)
#define PA_ENABLE_PIN       0     // P1.0 (EXIO8) - Power Amplifier enable

// === SIDETONE AUDIO ===
#define SIDETONE_FREQ_HZ    600   // Frequenza sidetone (400-600 Hz)
#define SIDETONE_VOLUME     70    // Volume iniziale (0-100%)

// Rampe anti-click (in millisecondi)
#define SIDETONE_RAMP_UP_MS   2   // Ramp up per evitare click
#define SIDETONE_RAMP_DOWN_MS 4   // Ramp down per evitare click

// Calcolo samples per rampe (dipende da sample rate)
#define RAMP_UP_SAMPLES   ((I2S_SAMPLE_RATE * SIDETONE_RAMP_UP_MS) / 1000)   // 32 samples @ 16kHz
#define RAMP_DOWN_SAMPLES ((I2S_SAMPLE_RATE * SIDETONE_RAMP_DOWN_MS) / 1000) // 64 samples @ 16kHz

// === WAVETABLE ===
#define WAVETABLE_SIZE  64      // Samples in wavetable
#define TONE_AMPLITUDE  16384   // ~50% del massimo 16-bit signed

// ============================================================================
// KEYER TIMING - CURTIS MODE B CON FINESTRA MEMORY
// ============================================================================

// === VELOCITÀ E TIMING ===
#define KEYER_WPM_DEFAULT   20      // Velocità iniziale in WPM (Words Per Minute)
#define KEYER_WPM_MIN       5       // Minimo WPM
#define KEYER_WPM_MAX       60      // Massimo WPM (per HST)

// Calcolo timing PARIS standard:
// 1 WPM = 1 parola "PARIS" al minuto = 50 unità temporali al minuto
// Durata DOT (1 unità) = 60000 ms / (50 * WPM) = 1200 / WPM ms
// DOT a 20 WPM = 60 ms
// DASH = 3 * DOT
// Inter-element space = 1 * DOT
// Inter-character space = 3 * DOT
// Inter-word space = 7 * DOT

// === CURTIS MODE B - FINESTRA DI MEMORIZZAZIONE (WND) ===
// La finestra determina QUANDO il keyer accetta input del paddle opposto

#define KEYER_MEMORY_WINDOW_UP      45  // U% - Apertura finestra (% dell'elemento)
#define KEYER_MEMORY_WINDOW_DOWN    5   // D% - Chiusura finestra (% dall'inizio fine)

// Esempi configurazioni finestra:
// U=45%, D=5%  : Finestra centrale (45%-95% dell'elemento) - BILANCIATO
// U=30%, D=40% : Finestra ampia (30%-60%) - Memory facile
// U=99%, D=40% : Finestra ristretta (99%-60%=impossibile) - Quasi nessun memory
// U=60%, D=1%  : Finestra tardiva (60%-99%) - Memory solo a fine elemento

// === KEYER MODE ===
#define KEYER_MODE_STRAIGHT  0  // Straight key (no iambic)
#define KEYER_MODE_IAMBIC_A  1  // Iambic Mode A
#define KEYER_MODE_IAMBIC_B  2  // Iambic Mode B (Curtis)
#define KEYER_MODE_ULTIMATIC 3  // Ultimatic (ultimo paddle ha priorità)

#define KEYER_MODE_DEFAULT   KEYER_MODE_IAMBIC_B

// === DEBOUNCING ===
#define PADDLE_DEBOUNCE_US   1000   // Debounce in microsecondi (1ms)

// === TIMER HARDWARE ===
#define KEYER_TIMER_DIVIDER  80     // Prescaler per 1 MHz (80 MHz / 80 = 1 MHz)
                                     // 1 tick = 1 microsecondo

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================

// Modalità WiFi:
// 0 = Access Point (AP) - crea rete propria
// 1 = Station (STA) - si connette ad AP esistente
#define KEYER_WIFI_MODE_AP    0
#define KEYER_WIFI_MODE_STA   1

#define KEYER_WIFI_MODE       KEYER_WIFI_MODE_STA  // Modalità di default

// === ACCESS POINT MODE (quando WIFI_MODE = WIFI_MODE_AP) ===
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID        "IU3QEZ-Keyer"
#endif

#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD    "changeme00"   // Min 8 caratteri per WPA2
#endif
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4              // Max 4 client simultanei

// === STATION MODE (quando WIFI_MODE = WIFI_MODE_STA) ===
#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID       "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_STA_PASSWORD
#define WIFI_STA_PASSWORD   "your_wifi_pass"
#endif
#define WIFI_STA_TIMEOUT_MS 10000          // Timeout connessione (10 secondi)

// Se la connessione in modalità STA fallisce, fare fallback ad AP?
#define WIFI_STA_FALLBACK_TO_AP  true

#endif // SETTINGS_H
