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
#include "esp_timer.h"

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
#include "usb_debug.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "config_store.h"

#include "nvs_flash.h"
#include "led_strip.h"

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
static TimelineBuffer g_timeline_usb;
static KeyerLogic g_keyer;
static MorseDecoder g_decoder(&g_timeline_decoder);
static tone_generator_t g_tone_gen;
static led_strip_handle_t g_led_strip = nullptr;
static bool g_keying_active = false;
static uint32_t g_last_tone_off_us = 0;
static uint8_t g_led_phase = 0;
static uint8_t g_led_phase_pending = 0;
static bool g_led_override_active = false;

struct FeedbackStep {
    bool tone_on;
    bool led_on;
    uint32_t duration_ms;
};

struct FeedbackState {
    bool pending;
    bool active;
    size_t step_index;
    uint64_t next_transition_us;
};

static constexpr uint32_t kWifiFeedbackUnitMs = 300;
static constexpr FeedbackStep kWifiFeedbackSteps[] = {
    {true, true, 3 * kWifiFeedbackUnitMs},
    {false, false, 1 * kWifiFeedbackUnitMs},
    {true, true, 1 * kWifiFeedbackUnitMs},
    {false, false, 1 * kWifiFeedbackUnitMs},
    {true, true, 3 * kWifiFeedbackUnitMs},
    {false, false, 3 * kWifiFeedbackUnitMs},
};
static constexpr size_t kWifiFeedbackStepCount = sizeof(kWifiFeedbackSteps) / sizeof(kWifiFeedbackSteps[0]);

static FeedbackState g_wifi_feedback_state = {
    .pending = false,
    .active = false,
    .step_index = 0,
    .next_transition_us = 0,
};

#if ENABLE_AUDIO_LOOP_DEBUG
#define AUDIO_LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
#define AUDIO_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
static constexpr uint64_t kAudioLoopBudgetFloorUs = 30000ULL;

struct AudioLoopStats {
    uint64_t expected_loop_us;
    uint64_t max_loop_us;
    uint64_t max_decode_us;
    uint64_t max_fill_us;
    uint64_t max_codec_us;
    uint64_t last_loop_us;
    uint64_t last_decode_us;
    uint64_t last_fill_us;
    uint64_t last_codec_us;
    TickType_t last_report_ticks;
};

static AudioLoopStats g_audio_stats = {};
#else
#define AUDIO_LOGD(...) do { } while (0)
#define AUDIO_LOGW(...) do { } while (0)
#endif

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

static void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_led_strip) {
        return;
    }
    for (int i = 0; i < NUM_LEDS; ++i) {
        led_strip_set_pixel(g_led_strip, i, r, g, b);
    }
    led_strip_refresh(g_led_strip);
}

static void led_apply_phase(uint8_t phase)
{
    g_led_phase = phase;
    switch (phase) {
        case 0: // idle, less than one dot
            led_set_color(0, 0, 60);
            break;
        case 1: // keying active
            led_set_color(0, 0, 200);
            break;
        case 2: // >= 1 dot
            led_set_color(0, 255, 0);
            break;
        case 3: // >= char space
            led_set_color(255, 255, 0);
            break;
        case 4: // >= word space
            led_set_color(255, 128, 0);
            break;
        default:
            led_set_color(0, 0, 60);
            break;
    }
}

static void led_update_phase(uint8_t phase)
{
    g_led_phase_pending = phase;
    if (g_led_override_active) {
        return;
    }
    if (phase == g_led_phase) {
        return;
    }
    led_apply_phase(phase);
}

static void start_wifi_feedback_sequence(void)
{
    if (!g_led_strip) {
        g_wifi_feedback_state.pending = false;
        return;
    }
    g_led_override_active = true;
    g_wifi_feedback_state.active = true;
    g_wifi_feedback_state.pending = false;
    g_wifi_feedback_state.step_index = 0;
    g_wifi_feedback_state.next_transition_us = 0;
}

