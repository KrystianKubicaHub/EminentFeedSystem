idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        eminent_sdk
        driver
        esp_wifi
        nvs_flash
)
