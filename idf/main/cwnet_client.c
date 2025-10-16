/**
 * @file cwnet_client.c
 * @brief RemoteCW Network Client Implementation for ESP32
 *
 * Implementation of TCP/IP client for DL4YHF's Remote CW Keyer protocol.
 * Allows ESP32 keyer to connect to a RemoteCW server and send keying events.
 */

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "cwnet_client.h"
#include "config.h"

static const char *TAG = "cwnet_client";

// ============================================================================
// Stream Encoding Functions
// ============================================================================

/**
 * @brief Encode duration into 7-bit compressed format
 *
 * Encoding scheme:
 *   0-31: 1 ms steps (0-31 ms)
 *   32-95: 4 ms steps (32-284 ms)
 *   96-127: 16 ms steps (288-1165 ms)
 */
uint8_t cwstream_encode_event(bool key_down, uint32_t duration_ms)
{
    uint8_t encoded = 0;

    // Set key state bit
    if (key_down) {
        encoded |= CWSTREAM_KEY_DOWN_MASK;
    }

    // Encode duration with non-linear compression
    uint8_t time_code;

    if (duration_ms <= 31) {
        // 0-31 ms: direct encoding (1 ms steps)
        time_code = (uint8_t)duration_ms;
    } else if (duration_ms <= 284) {
        // 32-284 ms: 4 ms steps
        time_code = 32 + ((duration_ms - 32) / 4);
        if (time_code > 95) time_code = 95;
    } else {
        // 288-1165 ms: 16 ms steps
        uint32_t ms = duration_ms;
        if (ms > CWSTREAM_MAX_MS) ms = CWSTREAM_MAX_MS;
        time_code = 96 + ((ms - 288) / 16);
        if (time_code > 127) time_code = 127;
    }

    encoded |= (time_code & CWSTREAM_TIME_MASK);

    ESP_LOGD(TAG, "Encode: key=%d, dur=%lu ms -> 0x%02X (time_code=%u)",
             key_down, duration_ms, encoded, time_code);

    return encoded;
}

/**
 * @brief Decode duration from 7-bit compressed format
 */
void cwstream_decode_event(uint8_t encoded, bool *key_down, uint32_t *duration_ms)
{
    *key_down = (encoded & CWSTREAM_KEY_DOWN_MASK) != 0;
    uint8_t time_code = encoded & CWSTREAM_TIME_MASK;

    if (time_code <= 31) {
        *duration_ms = time_code;
    } else if (time_code <= 95) {
        *duration_ms = 32 + ((time_code - 32) * 4);
    } else {
        *duration_ms = 288 + ((time_code - 96) * 16);
    }
}

// ============================================================================
// FIFO Functions
// ============================================================================

static inline bool fifo_is_empty(cwnet_keying_fifo_t *fifo)
{
    return fifo->head == fifo->tail;
}

static inline bool fifo_is_full(cwnet_keying_fifo_t *fifo)
{
    return ((fifo->head + 1) % REMOTECW_KEYING_FIFO_SIZE) == fifo->tail;
}

static inline uint32_t fifo_count(cwnet_keying_fifo_t *fifo)
{
    if (fifo->head >= fifo->tail) {
        return fifo->head - fifo->tail;
    } else {
        return REMOTECW_KEYING_FIFO_SIZE - fifo->tail + fifo->head;
    }
}

static bool fifo_push(cwnet_keying_fifo_t *fifo, uint8_t cmd_byte)
{
    if (fifo_is_full(fifo)) {
        return false;
    }

    fifo->events[fifo->head].cmd_byte = cmd_byte;
    fifo->events[fifo->head].time_of_reception = esp_timer_get_time() / 1000;
    fifo->head = (fifo->head + 1) % REMOTECW_KEYING_FIFO_SIZE;

    return true;
}

static bool fifo_pop(cwnet_keying_fifo_t *fifo, cwnet_keying_event_t *event)
{
    if (fifo_is_empty(fifo)) {
        return false;
    }

    *event = fifo->events[fifo->tail];
    fifo->tail = (fifo->tail + 1) % REMOTECW_KEYING_FIFO_SIZE;

    return true;
}