static void process_wifi_feedback_sequence(uint64_t now_us)
{
    if (g_keying_active) {
        if (g_wifi_feedback_state.active) {
            g_wifi_feedback_state.active = false;
            g_led_override_active = false;
            led_apply_phase(g_led_phase_pending);
            g_wifi_feedback_state.step_index = 0;
            g_wifi_feedback_state.next_transition_us = 0;
        }
        g_wifi_feedback_state.pending = false;
        return;
    }

    if (g_wifi_feedback_state.pending && !g_wifi_feedback_state.active) {
        if (!g_keying_active && !tone_generator_is_active(&g_tone_gen)) {
            start_wifi_feedback_sequence();
        }
    }

    if (!g_wifi_feedback_state.active) {
        return;
    }

    if (g_wifi_feedback_state.next_transition_us != 0 &&
        now_us < g_wifi_feedback_state.next_transition_us) {
        return;
    }

    if (g_wifi_feedback_state.step_index >= kWifiFeedbackStepCount) {
        if (!g_keying_active) {
            tone_generator_stop(&g_tone_gen);
        }
        g_wifi_feedback_state.active = false;
        g_wifi_feedback_state.step_index = 0;
        g_wifi_feedback_state.next_transition_us = 0;
        g_led_override_active = false;
        led_apply_phase(g_led_phase_pending);
        return;
    }

    const FeedbackStep &step = kWifiFeedbackSteps[g_wifi_feedback_state.step_index];
    if (!g_keying_active) {
        if (step.tone_on) {
            tone_generator_start(&g_tone_gen);
        } else {
            tone_generator_stop(&g_tone_gen);
        }
    }

    if (g_led_strip) {
        if (step.led_on) {
            led_set_color(0, 160, 0);
        } else {
            led_set_color(0, 0, 0);
        }
    }

    g_wifi_feedback_state.step_index++;
    g_wifi_feedback_state.next_transition_us = now_us + (uint64_t)step.duration_ms * 1000ULL;
}

static void keyerCallback(bool keying)
{
    gpio_set_level(static_cast<gpio_num_t>(KEY_PIN), keying ? 1 : 0);
    if (keying) {
        tone_generator_start(&g_tone_gen);
        g_keying_active = true;
        led_update_phase(1);
    } else {
        tone_generator_stop(&g_tone_gen);
        g_keying_active = false;
        g_last_tone_off_us = static_cast<uint32_t>(esp_timer_get_time());
        led_update_phase(0);
    }
}

static esp_err_t init_led_strip_device(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = NEOPIXEL_PIN,
        .max_leds = NUM_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    return led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip);
}

