#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"

#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "device/include/es8311_codec.h"

#define TAG "codec_input_demo"

#define I2C_PORT                 I2C_NUM_0
#define I2C_SCL_GPIO             10
#define I2C_SDA_GPIO             11
#define I2C_CLK_SPEED_HZ         400000

#define I2S_MCLK_GPIO            12
#define I2S_BCLK_GPIO            13
#define I2S_LRCK_GPIO            14
#define I2S_DOUT_GPIO            16

#define TCA9555_ADDR             ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_00
#define TCA_PIN_I2C_SEL          IO_EXPANDER_PIN_NUM_6   // P0.6 (EXIO7)
#define TCA_PIN_PA_ENABLE        IO_EXPANDER_PIN_NUM_8   // P1.0 (EXIO8)
#define TCA_PIN_PADDLE_SENSE     IO_EXPANDER_PIN_NUM_9   // P1.1 (EXIO10 test input)

#define SAMPLE_RATE_HZ           16000
#define TONE_FREQ_HZ             600
#define BUFFER_FRAMES            256
#define TONE_AMPLITUDE           12000

static esp_err_t init_i2c_bus(i2c_master_bus_handle_t *out_bus)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, out_bus);
}

static esp_err_t init_tca9555(esp_io_expander_handle_t *out_handle, i2c_master_bus_handle_t bus)
{
    esp_io_expander_handle_t handle = NULL;
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca95xx_16bit(bus, TCA9555_ADDR, &handle), TAG, "create TCA9555");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, TCA_PIN_I2C_SEL, IO_EXPANDER_OUTPUT), TAG, "set I2C sel dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(handle, TCA_PIN_I2C_SEL, 1), TAG, "enable codec I2C path");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, TCA_PIN_PA_ENABLE, IO_EXPANDER_OUTPUT), TAG, "set PA dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(handle, TCA_PIN_PA_ENABLE, 0), TAG, "disable PA initially");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(handle, TCA_PIN_PADDLE_SENSE, IO_EXPANDER_INPUT), TAG, "configure paddle sense input");

    ESP_LOGI(TAG, "Initial TCA9555 state:");
    esp_io_expander_print_state(handle);

    *out_handle = handle;
    return ESP_OK;
}

static esp_err_t init_i2s_tx(i2s_chan_handle_t *out_tx)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_handle_t tx_handle = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, NULL), TAG, "alloc I2S channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_LRCK_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };

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
    if (esp_io_expander_get_level(handle, TCA_PIN_PADDLE_SENSE, &level_mask) == ESP_OK) {
        return (level_mask & TCA_PIN_PADDLE_SENSE) != 0;
    }
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "IU3QEZ ESP-IDF codec + expander bring-up test");

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(init_i2c_bus(&i2c_bus));
    ESP_LOGI(TAG, "I2C bus ready on SDA%d / SCL%d", I2C_SDA_GPIO, I2C_SCL_GPIO);

    esp_io_expander_handle_t tca_handle = NULL;
    ESP_ERROR_CHECK(init_tca9555(&tca_handle, i2c_bus));

    i2s_chan_handle_t i2s_tx = NULL;
    ESP_ERROR_CHECK(init_i2s_tx(&i2s_tx));

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    ESP_ERROR_CHECK(ctrl_if ? ESP_OK : ESP_FAIL);

    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = i2s_tx,
        .rx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    ESP_ERROR_CHECK(data_if ? ESP_OK : ESP_FAIL);

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = NULL,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .adc_input = 0,
            .dac_output = 0,
            .mic_input = 0,
            .pa_voltage = 0,
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
        .sample_rate = SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };

    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_set_out_vol(codec, 60));
    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_open(codec, &fs));

    ESP_ERROR_CHECK(esp_io_expander_set_level(tca_handle, TCA_PIN_PA_ENABLE, 1));
    ESP_LOGI(TAG, "Power amplifier enabled via TCA9555");

    float phase = 0.0f;
    const float two_pi = 6.28318530718f;
    const float step = two_pi * (float)TONE_FREQ_HZ / (float)SAMPLE_RATE_HZ;
    int16_t frame_buffer[BUFFER_FRAMES * 2];
    bool last_paddle = read_paddle_level(tca_handle);
    TickType_t last_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "Starting sidetone playback (%d Hz) and monitoring paddle input", TONE_FREQ_HZ);

    while (true) {
        for (size_t i = 0; i < BUFFER_FRAMES; ++i) {
            float value = sinf(phase);
            phase += step;
            if (phase >= two_pi) {
                phase -= two_pi;
            }
            int16_t sample = (int16_t)(value * TONE_AMPLITUDE);
            frame_buffer[i * 2] = sample;
            frame_buffer[i * 2 + 1] = sample;
        }

        if (codec_write_checked(codec, frame_buffer, sizeof(frame_buffer)) != ESP_CODEC_DEV_OK) {
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

    ESP_LOGE(TAG, "Codec write loop exited unexpectedly");
    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_close(codec));
    ESP_ERROR_CHECK(esp_io_expander_set_level(tca_handle, TCA_PIN_PA_ENABLE, 0));
}
