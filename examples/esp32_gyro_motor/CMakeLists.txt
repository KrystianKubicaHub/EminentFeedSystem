cmake_minimum_required(VERSION 3.16)

# Point to EminentFeedSystem as an extra component
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../esp_idf_component"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_gyro_motor)
