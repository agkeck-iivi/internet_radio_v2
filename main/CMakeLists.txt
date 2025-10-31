# set(COMPONENT_SRCS "internet_radio_adf.c" "audio_pipeline_manager.c")
# set(COMPONENT_ADD_INCLUDEDIRS .)


# idf_component_register(PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning)

# register_component()

set(COMPONENT_ADD_INCLUDEDIRS "")

idf_component_register(SRCS  "internet_radio_adf.c" "audio_pipeline_manager.c" "test_ssd1306.c"
                       PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning audio_pipeline audio_stream esp_peripherals
                       REQUIRES u8g2 u8g2-hal-esp-idf
                       INCLUDE_DIRS ".")