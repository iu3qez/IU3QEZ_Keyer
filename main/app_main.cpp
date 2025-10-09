#include <cmath>
#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "config.h"
#include "tone_generator.h"
#include "timeline_buffer.h"
#include "keyer_logic.h"
#include "morse_decoder.h"
#include "settings.h"

#define TAG "codec_input_demo"

namespace {
constexpr i2c_port_num_t kI2CPort = I2C_NUM_0;
constexpr gpio_num_t kI2CSclGpio = static_cast<gpio_num_t>(I2C_SCL_PIN);
constexpr gpio_num_t kI2CSdaGpio = static_cast<gpio_num_t>(I2C_SDA_PIN);
constexpr gpio_num_t kI2SMclkGpio = static_cast<gpio_num_t>(I2S_MCLK_PIN);
constexpr gpio_num_t kI2SBclkGpio = static_cast<gpio_num_t>(I2S_BCLK_PIN);
constexpr gpio_num_t kI2SLrckGpio = static_cast<gpio_num_t>(I2S_LRCK_PIN);
constexpr gpio_num_t kI2SDoutGpio = static_cast<gpio_num_t>(I2S_DOUT_PIN);
constexpr uint32_t kTca9555Address = ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000;
constexpr uint32_t kTcaPinI2cSel = static_cast<uint32_t>(IO_EXPANDER_PIN_NUM_6);
constexpr uint32_t kTcaPinPaEnable = static_cast<uint32_t>(IO_EXPANDER_PIN_NUM_8);
constexpr uint32_t kTcaPinPaddleSense = static_cast<uint32_t>(IO_EXPANDER_PIN_NUM_9);
}

static TimelineBuffer g_timeline_decoder;
static TimelineBuffer g_timeline_websocket;
static KeyerLogic g_keyer;
static MorseDecoder g_decoder(&g_timeline_decoder);
static tone_generator_t g_tone_gen;

static void keyerCallback(bool keying);

extern "C" void app_main(void);

static esp_err_t init_i2c_bus(i2c_master_bus_handle_t *out_bus)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = kI2CPort;
    bus_config.sda_io_num = kI2CSdaGpio;
    bus_config.scl_io_num = kI2CSclGpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    return i2c_new_master_bus(&bus_config, out_bus);
}

static esp_err_t init_tca9555(esp_io_expander_handle_t *out_handle, i2c_master_bus_handle_t bus)
{
    esp_io_expander_handle_t handle = nullptr;
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca95xx_16bit(bus, static_cast<uint8_t>(kTca9555Address), &handle), TAG, "create TCA9555");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, kTcaPinI2cSel, IO_EXPANDER_OUTPUT), TAG, "set I2C sel dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(handle, kTcaPinI2cSel, 1), TAG, "enable codec I2C path");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, kTcaPinPaEnable, IO_EXPANDER_OUTPUT), TAG, "set PA dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(handle, kTcaPinPaEnable, 0), TAG, "disable PA initially");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, kTcaPinPaddleSense, IO_EXPANDER_INPUT), TAG, "configure paddle sense input");

    ESP_LOGI(TAG, "Initial TCA9555 state:");
    esp_io_expander_print_state(handle);

    *out_handle = handle;
    return ESP_OK;
}

static esp_err_t init_i2s_tx(i2s_chan_handle_t *out_tx, uint32_t sample_rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_handle_t tx_handle = nullptr;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, nullptr), TAG, "alloc I2S channel");

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = kI2SMclkGpio;
    std_cfg.gpio_cfg.bclk = kI2SBclkGpio;
    std_cfg.gpio_cfg.ws = kI2SLrckGpio;
    std_cfg.gpio_cfg.dout = kI2SDoutGpio;
    std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg), TAG, "init std mode");

    *out_tx = tx_handle;
    return ESP_OK;
}

static int codec_write_checked(esp_codec_dev_handle_t codec, void *data, size_t len)
{
    int ret = esp_codec_dev_write(codec, data, len);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec write failed: %d", ret);
    }
    return ret;
}

static bool read_paddle_level(esp_io_expander_handle_t handle)
{
    uint32_t level_mask = 0;
    if (esp_io_expander_get_level(handle, kTcaPinPaddleSense, &level_mask) == ESP_OK) {
        return (level_mask & kTcaPinPaddleSense) != 0;
    }
    return false;
}

