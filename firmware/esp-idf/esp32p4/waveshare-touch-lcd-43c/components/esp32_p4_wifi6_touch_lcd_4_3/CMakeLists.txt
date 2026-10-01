idf_component_register(
    SRCS "esp32_p4_wifi6_touch_lcd_4_3.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_lcd esp_lcd_touch esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_hw_support
    PRIV_REQUIRES esp_lcd_st7701 esp_lcd_touch_gt911
)
