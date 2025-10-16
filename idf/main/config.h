#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "esp_err.h"

// ============================================================================
// DEBUG FLAGS
// ============================================================================

#ifndef ENABLE_MORSE_DECODER_DEBUG
#define ENABLE_MORSE_DECODER_DEBUG 0  // 1=Enable morse decoder debug logs
#endif

#ifndef ENABLE_AUDIO_LOOP_DEBUG
#define ENABLE_AUDIO_LOOP_DEBUG 0     // 1=Enable audio loop timing debug (overrun warnings, stats)
#endif

#ifndef ENABLE_HTTPD_LOG_SUPPRESSION
#define ENABLE_HTTPD_LOG_SUPPRESSION 1  // 1=Suppress verbose HTTP server logs
#endif

// ============================================================================
// GPIO PIN DEFINITIONS
// ============================================================================

// Keyer I/O
#define DOT_PIN         3   // GPIO3 - Paddle DOT (active LOW, pull-up)
#define DASH_PIN        4   // GPIO4 - Paddle DASH (active LOW, pull-up)
#define KEY_PIN         5   // GPIO5 - Key output

// NeoPixel Debug LED
#define NEOPIXEL_PIN    38  // GPIO38 - NeoPixel WS2812
#define NUM_LEDS        7   // Numero LED

// I2C Bus (shared: ES8311 + TCA9555)
#define I2C_SDA_PIN     11  // GPIO11 - SDA
#define I2C_SCL_PIN     10  // GPIO10 - SCL

// I2S Audio Output
#define I2S_MCLK_PIN    12  // GPIO12 - Master Clock
#define I2S_BCLK_PIN    13  // GPIO13 - Bit Clock
#define I2S_LRCK_PIN    14  // GPIO14 - Left/Right Clock
#define I2S_DOUT_PIN    16  // GPIO16 - Data Out to ES8311

// ============================================================================
// I2C DEVICE ADDRESSES
// ============================================================================

#define ES8311_I2C_ADDR         0x18  // ES8311 Audio Codec
#define TCA9555_DEFAULT_ADDR    0x20  // TCA9555 GPIO Expander

// TCA9555 GPIO Expander Pins
#define I2C_USB_SEL_PIN     6     // P0.6 (EXIO7) - I2C/USB selector (HIGH=I2C)
#define PA_ENABLE_PIN       0     // P1.0 (EXIO8) - Power Amplifier enable

// ============================================================================
// AUDIO CONFIGURATION
// ============================================================================

// I2S Audio
#define I2S_SAMPLE_RATE 16000  // Sample rate in Hz
#define I2S_CHANNELS    2      // Stereo

// Sidetone
#define SIDETONE_FREQ_HZ    550   // Frequenza sidetone (400-600 Hz)
#define SIDETONE_VOLUME     80    // Volume iniziale (0-100%)

// Anti-click ramps (milliseconds)
#define SIDETONE_RAMP_UP_MS   5   // Ramp up per evitare click
#define SIDETONE_RAMP_DOWN_MS 8   // Ramp down per evitare click

// Ramp samples calculation (depends on sample rate)
#define RAMP_UP_SAMPLES   ((I2S_SAMPLE_RATE * SIDETONE_RAMP_UP_MS) / 1000)
#define RAMP_DOWN_SAMPLES ((I2S_SAMPLE_RATE * SIDETONE_RAMP_DOWN_MS) / 1000)

// Wavetable
#define WAVETABLE_SIZE  64      // Samples in wavetable
#define TONE_AMPLITUDE  16384   // ~50% of max 16-bit signed

// ============================================================================
// KEYER CONFIGURATION - CURTIS MODE B
// ============================================================================

// WPM Speed
#define KEYER_WPM_DEFAULT   25     // Default speed in WPM (Words Per Minute)
#define KEYER_WPM_MIN       5      // Minimum WPM
#define KEYER_WPM_MAX       100    // Maximum WPM (for HST)

// Curtis Mode B - Memory Window
#define KEYER_MEMORY_WINDOW_UP      45  // U% - Window open (% of element)
#define KEYER_MEMORY_WINDOW_DOWN    5   // D% - Window close (% from end start)

