# set(COMPONENT_SRCS "internet_radio_adf.c" "audio_pipeline_manager.c")
# set(COMPONENT_ADD_INCLUDEDIRS .)


# idf_component_register(PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning)

# register_component()

set(COMPONENT_ADD_INCLUDEDIRS "")

idf_component_register(SRCS  "internet_radio_adf.c" "audio_pipeline_manager.c" "lvgl_ssd1306_setup.c" "screens.c"
                            "encoders.c"
                       PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning audio_pipeline audio_stream esp_peripherals
                       REQUIRES esp_lcd
                       INCLUDE_DIRS "." "../components/es8388_board")