// ============================================================================
// Protocol Parser
// ============================================================================

static void parser_reset(cwnet_client_t *client)
{
    client->parser_state = CWNET_PSTATE_RX_CMD;
    client->current_command = CWNET_CMD_NONE;
    client->cmd_data_length = 0;
    client->cmd_data_index = 0;
}

static void handle_command(cwnet_client_t *client, uint8_t cmd, uint8_t *data, uint16_t len)
{
    switch (cmd) {
        case CWNET_CMD_CONNECT:
            if (len >= 1) {
                client->permissions = data[0];
                client->state = CWNET_STATE_LOGIN_CONFIRMED;
                ESP_LOGI(TAG, "Login confirmed! Permissions: 0x%02X", client->permissions);

                if (client->permissions & CWNET_PERMISSION_TRANSMIT) {
                    ESP_LOGI(TAG, "Transmit permission granted");
                    xEventGroupSetBits(client->event_group, CWNET_EVENT_TX_PERMITTED);
                }
                xEventGroupSetBits(client->event_group, CWNET_EVENT_CONNECTED);
            }
            break;

        case CWNET_CMD_PRINT:
            if (len > 0 && data[len-1] == 0) {
                ESP_LOGI(TAG, "Server message: %s", (char*)data);
            }
            break;

        case CWNET_CMD_TX_INFO:
            if (len >= 2) {
                int8_t tx_client = (int8_t)data[0];
                ESP_LOGI(TAG, "TX info: client %d has the key", tx_client);
                if (len > 1 && data[len-1] == 0) {
                    ESP_LOGD(TAG, "TX info text: %s", (char*)&data[1]);
                }
            }
            break;

        case CWNET_CMD_PING:
            // Echo ping back to server
            if (len == 16) {
                ESP_LOGD(TAG, "Ping received, echoing back");
                // Will be handled by adding to TX buffer in caller
            }
            break;

        case CWNET_CMD_DISCONN:
            ESP_LOGI(TAG, "Server requested disconnect");
            client->state = CWNET_STATE_DISCONNECTED;
            break;

        default:
            ESP_LOGD(TAG, "Unhandled command 0x%02X, len=%u", cmd, len);
            break;
    }
}

static void parse_byte(cwnet_client_t *client, uint8_t byte)
{
    switch (client->parser_state) {
        case CWNET_PSTATE_RX_CMD:
            client->current_command = byte & CWNET_CMD_MASK_COMMAND;
            client->cmd_data_index = 0;

            // Check block length indicator
            uint8_t len_type = byte & CWNET_CMD_MASK_BLOCKLEN;
            if (len_type == CWNET_CMD_MASK_NO_BLOCK) {
                // No block data
                client->cmd_data_length = 0;
                handle_command(client, client->current_command, NULL, 0);
                // Stay in CMD state
            } else if (len_type == CWNET_CMD_MASK_SHORT_BLOCK) {
                // 1-byte length follows
                client->parser_state = CWNET_PSTATE_RX_SHORT_LEN;
            } else if (len_type == CWNET_CMD_MASK_LONG_BLOCK) {
                // 2-byte length follows
                client->parser_state = CWNET_PSTATE_RX_LENGTH_LO;
            } else {
                // Reserved/unknown
                ESP_LOGW(TAG, "Unknown block length type: 0x%02X", byte);
            }
            break;

        case CWNET_PSTATE_RX_SHORT_LEN:
            client->cmd_data_length = byte;
            if (client->cmd_data_length == 0) {
                handle_command(client, client->current_command, NULL, 0);
                client->parser_state = CWNET_PSTATE_RX_CMD;
            } else {
                client->parser_state = CWNET_PSTATE_RX_BLOCK;
            }
            break;

        case CWNET_PSTATE_RX_LENGTH_LO:
            client->cmd_data_length = byte;
            client->parser_state = CWNET_PSTATE_RX_LENGTH_HI;
            break;

        case CWNET_PSTATE_RX_LENGTH_HI:
            client->cmd_data_length |= (byte << 8);
            if (client->cmd_data_length == 0) {
                handle_command(client, client->current_command, NULL, 0);
                client->parser_state = CWNET_PSTATE_RX_CMD;
            } else {
                client->parser_state = CWNET_PSTATE_RX_BLOCK;
            }
            break;

        case CWNET_PSTATE_RX_BLOCK:
            if (client->cmd_data_index < sizeof(client->cmd_data_buffer)) {
                client->cmd_data_buffer[client->cmd_data_index++] = byte;
            }

            if (client->cmd_data_index >= client->cmd_data_length) {
                // Block complete
                handle_command(client, client->current_command,
                             client->cmd_data_buffer, client->cmd_data_length);
                client->parser_state = CWNET_PSTATE_RX_CMD;
            }
            break;
    }
}

