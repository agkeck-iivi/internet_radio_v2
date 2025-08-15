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
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_pipeline_manager.h" // Include the new header
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


// #include "HD44780.h"  // for lcd 16x2 i2c backpack

#define LCD_ADDR 0x27
#define SDA_PIN  21
#define SCL_PIN  22
#define LCD_COLS 16
#define LCD_ROWS 2

#include "lcd1602/lcd1602.h"


// note: the arduino library for reset sends 0x03 3 times, then 0x02 once
//  0x03 wait 4500 us 0x03 wait 4500 us 0x03 wait 150 us 0x02
// this library only sends it twice.
// one of many resources: https://web.alfredstate.edu/faculty/weimandn/lcd/lcd_initialization/lcd_initialization_index.html



// void LCD_DemoTask(void* param)
// {
//     char num[20];
//     while (true) {
//         // LCD_reset();
//         LCD_home();
//         LCD_clearScreen();
//         LCD_writeStr("16x2 I2C LCD");
//         vTaskDelay(1000 / portTICK_RATE_MS);
//         LCD_clearScreen();
//         LCD_writeStr("Lets Count 0-10!");
//         vTaskDelay(1000 / portTICK_RATE_MS);
//         LCD_clearScreen();
//         // for (int i = 0; i <= 10; i++) {
//         //     LCD_setCursor(8, 1);
//         //     sprintf(num, "%d", i);
//         //     LCD_writeStr(num);
//         //     vTaskDelay(1000 / portTICK_RATE_MS);
//         // }

//     }
// }

//    lcd1602_context *ctx = lcd1602_init(ESP_I2C_ADDRESS, true, &config);
static lcd1602_context* ctx = NULL; // Global context for LCD

void LCD_DemoTask(void* param)
{
    const char* TAG = "LCD_demo";
    char num[20];
    int i=0;
    while (true) {
        // LCD_reset();
        int res;
        res = lcd1602_reset(ctx);
        if(res != 0)
        {
            ESP_LOGE(TAG, "Failed to reset LCD: %d", res);
        }
        res = lcd1602_clear(ctx);
        lcd1602_string(ctx, "count = ");
        lcd1602_set_cursor(ctx, 1, 0);
        sprintf(num, "%d", i++);
        lcd1602_string(ctx, num);
        lcd1602_set_display(ctx, true, false, false);
        vTaskDelay(pdMS_TO_TICKS(1000));

            // LCD_home();
            // LCD_clearScreen();
            // LCD_writeStr("16x2 I2C LCD");
            // vTaskDelay(1000 / portTICK_RATE_MS);
            // LCD_clearScreen();
            // LCD_writeStr("Lets Count 0-10!");
            // vTaskDelay(1000 / portTICK_RATE_MS);
            // LCD_clearScreen();
            // for (int i = 0; i <= 10; i++) {
            //     LCD_setCursor(8, 1);
            //     sprintf(num, "%d", i);
            //     LCD_writeStr(num);
            //     vTaskDelay(1000 / portTICK_RATE_MS);
            // }

    }
}

static const char* TAG = "INTERNET_RADIO";



// NOTES:

// Current i2s_stream.h implementation fails with this board
// The i2c interface works at init time but after starting the i2s stream we can
// no longer access the volume control 
// This bug claims a fix is in the works: https://github.com/espressif/esp-idf/issues/14030
// This bug is almost exactly the same behavior as the one I am seeing: https://github.com/espressif/esp-adf/issues/1334

// low pass filter: 100 \Omega, 22 pF with 1K pullup resistor works for about 15 minutes (900 writes)
// spreadsheet with rc values for various filters: https://docs.google.com/spreadsheets/d/1gqpgkPefZ_oJ2fmMdUvjdyCvKpkRdYS4wy-v37hEgWs/edit?gid=0#gid=0


// pinout https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html#header-block
// GPIO definitions for buttons

// lcd 16x2 with i2c backpack and 3.3v power: https://www.buydisplay.com/3-3v-or-5v-lcd-module-16x2-1602-character-display-i2c-arduino-wide-view

// lcd with boost-buck converter: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/3584/MCCOG21605D6W-BNMLWI.pdf


// #define GPIO_VOLUME_UP      14
// #define GPIO_VOLUME_DOWN    27
#define GPIO_STATION_DOWN     14
#define GPIO_STATION_UP     12
#define GPIO_ACTIVE_LOW     0

// Volume control
#define INITIAL_VOLUME 100
// #define VOLUME_STEP    10


// Static global variables for easier access in callbacks
audio_pipeline_components_t audio_pipeline_components = { 0 };
static int current_station = 0;
audio_board_handle_t board_handle = NULL;  // make this global during debugging
static audio_event_iface_handle_t evt = NULL;
static esp_periph_set_handle_t periph_set = NULL;

