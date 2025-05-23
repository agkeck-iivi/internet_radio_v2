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
// #include "audio_element.h"
// #include "audio_pipeline.h"
#include "audio_pipeline_manager.h" // Include the new header
#include "audio_event_iface.h"
#include "audio_common.h"
// #include "http_stream.h"
#include "i2s_stream.h"
// #include "aac_decoder.h"
// #include "mp3_decoder.h"
// #include "ogg_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#include "audio_idf_version.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "audio_pipeline_manager.h" // Include the new header

static const char *TAG = "INTERNET_RADIO";

/**
 * @brief Structure to define a radio station's properties.
 */
typedef struct
{
    const char *call_sign; // Station's call sign or name
    const char *uri;       // Stream URI
    codec_type_t codec;    // Codec type for the stream
} station_t;

station_t radio_stations[] = {
    {"KEXP Seattle", "https://kexp.streamguys1.com/kexp160.aac", CODEC_TYPE_AAC},
    {"KBUT FM", "http://26273.live.streamtheworld.com/KBUTFM.mp3", CODEC_TYPE_MP3},
    // Add more stations here
};

int station_count = sizeof(radio_stations) / sizeof(radio_stations[0]);

// reference audo streams: https://www.radiomast.io/reference-streams
// #define STREAM_URI "http://open.ls.qingting.fm/live/274/64k.m3u8?format=aac"
// #define STREAM_URI "https://streams.radiomast.io/ref-128k-aaclc-stereo"

// These two work for kexp
// #define STREAM_URI "https://kexp.streamguys1.com/kexp64.aac"

// #define STREAM_URI "https://kexp.streamguys1.com/kexp160.aac"
// #define CODEC CODEC_TYPE_AAC

// kbut this didn't work, This brings up a player page
// #define STREAM_URI  "http://www.radiorethink.com/tuner/?stationCode=kbut&stream=hi"
// I think that the following is defunct
// KBUT: https://playerservices.streamtheworld.com/api/livestream-redirect/KBUTFM.mp3

// this looks live: http://26273.live.streamtheworld.com/KBUTFM.mp3
// open developers tab in browser on player page.  play audio and go to the network tab in the developer's tab to find
// network address for streaming audio.  it looks like there is an initial GET that returns a redirect to the 26273... server
// subsequent pause/resume actions get different redirects.

#define STREAM_URI "http://26273.live.streamtheworld.com/KBUTFM.mp3"
#define CODEC CODEC_TYPE_MP3

// here is a list of servers for kbut: http://playerservices.streamtheworld.com/pls/KBUTFM.pls

// ksut: the following seems to work without the uuid as well
// ksut: https://ksut.streamguys1.com/kute?uuid=yflwpj1y

// OGG
// #define STREAM_URI "https://streams.radiomast.io/ref-64k-ogg-vorbis-stereo"
// #define CODEC CODEC_TYPE_OGG

void app_main(void)
{
    // only one pipeline structure, it is reused
    audio_pipeline_components_t audio_pipeline_components = {0};
    int current_station = 0;

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
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    // err = create_audio_pipeline(&audio_pipeline_components, CODEC, STREAM_URI);
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to create audio pipeline, error: %d", err);
    //     // Handle error appropriately, e.g., by restarting or halting.
    //     return; // Exit app_main on failure
    // }

    ESP_LOGI(TAG, "Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    // ESP_LOGI(TAG, "Listening event from all elements of pipeline");
    // audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);

    ESP_LOGI(TAG, "Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    // ESP_LOGI(TAG, "Start audio_pipeline");
    // audio_pipeline_run(audio_pipeline_components.pipeline);

    ESP_LOGI(TAG, "starting stream: %s", radio_stations[current_station].call_sign);
    err = create_audio_pipeline(&audio_pipeline_components, radio_stations[current_station].codec, radio_stations[current_station].uri);
    current_station = (current_station + 1) % station_count;
    ESP_LOGI(TAG, "Listening event from all elements of pipeline");
    audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create audio pipeline, error: %d", err);
        // Handle error appropriately, e.g., by restarting or halting.
        return; // Exit app_main on failure
    }

    ESP_LOGI(TAG, "Start audio_pipeline");
    audio_pipeline_run(audio_pipeline_components.pipeline);

    while (1)
    {

        // ESP_LOGI(TAG, "starting stream: %s", radio_stations[current_station].call_sign);
        // err = create_audio_pipeline(&audio_pipeline_components, radio_stations[current_station].codec, radio_stations[current_station].uri);
        // current_station = (current_station + 1) % station_count;
        // ESP_LOGI(TAG, "Listening event from all elements of pipeline");
        // audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);
        // if (err != ESP_OK)
        // {
        //     ESP_LOGE(TAG, "Failed to create audio pipeline, error: %d", err);
        //     // Handle error appropriately, e.g., by restarting or halting.
        //     return; // Exit app_main on failure
        // }

        // ESP_LOGI(TAG, "Start audio_pipeline");
        // audio_pipeline_run(audio_pipeline_components.pipeline);
        // vTaskDelay(pdMS_TO_TICKS(1000));

        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)audio_pipeline_components.codec_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
        {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(audio_pipeline_components.codec_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from codec decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            i2s_stream_set_clk(audio_pipeline_components.i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* restart stream when the first pipeline element (http_stream_reader in this case) receives stop event (caused by reading errors) */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *)audio_pipeline_components.http_stream_reader && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int)msg.data == AEL_STATUS_ERROR_OPEN)
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

    ESP_LOGI(TAG, "Stop audio_pipeline");
    // Call the new destroy function
    destroy_audio_pipeline(&audio_pipeline_components);

    /* The audio_pipeline_terminate within destroy_audio_pipeline handles removing
     * the listener that was set by audio_pipeline_set_listener.
     * If other listeners were added elsewhere, they would need separate removal.
     * For the main pipeline listener, this explicit call is now redundant.
     * audio_pipeline_remove_listener(audio_pipeline_components.pipeline); // Redundant
     */

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);
    esp_periph_set_destroy(set);
}