// ============================================================================
// Network Functions
// ============================================================================

static esp_err_t tcp_connect(cwnet_client_t *client)
{
    struct sockaddr_in server_addr;
    struct hostent *server;

    ESP_LOGI(TAG, "Connecting to %s:%u", client->server_ip, client->server_port);

    // Create socket
    client->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client->sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set non-blocking mode
    int flags = fcntl(client->sock, F_GETFL, 0);
    fcntl(client->sock, F_SETFL, flags | O_NONBLOCK);

    // Resolve hostname
    server = gethostbyname(client->server_ip);
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to resolve host: %s", client->server_ip);
        close(client->sock);
        client->sock = -1;
        return ESP_FAIL;
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(client->server_port);

    // Connect
    int ret = connect(client->sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        ESP_LOGE(TAG, "Connect failed: errno %d", errno);
        close(client->sock);
        client->sock = -1;
        return ESP_FAIL;
    }

    // Wait for connection with timeout
    fd_set write_fds;
    struct timeval timeout;
    FD_ZERO(&write_fds);
    FD_SET(client->sock, &write_fds);
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    ret = select(client->sock + 1, NULL, &write_fds, NULL, &timeout);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Connection timeout or error");
        close(client->sock);
        client->sock = -1;
        return ESP_FAIL;
    }

    // Check if connection succeeded
    int error;
    socklen_t len = sizeof(error);
    getsockopt(client->sock, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        ESP_LOGE(TAG, "Connection failed: error %d", error);
        close(client->sock);
        client->sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connected to server");
    client->state = CWNET_STATE_CONNECTED;
    client->last_activity_time = esp_timer_get_time() / 1000;

    return ESP_OK;
}

static esp_err_t send_login(cwnet_client_t *client)
{
    // Build login string: "username,CALLSIGN\0"
    char login_str[170];
    int len = snprintf(login_str, sizeof(login_str), "%s,%s",
                      client->username, client->callsign);
    login_str[len++] = '\0';  // Null terminator included in payload

    // Build packet: CMD | SHORT_BLOCK | LENGTH | DATA
    uint8_t packet[172];
    packet[0] = CWNET_CMD_CONNECT | CWNET_CMD_MASK_SHORT_BLOCK;
    packet[1] = (uint8_t)len;
    memcpy(&packet[2], login_str, len);

    int total_len = 2 + len;
    int sent = send(client->sock, packet, total_len, 0);

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send login: errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Login sent: user=%s, call=%s", client->username, client->callsign);
    client->state = CWNET_STATE_LOGIN_SENT;
    client->last_activity_time = esp_timer_get_time() / 1000;

    return ESP_OK;
}

static esp_err_t flush_tx_buffer(cwnet_client_t *client)
{
    if (client->tx_buffer_used == 0 || client->sock < 0) {
        return ESP_OK;
    }

    int sent = send(client->sock, client->tx_buffer, client->tx_buffer_used, 0);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Try again later
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Send failed: errno %d", errno);
        return ESP_FAIL;
    }

    if (sent > 0) {
        ESP_LOGD(TAG, "Sent %d bytes", sent);
        client->last_activity_time = esp_timer_get_time() / 1000;

        // Remove sent data from buffer
        if (sent < client->tx_buffer_used) {
            memmove(client->tx_buffer, client->tx_buffer + sent,
                   client->tx_buffer_used - sent);
        }
        client->tx_buffer_used -= sent;
    }

    return ESP_OK;
}

