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


// #include "lcd1602/lcd1602.h"



// note: the arduino library for reset sends 0x03 3 times, then 0x02 once
//  0x03 wait 4500 us 0x03 wait 4500 us 0x03 wait 150 us 0x02
// this library only sends it twice.
// one of many resources: https://web.alfredstate.edu/faculty/weimandn/lcd/lcd_initialization/lcd_initialization_index.html




// static lcd1602_context* ctx = NULL; // Global context for LCD

// #define LCD_I2C_SDA     GPIO_NUM_21
// #define LCD_I2C_SCL     GPIO_NUM_22
// #define ESP_I2C_ADDRESS LCD1602_I2C_ADDRESS_DEFAULT

// static    i2c_lowlevel_config config = { 0 };


static const char* TAG = "INTERNET_RADIO";




// lcd with boost-buck converter: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/3584/MCCOG21605D6W-BNMLWI.pdf


#define GPIO_STATION_DOWN     14
#define GPIO_STATION_UP     12
#define GPIO_ACTIVE_LOW     0

// Volume control
#define INITIAL_VOLUME 100
// #define VOLUME_STEP    10ed


// Static global variables for easier access in callbacks
audio_pipeline_components_t audio_pipeline_components = { 0 };
static int current_station = 0;
audio_board_handle_t board_handle = NULL;  // make this global during debugging
static audio_event_iface_handle_t evt = NULL;
static esp_periph_set_handle_t periph_set = NULL;

// Button Handles
static button_handle_t station_down_btn_handle = NULL;
static button_handle_t station_up_btn_handle = NULL;

/* Event group to signal when Wi-Fi is connected */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
// Custom event definitions
#define CUSTOM_EVENT_SOURCE_ID ((void*)0x12345678) // Arbitrary ID for the source
#define CUSTOM_EVENT_TYPE_USER (AUDIO_ELEMENT_TYPE_PERIPH + 1) // Unique type
#define CUSTOM_CMD_PRINT_MESSAGE 1




/**
 * @brief Structure to define a radio station's properties.
 */
typedef struct
{
    const char* call_sign; // Station's call sign or name
    const char* city;      // Station's city
    const char* uri;       // Stream URI
    codec_type_t codec;    // Codec type for the stream
} station_t;

station_t radio_stations[] = {
    {"KEXP", "Seattle", "https://kexp.streamguys1.com/kexp160.aac", CODEC_TYPE_AAC},
    {"KBUT", "Crested Butte", "http://playerservices.streamtheworld.com/api/livestream-redirect/KBUTFM.mp3", CODEC_TYPE_MP3},
    {"KSUT", "4 Corners", "https://ksut.streamguys1.com/kute", CODEC_TYPE_AAC},
    {"KDUR", "Durango", "https://kdurradio.fortlewis.edu/stream", CODEC_TYPE_MP3},
    {"KOTO", "Telluride", "http://26193.live.streamtheworld.com/KOTOFM.mp3", CODEC_TYPE_MP3},
    {"KHEN", "Salida", "https://stream.pacificaservice.org:9000/khen_128", CODEC_TYPE_MP3},
    // {"KRCL", "Salt Lake City", "https://stream.xmission.com/krcl-high", CODEC_TYPE_AAC}, // 5 seconds only
    // {"KRCL", "Salt Lake City", "https://kcpw.xmission.com/kuaa", CODEC_TYPE_MP3},  // 5 seconds at a time
    {"KWSB", "Gunnison", "https://kwsb.streamguys1.com/live", CODEC_TYPE_MP3},
    {"KFFP", "Portland","http://listen.freeformportland.org:8000/stream", CODEC_TYPE_MP3},
    {"KBOO", "Portland","https://live.kboo.fm:8443/high", CODEC_TYPE_MP3},
    // {"KRRC", "Reed College", "https://stream.radiojar.com/3wg5hpdkfkeuv", CODEC_TYPE_MP3}

    // {"inet", "Radio Paradise", "http://stream.radioparadise.com/flac", CODEC_TYPE_FLAC},  // works (poorly) after xxx seconds
    // {"test", "netherlands", "http://stream.haarlem105.nl:8000/haarlem105DAB.flac", CODEC_TYPE_FLAC},
    // Add more stations here
};

int station_count = sizeof(radio_stations) / sizeof(radio_stations[0]);

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

static void station_up_button_cb(void* arg, void* usr_data) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Station Up Button Pressed");
    ESP_LOGI(TAG, "Destroying current pipeline...");
    destroy_audio_pipeline(&audio_pipeline_components);
    current_station = (current_station + 1) % station_count;
    save_current_station_to_nvs(current_station);
    ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station, radio_stations[current_station].call_sign, radio_stations[current_station].city);


    ret = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new audio pipeline for station %s, %s. Error: %d", radio_stations[current_station].call_sign, radio_stations[current_station].city, ret);
        return;
    }

    if (audio_pipeline_components.pipeline && evt) {
        ESP_LOGI(TAG, "Setting listener for new pipeline");
        ESP_LOGI(TAG, "Starting new audio pipeline");
        ret = audio_pipeline_run(audio_pipeline_components.pipeline);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to run new audio pipeline. Error: %d", ret);
            destroy_audio_pipeline(&audio_pipeline_components);
        }
        else {
            ESP_LOGI(TAG, "Successfully switched to station: %s, %s", radio_stations[current_station].call_sign, radio_stations[current_station].city);
        }
    }
    else {
        ESP_LOGE(TAG, "Pipeline or event handle is NULL after create_audio_pipeline. Cannot start.");
        if (audio_pipeline_components.pipeline) {
            destroy_audio_pipeline(&audio_pipeline_components);
        }
    }
}