static void keyerCallback(bool keying)
{
    gpio_set_level(static_cast<gpio_num_t>(KEY_PIN), keying ? 1 : 0);
    if (keying) {
        tone_generator_start(&g_tone_gen);
    } else {
        tone_generator_stop(&g_tone_gen);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "IU3QEZ ESP-IDF codec + expander bring-up test");

    config_init();
    audio_settings_t audio_cfg;
    ESP_ERROR_CHECK(config_audio_get(&audio_cfg));

    i2c_master_bus_handle_t i2c_bus = nullptr;
    ESP_ERROR_CHECK(init_i2c_bus(&i2c_bus));
    ESP_LOGI(TAG, "I2C bus ready on SDA%d / SCL%d", I2C_SDA_PIN, I2C_SCL_PIN);

    esp_io_expander_handle_t tca_handle = nullptr;
    ESP_ERROR_CHECK(init_tca9555(&tca_handle, i2c_bus));

    i2s_chan_handle_t i2s_tx = nullptr;
    ESP_ERROR_CHECK(init_i2s_tx(&i2s_tx, audio_cfg.sample_rate_hz));

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = kI2CPort,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    ESP_ERROR_CHECK(ctrl_if ? ESP_OK : ESP_FAIL);

    audio_codec_i2s_cfg_t i2s_data_cfg = {};
    i2s_data_cfg.port = I2S_NUM_0;
    i2s_data_cfg.rx_handle = nullptr;
    i2s_data_cfg.tx_handle = i2s_tx;
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    ESP_ERROR_CHECK(data_if ? ESP_OK : ESP_FAIL);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = nullptr,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
            .pa_gain = 0.0f,
        },
        .no_dac_ref = false,
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_ERROR_CHECK(codec_if ? ESP_OK : ESP_FAIL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    esp_codec_dev_handle_t codec = esp_codec_dev_new(&dev_cfg);
    ESP_ERROR_CHECK(codec ? ESP_OK : ESP_FAIL);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = audio_cfg.sample_rate_hz,
        .mclk_multiple = 0,
    };

    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_set_out_vol(codec, 60));
    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_open(codec, &fs));
    ESP_LOGI(TAG, "Codec configured: %lu Hz, %u bits, %u channels",
              (unsigned long)fs.sample_rate, (unsigned)fs.bits_per_sample, (unsigned)fs.channel);

    ESP_ERROR_CHECK(esp_io_expander_set_level(tca_handle, kTcaPinPaEnable, 1));
    ESP_LOGI(TAG, "Power amplifier enabled via TCA9555");

    gpio_config_t key_conf = {};
    key_conf.pin_bit_mask = (1ULL << KEY_PIN);
    key_conf.mode = GPIO_MODE_OUTPUT;
    key_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    key_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    key_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&key_conf));
    gpio_set_level(static_cast<gpio_num_t>(KEY_PIN), 0);

    g_keyer.setTimelineTargets(&g_timeline_decoder, &g_timeline_websocket);
    ESP_ERROR_CHECK(g_keyer.begin(keyerCallback) ? ESP_OK : ESP_FAIL);
    g_decoder.setWebSocketTimeline(&g_timeline_websocket);
    ESP_ERROR_CHECK(g_decoder.begin() ? ESP_OK : ESP_FAIL);
    g_decoder.setDotDuration(g_keyer.getDotDuration());

    tone_generator_init(&g_tone_gen, &audio_cfg);
    tone_generator_stop(&g_tone_gen);

    size_t frame_count = audio_cfg.buffer_frames;
    size_t sample_count = frame_count * 2;
    int16_t *frame_buffer = static_cast<int16_t*>(calloc(sample_count, sizeof(int16_t)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(frame_buffer ? ESP_OK : ESP_ERR_NO_MEM);
    if (frame_buffer == nullptr) {
        ESP_LOGE(TAG, "Unable to allocate audio buffer");
        ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_close(codec));
        ESP_ERROR_CHECK(esp_io_expander_set_level(tca_handle, kTcaPinPaEnable, 0));
        return;
    }

    bool last_paddle = read_paddle_level(tca_handle);
    TickType_t last_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "Starting sidetone playback (%u Hz) and monitoring paddle input",
             (unsigned)audio_cfg.tone_frequency_hz);

    bool codec_ok = true;

    while (true) {
        g_decoder.process();
        g_decoder.checkTimeout();

        tone_generator_fill(&g_tone_gen, frame_buffer, frame_count);

        if (codec_write_checked(codec, frame_buffer, sample_count * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            codec_ok = false;
            break;
        }

        TickType_t now = xTaskGetTickCount();
        if (now - last_report >= pdMS_TO_TICKS(500)) {
            bool paddle = read_paddle_level(tca_handle);
            if (paddle != last_paddle) {
                ESP_LOGI(TAG, "Paddle sense changed: %s", paddle ? "HIGH" : "LOW");
                last_paddle = paddle;
            } else {
                ESP_LOGI(TAG, "Paddle sense: %s", paddle ? "HIGH" : "LOW");
            }
            last_report = now;
        }
    }

    tone_generator_stop(&g_tone_gen);
    if (codec_ok) {
        while (tone_generator_is_active(&g_tone_gen)) {
            g_decoder.process();
            g_decoder.checkTimeout();
            tone_generator_fill(&g_tone_gen, frame_buffer, frame_count);
            if (codec_write_checked(codec, frame_buffer, sample_count * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
                codec_ok = false;
                break;
            }
        }
    }

    if (!codec_ok) {
        ESP_LOGE(TAG, "Codec write loop exited unexpectedly");
    } else {
        ESP_LOGI(TAG, "Codec write loop stopped cleanly");
    }
    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_close(codec));
    ESP_ERROR_CHECK(esp_io_expander_set_level(tca_handle, kTcaPinPaEnable, 0));
    free(frame_buffer);
}