// Button Handles
// static button_handle_t volume_up_btn_handle = NULL;
// static button_handle_t volume_down_btn_handle = NULL;
static button_handle_t station_down_btn_handle = NULL;
static button_handle_t station_up_btn_handle = NULL;

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
    const char* uri;       // Stream URI
    codec_type_t codec;    // Codec type for the stream
} station_t;

station_t radio_stations[] = {
    {"KEXP Seattle", "https://kexp.streamguys1.com/kexp160.aac", CODEC_TYPE_AAC},
    {"KBUT Crested Butte", "http://26273.live.streamtheworld.com/KBUTFM.mp3", CODEC_TYPE_MP3},
    {"KSUT 4 Corners", "https://ksut.streamguys1.com/kute?uuid=mjgs8e5f8", CODEC_TYPE_AAC},
    {"KDUR Durango", "https://kdurradio.fortlewis.edu/stream", CODEC_TYPE_MP3},
    {"KOTO Telluride", "http://26193.live.streamtheworld.com/KOTOFM.mp3", CODEC_TYPE_MP3},
    // Add more stations here
};

int station_count = sizeof(radio_stations) / sizeof(radio_stations[0]);

static void station_up_button_cb(void* arg, void* usr_data) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Station Up Button Pressed");
    ESP_LOGI(TAG, "Destroying current pipeline...");
    destroy_audio_pipeline(&audio_pipeline_components);
    current_station = (current_station + 1) % station_count;
    ESP_LOGI(TAG, "Switching to station %d: %s", current_station, radio_stations[current_station].call_sign);


    ret = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new audio pipeline for station %s. Error: %d", radio_stations[current_station].call_sign, ret);
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
            ESP_LOGI(TAG, "Successfully switched to station: %s", radio_stations[current_station].call_sign);
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
    ESP_LOGI(TAG, "Switching to station %d: %s", current_station, radio_stations[current_station].call_sign);


    ret = create_audio_pipeline(&audio_pipeline_components,
        radio_stations[current_station].codec,
        radio_stations[current_station].uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new audio pipeline for station %s. Error: %d", radio_stations[current_station].call_sign, ret);
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
            ESP_LOGI(TAG, "Successfully switched to station: %s", radio_stations[current_station].call_sign);
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

void app_main(void)
{
    int temp_volume;
    // esp_log_level_set("*", ESP_LOG_DEBUG);
    // esp_log_level_set(TAG, ESP_LOG_DEBUG);
    // esp_log_level_set("AUDIO_PIPELINE_MGR", ESP_LOG_DEBUG);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif


    ESP_LOGI(TAG, "Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_set = esp_periph_set_init(&periph_cfg); // Assign to static global
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(periph_set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);


    ESP_LOGI(TAG, "Start audio codec chip");
    board_handle = audio_board_init(); // Assign to static global
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
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


    ESP_LOGI(TAG, "Starting initial stream: %s", radio_stations[current_station].call_sign);
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


    // ESP_LOGI(TAG, "Listening event from all elements of pipeline");
    // audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);


    ESP_LOGI(TAG, "Start audio_pipeline");
    audio_pipeline_run(audio_pipeline_components.pipeline);

    ESP_LOGI(TAG, "Initializing I2C LCD 16x2");
    // LCD_init(LCD_ADDR, SDA_PIN, SCL_PIN, LCD_COLS, LCD_ROWS);
    // ESP_LOGI(TAG, "LCD initialized, starting demo task");
    // xTaskCreate(&LCD_DemoTask, "Demo Task", 4 * 1024, NULL, 5, NULL);

    // Initialize the LCD using zorxx's lcd1602 library
    // #define ESP_I2C_PORT    -1 // Use default I2C port
#define ESP_I2C_SDA     GPIO_NUM_21
#define ESP_I2C_SCL     GPIO_NUM_22
#define ESP_I2C_ADDRESS LCD1602_I2C_ADDRESS_DEFAULT

    i2c_lowlevel_config config = { 0 };
    static i2c_master_bus_handle_t i2c_bus;
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1, // Use default I2C port
        .sda_io_num = ESP_I2C_SDA,
        .scl_io_num = ESP_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &i2c_bus) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C bus");
    }
    else
        config.bus = &i2c_bus;
    ctx = lcd1602_init(ESP_I2C_ADDRESS, true, &config);
    //    lcd1602_context *ctx = lcd1602_init(ESP_I2C_ADDRESS, true, &config);
    if (NULL != ctx)
    {
        // lcd1602_set_backlight(ctx, false);
        lcd1602_string(ctx, "foo Software");
        lcd1602_set_cursor(ctx, 1, 0);
        lcd1602_string(ctx, "LCD1602 Library");
        lcd1602_set_display(ctx, true, false, false);
        //   lcd1602_deinit(ctx);
    }
    xTaskCreate(&LCD_DemoTask, "Demo Task", 4 * 1024, NULL, 5, NULL);

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