static esp_err_t receive_data(cwnet_client_t *client)
{
    if (client->sock < 0) {
        return ESP_FAIL;
    }

    uint8_t temp_buf[256];
    int received = recv(client->sock, temp_buf, sizeof(temp_buf), 0);

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Receive failed: errno %d", errno);
        return ESP_FAIL;
    } else if (received == 0) {
        // Connection closed
        ESP_LOGI(TAG, "Server closed connection");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Received %d bytes", received);
    client->last_activity_time = esp_timer_get_time() / 1000;

    // Parse received bytes
    for (int i = 0; i < received; i++) {
        parse_byte(client, temp_buf[i]);
    }

    return ESP_OK;
}

// ============================================================================
// Client Task
// ============================================================================

static void cwnet_task(void *arg)
{
    cwnet_client_t *client = (cwnet_client_t *)arg;

    ESP_LOGI(TAG, "RemoteCW client task started");

    while (client->running) {
        int64_t now = esp_timer_get_time() / 1000;  // milliseconds

        switch (client->state) {
            case CWNET_STATE_DISCONNECTED:
                if (now >= client->reconnect_time) {
                    client->state = CWNET_STATE_CONNECTING;
                }
                break;

            case CWNET_STATE_CONNECTING:
                if (tcp_connect(client) == ESP_OK) {
                    send_login(client);
                } else {
                    client->state = CWNET_STATE_DISCONNECTED;
                    client->reconnect_time = now + REMOTECW_RECONNECT_DELAY_MS;
                    ESP_LOGI(TAG, "Will retry connection in %d ms", REMOTECW_RECONNECT_DELAY_MS);
                }
                break;

            case CWNET_STATE_CONNECTED:
            case CWNET_STATE_LOGIN_SENT:
            case CWNET_STATE_LOGIN_CONFIRMED:
                // Check activity timeout
                if (now - client->last_activity_time > REMOTECW_ACTIVITY_TIMEOUT_MS) {
                    ESP_LOGW(TAG, "Activity timeout, disconnecting");
                    client->state = CWNET_STATE_DISCONNECTED;
                    if (client->sock >= 0) {
                        close(client->sock);
                        client->sock = -1;
                    }
                    xEventGroupClearBits(client->event_group,
                                        CWNET_EVENT_CONNECTED | CWNET_EVENT_TX_PERMITTED);
                    client->reconnect_time = now + REMOTECW_RECONNECT_DELAY_MS;
                    break;
                }

                // Receive data
                if (receive_data(client) != ESP_OK) {
                    ESP_LOGW(TAG, "Receive error, disconnecting");
                    client->state = CWNET_STATE_DISCONNECTED;
                    if (client->sock >= 0) {
                        close(client->sock);
                        client->sock = -1;
                    }
                    xEventGroupClearBits(client->event_group,
                                        CWNET_EVENT_CONNECTED | CWNET_EVENT_TX_PERMITTED);
                    client->reconnect_time = now + REMOTECW_RECONNECT_DELAY_MS;
                    break;
                }

                // Send keying events from FIFO
                if (client->state == CWNET_STATE_LOGIN_CONFIRMED &&
                    !fifo_is_empty(&client->keying_fifo)) {

                    // Count how many events we can send
                    uint32_t count = fifo_count(&client->keying_fifo);
                    if (count > 100) count = 100;  // Limit per packet

                    // Check if we have space in TX buffer
                    uint32_t needed = 2 + count;  // cmd + len + data
                    if (client->tx_buffer_used + needed <= REMOTECW_TX_BUFFER_SIZE) {
                        // Build MORSE command packet
                        uint8_t *p = &client->tx_buffer[client->tx_buffer_used];
                        *p++ = CWNET_CMD_MORSE | CWNET_CMD_MASK_SHORT_BLOCK;
                        *p++ = (uint8_t)count;

                        // Copy events from FIFO
                        for (uint32_t i = 0; i < count; i++) {
                            cwnet_keying_event_t event;
                            if (fifo_pop(&client->keying_fifo, &event)) {
                                *p++ = event.cmd_byte;
                            }
                        }

                        client->tx_buffer_used += (2 + count);
                        ESP_LOGD(TAG, "Queued %lu keying events", count);
                    }
                }

                // Flush TX buffer
                flush_tx_buffer(client);

                // Send periodic ping
                if (client->state == CWNET_STATE_LOGIN_CONFIRMED &&
                    now - client->last_ping_time > REMOTECW_PING_INTERVAL_MS) {
                    // TODO: Implement proper ping with timestamps
                    client->last_ping_time = now;
                }

                break;

            case CWNET_STATE_ERROR:
                if (client->sock >= 0) {
                    close(client->sock);
                    client->sock = -1;
                }
                client->state = CWNET_STATE_DISCONNECTED;
                client->reconnect_time = now + REMOTECW_RECONNECT_DELAY_MS;
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // 20 ms poll interval
    }

    // Cleanup
    if (client->sock >= 0) {
        close(client->sock);
        client->sock = -1;
    }

    ESP_LOGI(TAG, "RemoteCW client task stopped");
    client->task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t cwnet_client_init(cwnet_client_t *client,
                            const char *server_ip,
                            uint16_t server_port,
                            const char *username,
                            const char *callsign)
{
    if (!client || !server_ip || !username || !callsign) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(client, 0, sizeof(cwnet_client_t));

    strncpy(client->server_ip, server_ip, sizeof(client->server_ip) - 1);
    client->server_port = server_port;
    strncpy(client->username, username, sizeof(client->username) - 1);
    strncpy(client->callsign, callsign, sizeof(client->callsign) - 1);

    client->state = CWNET_STATE_DISCONNECTED;
    client->sock = -1;
    client->ping_latency_ms = -1;

    parser_reset(client);

    client->event_group = xEventGroupCreate();
    if (!client->event_group) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Client initialized: server=%s:%u, user=%s, call=%s",
             server_ip, server_port, username, callsign);

    return ESP_OK;
}

esp_err_t cwnet_client_start(cwnet_client_t *client)
{
    if (!client || client->running) {
        return ESP_ERR_INVALID_STATE;
    }

    client->running = true;
    client->reconnect_time = 0;  // Connect immediately

    BaseType_t ret = xTaskCreate(cwnet_task, "cwnet_task", 4096, client, 5, &client->task_handle);
    if (ret != pdPASS) {
        client->running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Client started");
    return ESP_OK;
}

esp_err_t cwnet_client_stop(cwnet_client_t *client)
{
    if (!client || !client->running) {
        return ESP_ERR_INVALID_STATE;
    }

    client->running = false;

    // Wait for task to finish (with timeout)
    int timeout = 100;  // 1 second
    while (client->task_handle && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (client->event_group) {
        vEventGroupDelete(client->event_group);
        client->event_group = NULL;
    }

    ESP_LOGI(TAG, "Client stopped");
    return ESP_OK;
}

cwnet_state_t cwnet_client_get_state(cwnet_client_t *client)
{
    return client ? client->state : CWNET_STATE_DISCONNECTED;
}

bool cwnet_client_can_transmit(cwnet_client_t *client)
{
    if (!client) return false;

    return (client->state == CWNET_STATE_LOGIN_CONFIRMED) &&
           (client->permissions & CWNET_PERMISSION_TRANSMIT);
}

esp_err_t cwnet_client_send_keying_event(cwnet_client_t *client,
                                         bool key_down,
                                         uint32_t duration_ms)
{
    if (!client) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cwnet_client_can_transmit(client)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Encode event
    uint8_t encoded = cwstream_encode_event(key_down, duration_ms);

    // Add to FIFO
    if (!fifo_push(&client->keying_fifo, encoded)) {
        ESP_LOGW(TAG, "Keying FIFO full, event dropped");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "Keying event queued: key=%d, dur=%lu ms", key_down, duration_ms);

    return ESP_OK;
}

int cwnet_client_get_latency_ms(cwnet_client_t *client)
{
    return client ? client->ping_latency_ms : -1;
}