static void station_down_button_cb(void* arg, void* usr_data) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Station Up Button Pressed");
    ESP_LOGI(TAG, "Destroying current pipeline...");
    destroy_audio_pipeline(&audio_pipeline_components);
    current_station = (current_station + station_count - 1) % station_count;
    save_current_station_to_nvs(current_station);
    ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station, radio_stations[current_station].call_sign, radio_stations[current_station].city);


    ret = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new audio pipeline for station %s, %s. Error: %d", radio_stations[current_station].call_sign, radio_stations[current_station].city, ret);
        return;
    }

    if (audio_pipeline_components.pipeline && evt) {
        ESP_LOGI(TAG, "Setting listener for new pipeline");
        ESP_LOGI(TAG, "Starting new audio pipeline");
        ret = audio_pipeline_run(audio_pipeline_components.pipeline);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to run new audio pipeline. Error: %d", ret);
            destroy_audio_pipeline(&audio_pipeline_components);
        }
        else {
            ESP_LOGI(TAG, "Successfully switched to station: %s, %s", radio_stations[current_station].call_sign, radio_stations[current_station].city);
        }
    }
    else {
        ESP_LOGE(TAG, "Pipeline or event handle is NULL after create_audio_pipeline. Cannot start.");
        if (audio_pipeline_components.pipeline) {
            destroy_audio_pipeline(&audio_pipeline_components);
        }
    }
}

static void init_buttons(void) {
    // button_event_args_t event_args = {.long_press.press_time = 0};
    button_config_t btn_cfg = {
        .long_press_time = 0, // Use component default
        .short_press_time = 0, // Use component default
    };

    // Volume Down Button
    // button_gpio_config_t vol_down_gpio_cfg = {
    //     .gpio_num = GPIO_VOLUME_DOWN,
    //     .active_level = GPIO_ACTIVE_LOW,
    //     .enable_power_save = false,
    //     .disable_pull = false };
    // esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &vol_down_gpio_cfg, &volume_down_btn_handle);
    // if (ret == ESP_OK) {
    //     iot_button_register_cb(volume_down_btn_handle, BUTTON_SINGLE_CLICK, NULL, volume_down_button_cb, NULL);
    // }
    // else {
    //     ESP_LOGE(TAG, "Failed to create volume down button: %s", esp_err_to_name(ret));
    // }

    // // Volume Up Button
    // button_gpio_config_t vol_up_gpio_cfg = { .gpio_num = GPIO_VOLUME_UP, .active_level = GPIO_ACTIVE_LOW };
    // ret = iot_button_new_gpio_device(&btn_cfg, &vol_up_gpio_cfg, &volume_up_btn_handle);
    // if (ret == ESP_OK) {
    //     iot_button_register_cb(volume_up_btn_handle, BUTTON_SINGLE_CLICK, NULL, volume_up_button_cb, NULL);
    // }
    // else {
    //     ESP_LOGE(TAG, "Failed to create volume up button: %s", esp_err_to_name(ret));
    // }

    // Station Up Button
    button_gpio_config_t station_up_gpio_cfg = { .gpio_num = GPIO_STATION_UP, .active_level = GPIO_ACTIVE_LOW };
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &station_up_gpio_cfg, &station_up_btn_handle);
    if (ret == ESP_OK) {
        iot_button_register_cb(station_up_btn_handle, BUTTON_SINGLE_CLICK, NULL, station_up_button_cb, NULL);
    }
    else {
        ESP_LOGE(TAG, "Failed to create station up button: %s", esp_err_to_name(ret));
    }
    button_gpio_config_t station_down_gpio_cfg = { .gpio_num = GPIO_STATION_DOWN, .active_level = GPIO_ACTIVE_LOW };
    ret = iot_button_new_gpio_device(&btn_cfg, &station_down_gpio_cfg, &station_down_btn_handle);
    if (ret == ESP_OK) {
        iot_button_register_cb(station_down_btn_handle, BUTTON_SINGLE_CLICK, NULL, station_down_button_cb, NULL);
    }
    else {
        ESP_LOGE(TAG, "Failed to create station down button: %s", esp_err_to_name(ret));
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
    const char* ssid_prefix = "agk radio mac:";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
        ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}


void app_main(void)
{
    int temp_volume;
    // esp_log_level_set("*", ESP_LOG_DEBUG);
    // esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);
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
        nvs_close(nvs_handle);
    }

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

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
        wifi_prov_mgr_deinit();

        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
        /* Start Wi-Fi station */
        wifi_init_sta();

    }

    const TickType_t wifi_connect_timeout = portMAX_DELAY; // pdMS_TO_TICKS(30000); // 30 seconds
    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
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
    return;
    audio_hal_set_volume(board_handle->audio_hal, INITIAL_VOLUME);
    audio_hal_get_volume(board_handle->audio_hal, &temp_volume);
    ESP_LOGI(TAG, "Initial volume set to %d", temp_volume);


    ESP_LOGI(TAG, "Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg); // Assign to static global

    ESP_LOGI(TAG, "Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set), evt);


    ESP_LOGI(TAG, "Initializing buttons");
    init_buttons();


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

    ESP_LOGI(TAG, "Deleting buttons");
    // if (volume_up_btn_handle) iot_button_delete(volume_up_btn_handle);
    // if (volume_down_btn_handle) iot_button_delete(volume_down_btn_handle);
    if (station_up_btn_handle) iot_button_delete(station_up_btn_handle);

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
