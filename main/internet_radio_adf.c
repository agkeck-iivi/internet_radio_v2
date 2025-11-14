/* Play internet radio stations

    based on esp-adf code: pipeline_living_stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_pipeline_manager.h" 
#include "audio_event_iface.h"
#include "audio_common.h"
#include "i2s_stream.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#include "audio_idf_version.h"


#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "iot_button.h"
#include "button_gpio.h"
#include "driver/gpio.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#include "lvgl_ssd1306_setup.h"
#include "screens.h"

#include "station_data.h"
#include "encoders.h"


static const char* TAG = "INTERNET_RADIO";




// lcd with boost-buck converter: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/3584/MCCOG21605D6W-BNMLWI.pdf


#define GPIO_STATION_DOWN     14
#define GPIO_STATION_UP     12
#define GPIO_ACTIVE_LOW     0

// Volume control
#define INITIAL_VOLUME 0
// #define VOLUME_STEP    10ed

// oled screen with lvgl
extern int station_count; // from station_data.c
static lv_display_t* display;

// Static global variables for easier access in callbacks
audio_pipeline_components_t audio_pipeline_components = { 0 };
int current_station = 0;
static audio_board_handle_t board_handle = NULL;  // make this global during debugging
static audio_event_iface_handle_t evt = NULL;
static esp_periph_set_handle_t periph_set = NULL;

volatile float g_bitrate_kbps = 0.0f;
// Button Handles
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;



// Custom event definitions
#define CUSTOM_EVENT_SOURCE_ID ((void*)0x12345678) // Arbitrary ID for the source
#define CUSTOM_EVENT_TYPE_USER (AUDIO_ELEMENT_TYPE_PERIPH + 1) // Unique type
#define CUSTOM_CMD_PRINT_MESSAGE 1


static void save_current_station_to_nvs(int station_index)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(nvs_handle, "station_idx", station_index);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing 'station_idx' to NVS!", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "Saved current_station = %d to NVS", station_index);
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing 'station_idx' to NVS!", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}


// void lcd_update(void* param)
// {
//     const char* TAG = "LCD_Display";
//     int last_displayed_station = -1;
//     while (true) {
//         // Only update the display if the station has changed
//         if (ctx != NULL && current_station != last_displayed_station) {
//             ESP_LOGI(TAG, "Updating LCD for station: %s", radio_stations[current_station].call_sign);

//             lcd1602_clear(ctx);

//             // Display call sign on the first line
//             lcd1602_set_cursor(ctx, 0, 0);
//             lcd1602_string(ctx, radio_stations[current_station].call_sign);

//             // Display city on the second line
//             lcd1602_set_cursor(ctx, 1, 0);
//             lcd1602_string(ctx, radio_stations[current_station].city);

//             last_displayed_station = current_station;
//         }
//         vTaskDelay(pdMS_TO_TICKS(500)); // Check for station changes every 500ms
//     }
// }

void change_station(int new_station_index)
{
    esp_err_t ret;

    if (new_station_index < 0 || new_station_index >= station_count) {
        ESP_LOGE(TAG, "Invalid station index: %d", new_station_index);
        return;
    }

    ESP_LOGI(TAG, "Destroying current pipeline...");
    destroy_audio_pipeline(&audio_pipeline_components);

    current_station = new_station_index;
    ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station, radio_stations[current_station].call_sign, radio_stations[current_station].city);
    sync_station_encoder_index(); // Sync encoder's internal state
    save_current_station_to_nvs(current_station);
    update_station_name(radio_stations[current_station].call_sign);
    update_station_city(radio_stations[current_station].city);
    ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station, radio_stations[current_station].call_sign, radio_stations[current_station].city);

    ret = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new audio pipeline for station %s, %s. Error: %d", radio_stations[current_station].call_sign, radio_stations[current_station].city, ret);
        return;
    }

    ESP_LOGI(TAG, "Starting new audio pipeline");
    ret = audio_pipeline_run(audio_pipeline_components.pipeline);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run new audio pipeline. Error: %d", ret);
        destroy_audio_pipeline(&audio_pipeline_components);
    }
}

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t* wifi_sta_cfg = (wifi_sta_config_t*)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials"
                "\n\tSSID: %s\n\tPassword: %s",
                (const char*)wifi_sta_cfg->ssid,
                (const char*)wifi_sta_cfg->password);
            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t* reason = (wifi_prov_sta_fail_reason_t*)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason: %s"
                "\n\tPlease reset to factory and retry.",
                (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                "Wi-Fi station authentication failed" : "Wi-Fi station not found");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case WIFI_PROV_END:
            /* De-initialize manager once provisioning is finished */
            wifi_prov_mgr_deinit();
            break;
        default:
            break;
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    }
}

static void wifi_init_sta(void)
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char* service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char* ssid_prefix = "agk radio:";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
        ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief Task to measure and log the data throughput in kbps.
 */
