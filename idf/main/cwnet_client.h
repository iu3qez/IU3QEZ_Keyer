#ifndef CWNET_CLIENT_H
#define CWNET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// RemoteCW Network Protocol Definitions (from CwNet.h)
// ============================================================================

// Command masks for block length encoding
#define CWNET_CMD_MASK_BLOCKLEN    0xC0
#define CWNET_CMD_MASK_NO_BLOCK    0x00
#define CWNET_CMD_MASK_SHORT_BLOCK 0x40  // 1-byte length (max 255 bytes)
#define CWNET_CMD_MASK_LONG_BLOCK  0x80  // 2-byte length (max 65535 bytes)
#define CWNET_CMD_MASK_RESERVE     0xC0
#define CWNET_CMD_MASK_COMMAND     0x3F

// Protocol commands
#define CWNET_CMD_NONE     0x00
#define CWNET_CMD_CONNECT  0x01  // Connect/Connected
#define CWNET_CMD_DISCONN  0x02  // Disconnect
#define CWNET_CMD_PING     0x03  // Ping test
#define CWNET_CMD_PRINT    0x04  // Text message
#define CWNET_CMD_TX_INFO  0x05  // Who has the key
#define CWNET_CMD_MORSE    0x10  // Morse code keying events
#define CWNET_CMD_AUDIO    0x11  // Audio samples (A-Law)
#define CWNET_CMD_CI_V     0x14  // Icom CI-V packet
#define CWNET_CMD_SPECTRUM 0x15  // Spectrum data
#define CWNET_CMD_FREQ_REPORT 0x16  // Frequency report

// Client permissions
#define CWNET_PERMISSION_NONE     0x00
#define CWNET_PERMISSION_TALK     0x01  // Can send text
#define CWNET_PERMISSION_TRANSMIT 0x02  // Can transmit CW
#define CWNET_PERMISSION_CTRL_RIG 0x04  // Can control rig
#define CWNET_PERMISSION_ADMIN    0x08  // Administrator

// Client connection states
typedef enum {
    CWNET_STATE_DISCONNECTED = 0,
    CWNET_STATE_CONNECTING,
    CWNET_STATE_CONNECTED,
    CWNET_STATE_LOGIN_SENT,
    CWNET_STATE_LOGIN_CONFIRMED,
    CWNET_STATE_ERROR
} cwnet_state_t;

// Parser states for streaming protocol
typedef enum {
    CWNET_PSTATE_RX_CMD = 0,
    CWNET_PSTATE_RX_SHORT_LEN,
    CWNET_PSTATE_RX_LENGTH_LO,
    CWNET_PSTATE_RX_LENGTH_HI,
    CWNET_PSTATE_RX_BLOCK
} cwnet_parser_state_t;

// ============================================================================
// Keying Stream Encoding (from CwStreamEnc.h)
// ============================================================================

// Morse keying event encoding:
// Bit 7: Key state (1=key down, 0=key up)
// Bits 6-0: Time delay before applying state (compressed to 7 bits)
//   0-31: 1 ms steps (0-31 ms)
//   32-95: 4 ms steps (32-284 ms)
//   96-127: 16 ms steps (288-1165 ms)

#define CWSTREAM_KEY_DOWN_MASK  0x80
#define CWSTREAM_TIME_MASK      0x7F

// Maximum milliseconds that can be encoded
#define CWSTREAM_MAX_MS         1165

// ============================================================================
// Keying FIFO
// ============================================================================

typedef struct {
    uint8_t cmd_byte;           // Encoded key state + timing
    int32_t time_of_reception;  // Timestamp (for RX side only)
} cwnet_keying_event_t;

typedef struct {
    cwnet_keying_event_t events[REMOTECW_KEYING_FIFO_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} cwnet_keying_fifo_t;

// ============================================================================
// Client Instance
// ============================================================================

typedef struct {
    // Configuration
    char server_ip[64];
    uint16_t server_port;
    char username[84];
    char callsign[84];

    // Connection state
    cwnet_state_t state;
    int sock;
    int permissions;
    int ping_latency_ms;

    // Timing
    int64_t last_activity_time;
    int64_t last_ping_time;
    int64_t reconnect_time;

    // Buffers
    uint8_t tx_buffer[REMOTECW_TX_BUFFER_SIZE];
    uint32_t tx_buffer_used;
    uint8_t rx_buffer[REMOTECW_RX_BUFFER_SIZE];
    uint32_t rx_buffer_used;

    // Protocol parser
    cwnet_parser_state_t parser_state;
    uint8_t current_command;
    uint16_t cmd_data_length;
    uint16_t cmd_data_index;
    uint8_t cmd_data_buffer[512];

    // Keying FIFO
    cwnet_keying_fifo_t keying_fifo;
    int64_t last_key_state_time;
    bool last_key_state;

    // Thread control
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
    bool running;

} cwnet_client_t;

// Event group bits
#define CWNET_EVENT_CONNECTED    BIT0
#define CWNET_EVENT_DISCONNECTED BIT1
#define CWNET_EVENT_TX_PERMITTED BIT2

// ============================================================================
// Function Prototypes
// ============================================================================

/**
 * @brief Initialize the RemoteCW client
 * @param client Client instance
 * @param server_ip Server IP address
 * @param server_port Server port (usually 7355)
 * @param username Username for authentication
 * @param callsign Callsign
 * @return ESP_OK on success
 */
esp_err_t cwnet_client_init(cwnet_client_t *client,
                            const char *server_ip,
                            uint16_t server_port,
                            const char *username,
                            const char *callsign);

/**
 * @brief Start the RemoteCW client (connects to server)
 * @param client Client instance
 * @return ESP_OK on success
 */
esp_err_t cwnet_client_start(cwnet_client_t *client);

/**
 * @brief Stop the RemoteCW client
 * @param client Client instance
 * @return ESP_OK on success
 */
esp_err_t cwnet_client_stop(cwnet_client_t *client);

/**
 * @brief Get current connection state
 * @param client Client instance
 * @return Current state
 */
cwnet_state_t cwnet_client_get_state(cwnet_client_t *client);

/**
 * @brief Check if client can transmit
 * @param client Client instance
 * @return true if client has transmit permission and is connected
 */
bool cwnet_client_can_transmit(cwnet_client_t *client);

/**
 * @brief Send a keying event to the server
 * @param client Client instance
 * @param key_down true for key down, false for key up
 * @param duration_ms Duration of previous state in milliseconds
 * @return ESP_OK on success
 */
esp_err_t cwnet_client_send_keying_event(cwnet_client_t *client,
                                         bool key_down,
                                         uint32_t duration_ms);

/**
 * @brief Get measured ping latency
 * @param client Client instance
 * @return Latency in milliseconds, or -1 if not measured yet
 */
int cwnet_client_get_latency_ms(cwnet_client_t *client);

// ============================================================================
// Keying Stream Encoding Functions
// ============================================================================

/**
 * @brief Encode a keying event into the compressed format
 * @param key_down Key state (true=down, false=up)
 * @param duration_ms Duration in milliseconds
 * @return Encoded byte
 */
uint8_t cwstream_encode_event(bool key_down, uint32_t duration_ms);

/**
 * @brief Decode a keying event from compressed format
 * @param encoded Encoded byte
 * @param key_down Output: key state
 * @param duration_ms Output: duration in milliseconds
 */
void cwstream_decode_event(uint8_t encoded, bool *key_down, uint32_t *duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* CWNET_CLIENT_H */