// Keyer Modes
#define KEYER_MODE_STRAIGHT  0  // Straight key (no iambic)
#define KEYER_MODE_IAMBIC_A  1  // Iambic Mode A
#define KEYER_MODE_IAMBIC_B  2  // Iambic Mode B (Curtis)
#define KEYER_MODE_ULTIMATIC 3  // Ultimatic (last paddle has priority)

#define KEYER_MODE_DEFAULT   KEYER_MODE_IAMBIC_B

// Debouncing
#define PADDLE_DEBOUNCE_US   100   // Debounce in microseconds

// Hardware Timer
#define KEYER_TIMER_DIVIDER  80     // Prescaler for 1 MHz (80 MHz / 80 = 1 MHz)

// ============================================================================
// MORSE DECODER CONFIGURATION
// ============================================================================

#define DECODER_CHAR_SPACE_TOLERANCE_TENTHS  10  // Extra margin in tenths of dot (10 = +1 dot)
#define DECODER_WORD_SPACE_TOLERANCE_TENTHS  20  // Extra margin in tenths of dot (20 = +2 dots)

// Backward compatibility
#define DECODER_CHAR_SPACE_TOLERANCE  DECODER_CHAR_SPACE_TOLERANCE_TENTHS
#define DECODER_WORD_SPACE_TOLERANCE  DECODER_WORD_SPACE_TOLERANCE_TENTHS

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================

#define KEYER_WIFI_MODE_AP    0
#define KEYER_WIFI_MODE_STA   1
#define KEYER_WIFI_MODE       KEYER_WIFI_MODE_STA

// Access Point Mode
#define WIFI_AP_SSID        "IU3QEZ-Keyer"
#define WIFI_AP_PASSWORD    "cw73hst0"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

// Station Mode
#define WIFI_STA_SSID       "PONGO"
#define WIFI_STA_PASSWORD   "oratona1"
#define WIFI_STA_TIMEOUT_MS 10000

#define WIFI_STA_FALLBACK_TO_AP  true

// ============================================================================
// AUDIO SETTINGS (Runtime Configuration)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE_DEFAULT    I2S_SAMPLE_RATE
#define AUDIO_TONE_FREQUENCY_DEFAULT SIDETONE_FREQ_HZ
#define AUDIO_FADE_IN_MS_DEFAULT     SIDETONE_RAMP_UP_MS
#define AUDIO_FADE_OUT_MS_DEFAULT    SIDETONE_RAMP_DOWN_MS
#define AUDIO_BUFFER_FRAMES_DEFAULT  AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL
#define AUDIO_TONE_VOLUME_PERCENT_DEFAULT SIDETONE_VOLUME

#ifndef AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL
#define AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL 64U
#endif

// ============================================================================
// REMOTE CW KEYER NETWORK CLIENT CONFIGURATION
// ============================================================================
#define REMOTECW_ENABLED              0                      // 0=disabled, 1=enabled
#define REMOTECW_SERVER_IP            "192.168.1.100"        // Server IP address
#define REMOTECW_SERVER_PORT          7355                   // Default RemoteCW port (7355)
#define REMOTECW_USERNAME             "IU3QEZ_ESP32"         // Username for login
#define REMOTECW_CALLSIGN             "IU3QEZ"               // Callsign
#define REMOTECW_RECONNECT_DELAY_MS   5000                   // Delay before reconnect attempt
#define REMOTECW_PING_INTERVAL_MS     3000                   // Ping interval for latency measurement
#define REMOTECW_ACTIVITY_TIMEOUT_MS  10000                  // Disconnect if no activity
#define REMOTECW_TX_BUFFER_SIZE       1024                   // Transmit buffer size
#define REMOTECW_RX_BUFFER_SIZE       2048                   // Receive buffer size
#define REMOTECW_KEYING_FIFO_SIZE     128                    // Size of keying event FIFO

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t tone_frequency_hz;
    uint16_t fade_in_ms;
    uint16_t fade_out_ms;
    uint16_t buffer_frames;
    int16_t tone_amplitude;
    uint8_t volume_percent;
} audio_settings_t;

void config_init(void);
void config_audio_set_defaults(audio_settings_t *cfg);
esp_err_t config_audio_get(audio_settings_t *out);
esp_err_t config_audio_update(const audio_settings_t *cfg);
int16_t config_volume_percent_to_amplitude(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