static void data_throughput_task(void* pvParameters)
{
    uint64_t last_bytes_read = 0;
    uint64_t current_bytes_read;
    uint64_t bytes_read_in_last_second;
    int set_volume = 42;
    int temp_volume;
    while (1) {
        current_bytes_read = g_bytes_read;
        vTaskDelay(pdMS_TO_TICKS(1000));
        bytes_read_in_last_second = current_bytes_read - last_bytes_read;
        last_bytes_read = current_bytes_read;

        g_bitrate_kbps = (float)(bytes_read_in_last_second * 8) / 1000.0f;
        update_bitrate_label(g_bitrate_kbps);

        // ESP_LOGI("THROUGHPUT_MONITOR", "Data throughput: %.2f kbps", g_bitrate_kbps);
        // UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
        // ESP_LOGI(TAG, "MyTask High Water Mark: %d bytes", highWaterMark);
        // audio_hal_set_volume(board_handle->audio_hal, set_volume++);
        // audio_hal_get_volume(board_handle->audio_hal, &temp_volume);
        // ESP_LOGI(TAG, "Current volume set to %d", temp_volume);


    }
}

void app_main(void)
{
    display = lvgl_ssd1306_setup();
    screens_init(display);
    update_station_name(radio_stations[current_station].call_sign);
    update_station_city(radio_stations[current_station].city);
    int initial_volume = INITIAL_VOLUME;

    int temp_volume;
    esp_log_level_set("*", ESP_LOG_DEBUG);
    // esp_log_level_set(TAG, ESP_LOG_DEBUG);
    // esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs_handle;
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "Reading current_station from NVS");
        int32_t station_from_nvs = 0; // default to 0
        err = nvs_get_i32(nvs_handle, "station_idx", &station_from_nvs);
        switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Successfully read current_station = %d", (int)station_from_nvs);
            if (station_from_nvs >= 0 && station_from_nvs < station_count) {
                current_station = station_from_nvs;
            }
            else {
                ESP_LOGW(TAG, "Invalid station index %d found in NVS, defaulting to 0", (int)station_from_nvs);
                current_station = 0;
            }
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI(TAG, "The value 'station_idx' is not initialized yet!");
            break;
        default:
            ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
        }

        // Read volume from NVS
        ESP_LOGI(TAG, "Reading volume from NVS");
        int32_t volume_from_nvs = INITIAL_VOLUME;
        err = nvs_get_i32(nvs_handle, "volume", &volume_from_nvs);
        switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Successfully read volume = %d", (int)volume_from_nvs);
            if (volume_from_nvs >= 0 && volume_from_nvs <= 100) {
                initial_volume = volume_from_nvs;
            }
            else {
                ESP_LOGW(TAG, "Invalid volume %d found in NVS, defaulting to %d", (int)volume_from_nvs, INITIAL_VOLUME);
            }
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI(TAG, "The value 'volume' is not initialized yet!");
            break;
        default:
            ESP_LOGE(TAG, "Error (%s) reading 'volume'!", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    // start oled display test task,  remove after debugging
    // xTaskCreate(task_test_ssd1306, "u8g2_task", 4096, NULL, 5, NULL);


    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    // provisioned = false; // force provisioning for testing comment this line for production
    uint8_t custom_service_uuid[] = {
        /* LSB <---------------------------------------
         * ---------------------------------------> MSB */
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };

    if (!provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[20];
        get_device_service_name(service_name, sizeof(service_name));

        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL, service_name, NULL));
    }
    else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi");
    }

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    wifi_init_sta();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi Connected.");


    // wifi_event_group = xEventGroupCreate();

    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // esp_netif_create_default_wifi_sta();

    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // wifi_prov_mgr_config_t config = {
    //     .scheme = wifi_prov_scheme_ble,
    //     .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    // };

    // ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // bool provisioned = false;
    // ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    // provisioned = false; // force provisioning for testing comment this line for production
    // uint8_t custom_service_uuid[] = {
    //     /* LSB <---------------------------------------
    //      * ---------------------------------------> MSB */
    //     0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    //     0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    // };

    // if (!provisioned) {
    //     ESP_LOGI(TAG, "Starting provisioning");

    //     char service_name[20];
    //     get_device_service_name(service_name, sizeof(service_name));

    //     wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    //     ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL, service_name, NULL));
    // }
    // else {
    //     ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi");
    //     wifi_prov_mgr_deinit();

    //     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    //     /* Start Wi-Fi station */
    //     wifi_init_sta();

    // }

    // const TickType_t wifi_connect_timeout = portMAX_DELAY; // pdMS_TO_TICKS(30000); // 30 seconds
    // ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    // we should probably have a timeout here.  If it can't connect, restart provisioning.
    // EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, wifi_connect_timeout);

    // if ((bits & WIFI_CONNECTED_BIT) == 0) {
    //     ESP_LOGE(TAG, "Failed to connect to Wi-Fi within 30 seconds. Resetting provisioning and restarting.");
    //     // Erase provisioning data
    //     wifi_prov_mgr_reset_provisioning();
    //     vTaskDelay(pdMS_TO_TICKS(2000)); // Delay to allow logging
    //     esp_restart();
    // }


    ESP_LOGI(TAG, "Wi-Fi Connected.");

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_set = esp_periph_set_init(&periph_cfg);



    ESP_LOGI(TAG, "Start audio codec chip");
    board_handle = audio_board_init(); // Assign to static global
    // explicit start the codec, I'm not sure why it was not started elsewhere.
    board_handle->audio_hal->audio_codec_ctrl(AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, initial_volume);
    update_volume_slider(initial_volume);
    audio_hal_get_volume(board_handle->audio_hal, &temp_volume);
    ESP_LOGI(TAG, "Initial volume set to %d", temp_volume);


    ESP_LOGI(TAG, "Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg); // Assign to static global

    ESP_LOGI(TAG, "Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set), evt);


    ESP_LOGI(TAG, "Starting initial stream: %s, %s", radio_stations[current_station].call_sign, radio_stations[current_station].city);
    err = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create initial audio pipeline, error: %d", err);
        // Cleanup before returning
        if (evt) audio_event_iface_destroy(evt);
        if (periph_set) esp_periph_set_destroy(periph_set);
        // audio_board_deinit(board_handle); // If applicable
        return;
    }




    ESP_LOGI(TAG, "Start audio_pipeline");
    audio_pipeline_run(audio_pipeline_components.pipeline);

    xTaskCreate(data_throughput_task, "data_throughput_task", 3 * 1024, NULL, 5, NULL);

    //  start encoder pulse counters

    init_encoders(board_handle, initial_volume);
    // ESP_LOGI(TAG, "Initializing I2C LCD 16x2");
    // static i2c_master_bus_handle_t i2c_bus;
    // i2c_master_bus_config_t bus_cfg = {
    //     .clk_source = I2C_CLK_SRC_DEFAULT,
    //     .i2c_port = -1, // Use default I2C port
    //     .sda_io_num = LCD_I2C_SDA,
    //     .scl_io_num = LCD_I2C_SCL,
    //     .glitch_ignore_cnt = 7,
    //     .flags.enable_internal_pullup = true,
    // };
    // if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to initialize I2C bus");
    // }
    // else
    //     config.bus = &i2c_bus;
    // ctx = lcd1602_init(ESP_I2C_ADDRESS, true, &config);
    // if (NULL != ctx)
    // {
    //     lcd1602_set_display(ctx, true, false, false);
    // }

    // xTaskCreatePinnedToCore(&lcd_update, "LCD Update Task", 4 * 1024, NULL, 1, NULL, 0);

    while (1)
    {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }
        ESP_LOGI(TAG, "Received event from element: %X, command: %d", (int)msg.source, msg.cmd);

        // Handle the custom message
                // if (msg.source_type == CUSTOM_EVENT_TYPE_USER && msg.cmd == CUSTOM_CMD_PRINT_MESSAGE && msg.source == CUSTOM_EVENT_SOURCE_ID) {
                //     ESP_LOGI(TAG, "[ * ] Received Custom Message: %s", (char*)msg.data);
                //     continue;
                // }

                // if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void*)audio_pipeline_components.codec_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
                // {
                //     audio_element_info_t music_info = { 0 };
                //     audio_element_getinfo(audio_pipeline_components.codec_decoder, &music_info); // Make sure codec_decoder is valid

                //     ESP_LOGI(TAG, "[ * ] Receive music info from codec decoder, sample_rate=%d, bits=%d, ch=%d",
                //         music_info.sample_rates, music_info.bits, music_info.channels);

                //     i2s_stream_set_clk(audio_pipeline_components.i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
                //     continue;
                // }

        /* restart stream when the first pipeline element (http_stream_reader in this case) receives stop event (caused by reading errors) */
        if (audio_pipeline_components.http_stream_reader && msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void*)audio_pipeline_components.http_stream_reader && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int)msg.data == AEL_STATUS_ERROR_OPEN)
        {
            ESP_LOGW(TAG, "[ * ] Restart stream");
            audio_pipeline_stop(audio_pipeline_components.pipeline);
            audio_pipeline_wait_for_stop(audio_pipeline_components.pipeline);
            audio_element_reset_state(audio_pipeline_components.codec_decoder);
            audio_element_reset_state(audio_pipeline_components.i2s_stream_writer);
            audio_pipeline_reset_ringbuffer(audio_pipeline_components.pipeline);
            audio_pipeline_reset_items_state(audio_pipeline_components.pipeline);
            audio_pipeline_run(audio_pipeline_components.pipeline);
            continue;
        }
    }

    ESP_LOGI(TAG, "Stopping audio_pipeline");
    destroy_audio_pipeline(&audio_pipeline_components);

    if (periph_set) {
        esp_periph_set_stop_all(periph_set);
        if (evt) {
            audio_event_iface_remove_listener(esp_periph_set_get_event_iface(periph_set), evt);
        }
    }

    if (evt) {
        audio_event_iface_destroy(evt);
    }
    if (periph_set) {
        esp_periph_set_destroy(periph_set);
    }
    // audio_board_deinit(board_handle); // If applicable and not handled by esp_periph_set_destroy
}