static void neopixel_boot_orange(void)
{
    if (!g_led_strip) {
        return;
    }
    led_set_color(255, 120, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "IU3QEZ ESP-IDF codec + expander bring-up test");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    ESP_ERROR_CHECK(usb_debug_init(&g_timeline_usb));
    if (init_led_strip_device() == ESP_OK) {
        neopixel_boot_orange();
    } else {
        ESP_LOGW(TAG, "NeoPixel strip init failed");
    }

    config_init();
    audio_settings_t audio_cfg;
    ESP_ERROR_CHECK(config_audio_get(&audio_cfg));

    persistent_config_t persisted_cfg;
    config_store_set_defaults(&persisted_cfg);
    bool cfg_loaded = false;
    ESP_ERROR_CHECK(config_store_load(&persisted_cfg, &cfg_loaded));
    ESP_LOGI(TAG, "Persistent config %s", cfg_loaded ? "loaded" : "defaults applied");
    audio_cfg.volume_percent = persisted_cfg.volume_percent;
    audio_cfg.tone_frequency_hz = persisted_cfg.tone_frequency_hz;
    audio_cfg.fade_in_ms = persisted_cfg.fade_in_ms;
    audio_cfg.fade_out_ms = persisted_cfg.fade_out_ms;

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

    ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_set_out_vol(codec, audio_cfg.volume_percent));
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

    g_keyer.setTimelineTargets(&g_timeline_decoder, &g_timeline_websocket, &g_timeline_usb);
    ESP_ERROR_CHECK(g_keyer.begin(keyerCallback) ? ESP_OK : ESP_FAIL);
    // TODO: Evaluate pinning the keyer task or Wi-Fi task to a specific core.
    g_decoder.setWebSocketTimeline(&g_timeline_websocket);
    g_decoder.setUsbTimeline(&g_timeline_usb);
    ESP_ERROR_CHECK(g_decoder.begin() ? ESP_OK : ESP_FAIL);
    g_decoder.setDotDuration(g_keyer.getDotDuration());

    tone_generator_init(&g_tone_gen, &audio_cfg);
    tone_generator_stop(&g_tone_gen);

    config_store_apply_to_runtime(&persisted_cfg, &g_keyer, &g_tone_gen, &g_decoder);
    g_decoder.setDotDuration(g_keyer.getDotDuration());
    audio_cfg.volume_percent = tone_generator_get_volume(&g_tone_gen);
    audio_cfg.tone_frequency_hz = tone_generator_get_frequency(&g_tone_gen);
    audio_cfg.fade_in_ms = tone_generator_get_fade_in_ms(&g_tone_gen);
    audio_cfg.fade_out_ms = tone_generator_get_fade_out_ms(&g_tone_gen);
    ESP_ERROR_CHECK(config_audio_update(&audio_cfg));

    ESP_ERROR_CHECK(wifi_manager_start());

    if (wifi_manager_sta_connected()) {
        g_wifi_feedback_state.pending = true;
    }

    web_server_config_t web_cfg = {
        .keyer = &g_keyer,
        .tone = &g_tone_gen,
        .decoder = &g_decoder,
        .timeline = &g_timeline_websocket,
    };
    ESP_ERROR_CHECK(web_server_start(web_cfg));

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

#if ENABLE_AUDIO_LOOP_DEBUG
    g_audio_stats = {};
    if (audio_cfg.sample_rate_hz > 0) {
        g_audio_stats.expected_loop_us = ((uint64_t)frame_count * 1000000ULL) / audio_cfg.sample_rate_hz;
    } else {
        g_audio_stats.expected_loop_us = 0;
    }
    g_audio_stats.last_report_ticks = xTaskGetTickCount();
