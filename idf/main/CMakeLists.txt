idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        driver
        esp_event
        esp_netif
        esp_timer
        esp_hw_support
        esp_phy
        esp_pm
        freertos
        esp_codec_dev
        esp_io_expander
        esp_io_expander_tca95xx_16bit
)
