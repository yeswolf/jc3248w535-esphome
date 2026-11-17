idf_component_register(
    SRCS "esp_lcd_axs15231b.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_lcd_touch esp_driver_gpio
)