#endif

    bool codec_ok = true;

    while (true) {
#if ENABLE_AUDIO_LOOP_DEBUG
        uint64_t loop_start_us = esp_timer_get_time();
        uint64_t decode_start_us = loop_start_us;
#endif

        g_decoder.process();
        g_decoder.checkTimeout();

#if ENABLE_AUDIO_LOOP_DEBUG
        uint64_t decode_end_us = esp_timer_get_time();
        uint64_t decode_duration_us = decode_end_us - decode_start_us;
        uint64_t fill_start_us = decode_end_us;
#endif

        tone_generator_fill(&g_tone_gen, frame_buffer, frame_count);

#if ENABLE_AUDIO_LOOP_DEBUG
        uint64_t fill_end_us = esp_timer_get_time();
        uint64_t fill_duration_us = fill_end_us - fill_start_us;
        uint64_t now_us_full_64 = fill_end_us;
#else
        uint64_t now_us_full_64 = esp_timer_get_time();
#endif

        process_wifi_feedback_sequence(now_us_full_64);
        uint32_t now_us_full = static_cast<uint32_t>(now_us_full_64);
        if (g_keying_active) {
            led_update_phase(1);
        } else if (g_last_tone_off_us != 0) {
            uint32_t elapsed = now_us_full - g_last_tone_off_us;
            uint32_t dot_us = g_keyer.getDotDuration();
            if (dot_us == 0) {
                dot_us = 1;
            }
            uint8_t target_phase = 0;
            if (elapsed >= dot_us * 7) {
                target_phase = 4;
            } else if (elapsed >= dot_us * 3) {
                target_phase = 3;
            } else if (elapsed >= dot_us) {
                target_phase = 2;
            }
            led_update_phase(target_phase);
        }

        int codec_result;
#if ENABLE_AUDIO_LOOP_DEBUG
        uint64_t codec_start_us = esp_timer_get_time();
        codec_result = codec_write_checked(codec, frame_buffer, sample_count * sizeof(int16_t));
        uint64_t codec_end_us = esp_timer_get_time();
        uint64_t codec_duration_us = codec_end_us - codec_start_us;
#else
        codec_result = codec_write_checked(codec, frame_buffer, sample_count * sizeof(int16_t));
#endif

#if ENABLE_AUDIO_LOOP_DEBUG
        uint64_t loop_end_us = esp_timer_get_time();
        uint64_t loop_duration_us = loop_end_us - loop_start_us;

        g_audio_stats.last_decode_us = decode_duration_us;
        g_audio_stats.last_fill_us = fill_duration_us;
        g_audio_stats.last_codec_us = codec_duration_us;
        g_audio_stats.last_loop_us = loop_duration_us;
        if (decode_duration_us > g_audio_stats.max_decode_us) {
            g_audio_stats.max_decode_us = decode_duration_us;
        }
        if (fill_duration_us > g_audio_stats.max_fill_us) {
            g_audio_stats.max_fill_us = fill_duration_us;
        }
        if (codec_duration_us > g_audio_stats.max_codec_us) {
            g_audio_stats.max_codec_us = codec_duration_us;
        }
        if (loop_duration_us > g_audio_stats.max_loop_us) {
            g_audio_stats.max_loop_us = loop_duration_us;
        }

        uint64_t budget_us = g_audio_stats.expected_loop_us;
        if (budget_us < kAudioLoopBudgetFloorUs) {
            budget_us = kAudioLoopBudgetFloorUs;
        }

        if (budget_us > 0 &&
            loop_duration_us > budget_us + 2000ULL) {
            AUDIO_LOGW("Audio loop overrun: %llu us (target ~%llu us, theoretical ~%llu us), decode=%llu us, fill=%llu us, codec=%llu us",
                       (unsigned long long)loop_duration_us,
                       (unsigned long long)budget_us,
                       (unsigned long long)g_audio_stats.expected_loop_us,
                       (unsigned long long)decode_duration_us,
                       (unsigned long long)fill_duration_us,
                       (unsigned long long)codec_duration_us);
        }
#endif

        TickType_t now_ticks = xTaskGetTickCount();

#if ENABLE_AUDIO_LOOP_DEBUG
        if (now_ticks - g_audio_stats.last_report_ticks >= pdMS_TO_TICKS(1000)) {
            uint64_t budget_us = g_audio_stats.expected_loop_us;
            if (budget_us < kAudioLoopBudgetFloorUs) {
                budget_us = kAudioLoopBudgetFloorUs;
            }
            AUDIO_LOGD("Audio loop stats: last=%llu us (target ~%llu us) max=%llu us; decode last/max=%llu/%llu us; fill last/max=%llu/%llu us; codec last/max=%llu/%llu us",
                       (unsigned long long)g_audio_stats.last_loop_us,
                       (unsigned long long)budget_us,
                       (unsigned long long)g_audio_stats.max_loop_us,
                       (unsigned long long)g_audio_stats.last_decode_us,
                       (unsigned long long)g_audio_stats.max_decode_us,
                       (unsigned long long)g_audio_stats.last_fill_us,
                       (unsigned long long)g_audio_stats.max_fill_us,
                       (unsigned long long)g_audio_stats.last_codec_us,
                       (unsigned long long)g_audio_stats.max_codec_us);
            g_audio_stats.max_loop_us = 0;
            g_audio_stats.max_decode_us = 0;
            g_audio_stats.max_fill_us = 0;
            g_audio_stats.max_codec_us = 0;
            g_audio_stats.last_report_ticks = now_ticks;
        }
#endif

        if (codec_result != ESP_CODEC_DEV_OK) {
            codec_ok = false;
            break;
        }

        if (now_ticks - last_report >= pdMS_TO_TICKS(500)) {
            bool paddle = read_paddle_level(tca_handle);
            if (paddle != last_paddle) {
                ESP_LOGI(TAG, "Paddle sense changed: %s", paddle ? "HIGH" : "LOW");
                last_paddle = paddle;
            }
            last_report = now_ticks;
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
