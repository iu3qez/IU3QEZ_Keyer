cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../idf_samples/espressif__esp_codec_dev
    ${CMAKE_CURRENT_LIST_DIR}/../idf_samples/espressif__esp_io_expander
    ${CMAKE_CURRENT_LIST_DIR}/../idf_samples/espressif__esp_io_expander_tca95xx_16bit
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iu3qez_codec_input_test)
