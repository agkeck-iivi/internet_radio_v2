/* Play internet radio stations

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
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "ogg_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#include "audio_idf_version.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

static const char *TAG = "INTERNET_RADIO";

typedef enum {
    CODEC_TYPE_MP3,
    CODEC_TYPE_AAC,
    CODEC_TYPE_OGG
} codec_type_t;

typedef struct {
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader;
    audio_element_handle_t codec_decoder; // This can hold any decoder (aac, mp3, ogg, etc.)
    audio_element_handle_t i2s_stream_writer;
} audio_pipeline_components_t;


// reference audo streams: https://www.radiomast.io/reference-streams
// #define AAC_STREAM_URI "http://open.ls.qingting.fm/live/274/64k.m3u8?format=aac"
// #define AAC_STREAM_URI "https://streams.radiomast.io/ref-128k-aaclc-stereo"

// These two work for kexp
// #define AAC_STREAM_URI "https://kexp.streamguys1.com/kexp64.aac"
#define AAC_STREAM_URI "https://kexp.streamguys1.com/kexp160.aac"

// kbut this didn't work, This brings up a player page
// #define AAC_STREAM_URI  "http://www.radiorethink.com/tuner/?stationCode=kbut&stream=hi"
// I think that the following is defunct
// KBUT: https://playerservices.streamtheworld.com/api/livestream-redirect/KBUTFM.mp3

// this looks live: http://26273.live.streamtheworld.com/KBUTFM.mp3
// open developers tab in browser on player page.  play audio and go to the network tab in the developer's tab to find 
// network address for streaming audio.  it looks like there is an initial GET that returns a redirect to the 26273... server
// subsequent pause/resume actions get different redirects.

// here is a list of servers for kbut: http://playerservices.streamtheworld.com/pls/KBUTFM.pls 


// ksut: the following seems to work without the uuid as well
// ksut: https://ksut.streamguys1.com/kute?uuid=yflwpj1y

int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS) {
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK) {
        return http_stream_next_track(msg->el);
    }
    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST) {
        return http_stream_fetch_again(msg->el);
    }
    return ESP_OK;
}

esp_err_t create_audio_pipeline(audio_pipeline_components_t *components, codec_type_t codec_type, const char *uri) {
    if (components == NULL) {
        ESP_LOGE(TAG, "audio_pipeline_components_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (uri == NULL) {
        ESP_LOGE(TAG, "URI is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize all handles to NULL for robust cleanup
    components->pipeline = NULL;
    components->http_stream_reader = NULL;
    components->codec_decoder = NULL;
    components->i2s_stream_writer = NULL;

    esp_err_t ret = ESP_OK;
    const char *codec_tag_name = NULL;

    ESP_LOGI(TAG, "Creating audio pipeline for codec type: %d with URI: %s", codec_type, uri);

    // 1. Create audio pipeline
    ESP_LOGD(TAG, "[Pipeline Setup] Initializing audio pipeline");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    components->pipeline = audio_pipeline_init(&pipeline_cfg);
    if (components->pipeline == NULL) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        ret = ESP_FAIL; // Or a more specific ESP-ADF error if available
        goto cleanup;
    }

    // 2. Create HTTP stream reader
    ESP_LOGD(TAG, "[Pipeline Setup] Initializing HTTP stream reader");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    components->http_stream_reader = http_stream_init(&http_cfg);
    if (components->http_stream_reader == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP stream reader");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // 3. Create I2S stream writer
    ESP_LOGD(TAG, "[Pipeline Setup] Initializing I2S stream writer");
#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    components->i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (components->i2s_stream_writer == NULL) {
        ESP_LOGE(TAG, "Failed to initialize I2S stream writer");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // 4. Create Codec Decoder based on codec_type
    ESP_LOGD(TAG, "[Pipeline Setup] Initializing codec decoder");
    switch (codec_type) {
        case CODEC_TYPE_AAC:
            ESP_LOGI(TAG, "Creating AAC decoder");
            aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
            components->codec_decoder = aac_decoder_init(&aac_cfg);
            codec_tag_name = "aac";
            break;
        case CODEC_TYPE_MP3:
            ESP_LOGW(TAG, "MP3 decoder not yet fully implemented in this function.");
            // To implement MP3:
            // 1. Add #include "mp3_decoder.h"
            // 2. Ensure ESP-ADF is configured to include MP3 decoder component.
            // 3. Uncomment and adapt the following:
            mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
            components->codec_decoder = mp3_decoder_init(&mp3_cfg);
            codec_tag_name = "mp3";
            ret = ESP_ERR_NOT_SUPPORTED; // Mark as not supported for now
            goto cleanup;
        case CODEC_TYPE_OGG:
            ESP_LOGW(TAG, "OGG decoder not yet fully implemented in this function.");
            // To implement OGG:
            // 1. Add #include "ogg_decoder.h"
            // 2. Ensure ESP-ADF is configured to include OGG decoder component.
            // 3. Uncomment and adapt the following:
            ogg_decoder_cfg_t ogg_cfg = DEFAULT_OGG_DECODER_CONFIG();
            components->codec_decoder = ogg_decoder_init(&ogg_cfg);
            codec_tag_name = "ogg";
            ret = ESP_ERR_NOT_SUPPORTED; // Mark as not supported for now
            goto cleanup;
        default:
            ESP_LOGE(TAG, "Unsupported codec type: %d", codec_type);
            ret = ESP_ERR_INVALID_ARG;
            goto cleanup;
    }

    if (components->codec_decoder == NULL) {
        ESP_LOGE(TAG, "Failed to initialize %s decoder", codec_tag_name ? codec_tag_name : "selected");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // 5. Register elements to pipeline
    ESP_LOGD(TAG, "[Pipeline Setup] Registering elements to pipeline");
    if (audio_pipeline_register(components->pipeline, components->http_stream_reader, "http") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register http_stream_reader");
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (audio_pipeline_register(components->pipeline, components->codec_decoder, codec_tag_name) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s_decoder", codec_tag_name);
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (audio_pipeline_register(components->pipeline, components->i2s_stream_writer, "i2s") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register i2s_stream_writer");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // 6. Link elements
    ESP_LOGI(TAG, "Linking pipeline elements: http->%s->i2s", codec_tag_name);
    const char *link_tag[3] = {"http", codec_tag_name, "i2s"};
    if (audio_pipeline_link(components->pipeline, &link_tag[0], 3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to link pipeline elements");
        ret = ESP_FAIL;
        goto cleanup;
    }

    // 7. Set the URI for the HTTP stream reader
    ESP_LOGD(TAG, "[Pipeline Setup] Setting URI: %s", uri);
    if (audio_element_set_uri(components->http_stream_reader, uri) != ESP_OK) {
         ESP_LOGE(TAG, "Failed to set URI for http_stream_reader");
         ret = ESP_FAIL; // audio_element_set_uri itself returns esp_err_t, but often just stores the string. Check its specific return value if needed.
         goto cleanup;
    }


    ESP_LOGI(TAG, "Audio pipeline with %s codec created successfully", codec_tag_name);
    return ESP_OK;

cleanup:
    ESP_LOGE(TAG, "Cleaning up audio pipeline components due to error during creation");
    // Deinitialize elements first, then the pipeline
    if (components->http_stream_reader) {
        audio_element_deinit(components->http_stream_reader);
        components->http_stream_reader = NULL;
    }
    if (components->codec_decoder) {
        audio_element_deinit(components->codec_decoder);
        components->codec_decoder = NULL;
    }
    if (components->i2s_stream_writer) {
        audio_element_deinit(components->i2s_stream_writer);
        components->i2s_stream_writer = NULL;
    }
    if (components->pipeline) {
        // audio_pipeline_deinit handles internal cleanup including unregistering elements
        audio_pipeline_deinit(components->pipeline);
        components->pipeline = NULL;
    }
    return ret;
}


void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
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


    // audio_pipeline_handle_t pipeline;
    // audio_element_handle_t http_stream_reader, i2s_stream_writer, aac_decoder;

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);


    audio_pipeline_components_t audio_components;
    err = create_audio_pipeline(&audio_components, CODEC_TYPE_AAC, AAC_STREAM_URI);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create audio pipeline, error: %d", err);
        // Handle error appropriately, e.g., by restarting or halting.
        return; // Exit app_main on failure
    }


    
//     ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
//     audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
//     pipeline = audio_pipeline_init(&pipeline_cfg);

//     ESP_LOGI(TAG, "[2.1] Create http stream to read data");
//     http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
//     http_cfg.event_handle = _http_stream_event_handle;
//     http_cfg.type = AUDIO_STREAM_READER;
//     http_cfg.enable_playlist_parser = true;
//     http_stream_reader = http_stream_init(&http_cfg);

//     ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
// #if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
// #else
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
// #endif
//     i2s_cfg.type = AUDIO_STREAM_WRITER;
//     i2s_stream_writer = i2s_stream_init(&i2s_cfg);

//     ESP_LOGI(TAG, "[2.3] Create aac decoder to decode aac file");
//     aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
//     aac_decoder = aac_decoder_init(&aac_cfg);

//     ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
//     audio_pipeline_register(pipeline, http_stream_reader, "http");
//     audio_pipeline_register(pipeline, aac_decoder,        "aac");
//     audio_pipeline_register(pipeline, i2s_stream_writer,  "i2s");

//     ESP_LOGI(TAG, "[2.5] Link it together http_stream-->aac_decoder-->i2s_stream-->[codec_chip]");
//     const char *link_tag[3] = {"http", "aac", "i2s"};
//     audio_pipeline_link(pipeline, &link_tag[0], 3);

//     ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, aac as aac decoder, and default output is i2s)");
//     audio_element_set_uri(http_stream_reader, AAC_STREAM_URI);

    // ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    // esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    // esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    // periph_wifi_cfg_t wifi_cfg = {
    //     .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
    //     .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    // };
    // esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    // esp_periph_start(set, wifi_handle);
    // periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    // audio_pipeline_set_listener(pipeline, evt);
    audio_pipeline_set_listener(audio_components.pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    // audio_pipeline_run(pipeline);
    audio_pipeline_run(audio_components.pipeline);

    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            // && msg.source == (void *) aac_decoder
            && msg.source == (void *) audio_components.codec_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            // audio_element_getinfo(aac_decoder, &music_info);
            audio_element_getinfo(audio_components.codec_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from aac decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            // i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            i2s_stream_set_clk(audio_components.i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* restart stream when the first pipeline element (http_stream_reader in this case) receives stop event (caused by reading errors) */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) audio_components.http_stream_reader
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_ERROR_OPEN) {
            ESP_LOGW(TAG, "[ * ] Restart stream");
            audio_pipeline_stop(audio_components.pipeline);
            audio_pipeline_wait_for_stop(audio_components.pipeline);
            audio_element_reset_state(audio_components.codec_decoder);
            audio_element_reset_state(audio_components.i2s_stream_writer);
            audio_pipeline_reset_ringbuffer(audio_components.pipeline);
            audio_pipeline_reset_items_state(audio_components.pipeline);
            audio_pipeline_run(audio_components.pipeline);
            continue;
        }
    }

    // ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    // audio_pipeline_stop(pipeline);
    // audio_pipeline_wait_for_stop(pipeline);
    // audio_pipeline_terminate(pipeline);

    // audio_pipeline_unregister(pipeline, http_stream_reader);
    // audio_pipeline_unregister(pipeline, i2s_stream_writer);
    // audio_pipeline_unregister(pipeline, aac_decoder);

    // /* Terminate the pipeline before removing the listener */
    // audio_pipeline_remove_listener(pipeline);

    // /* Stop all peripherals before removing the listener */
    // esp_periph_set_stop_all(set);
    // audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    // /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    // audio_event_iface_destroy(evt);

    // /* Release all resources */
    // audio_pipeline_deinit(pipeline);
    // audio_element_deinit(http_stream_reader);
    // audio_element_deinit(i2s_stream_writer);
    // audio_element_deinit(aac_decoder);
    // esp_periph_set_destroy(set);
}
