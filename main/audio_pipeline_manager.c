#include "audio_pipeline_manager.h"
#include "esp_log.h"
#include "audio_common.h"
#include "http_stream.h"
#include "tcp_client_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "esp_http_client.h"
#include "ogg_decoder.h"
#include "board.h" // For CONFIG_ESP32_C3_LYRA_V2_BOARD and I2S_STREAM_PDM_TX_CFG_DEFAULT
#include "mp3_sync_filter.h"

extern audio_pipeline_components_t audio_pipeline_components;

static const char *TAG = "AUDIO_PIPELINE_MGR";

const char *codec_type_to_string(codec_type_t codec)
{
    switch (codec)
    {
    case CODEC_TYPE_MP3:
        return "MP3";
    case CODEC_TYPE_AAC:
        return "AAC";
    case CODEC_TYPE_OGG:
        return "OGG";
    default:
        return "Unknown Codec";
    }
}

static int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http_client = (esp_http_client_handle_t)msg->http_client;
    char *url = NULL;

    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        // This event is triggered before the HTTP request is sent.
        // We can use it to customize headers.
        esp_http_client_get_url(http_client, &url);
        ESP_LOGI(TAG, "HTTP_STREAM_PRE_REQUEST, URL=%s", url);

        // The KHEN stream requires a "Referer" header to work, as discovered during curl debugging.
        if (url && strstr(url, "stream.pacificaservice.org")) {
            ESP_LOGI(TAG, "Adding Referer header for KHEN stream");
            esp_http_client_set_header(http_client, "Referer", "https://www.radiorethink.com/");
        }
        free(url); // Important to free the URL string
    }

    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS)
    {
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK)
    {
        return http_stream_next_track(msg->el);
    }
    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST)
    {
        return http_stream_fetch_again(msg->el);
    }
    return ESP_OK;
}

static esp_err_t _icy_stream_event_handle(tcp_stream_event_msg_t *msg, tcp_stream_status_t state, void *ctx)
{
    const char *TAG_ICY = "ICY_STREAM_EVT";
    if (state == TCP_STREAM_STATE_CONNECTED) {
        ESP_LOGI(TAG_ICY, "ICY Stream connected, sending GET request");
        // Manually send a GET request. Icy-MetaData:1 asks the server to include song title metadata in the stream.
        const char *get_request = "GET /khen_128 HTTP/1.0\r\n"
                                  "Host: stream.pacificaservice.org:9000\r\n"
                                  "User-Agent: ESP-ADF/2.0\r\n"
                                  "Icy-MetaData: 1\r\n\r\n";
        if (esp_transport_write((esp_transport_handle_t)msg->sock_fd, get_request, strlen(get_request), 5000) < 0) {
            ESP_LOGE(TAG_ICY, "Failed to send GET request");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

#include "board.h" // remove after debugging
extern audio_board_handle_t board_handle; // remove after debugging

static esp_err_t codec_event_cb(audio_element_handle_t el, audio_event_iface_msg_t *msg, void *ctx){
    ESP_LOGI(TAG, "Codec event callback triggered for element: %s, command: %d", audio_element_get_tag(el), msg->cmd);
    if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg->source == (void *)el)
    {
        if (msg->cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO)
        {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(el, &music_info);
            ESP_LOGI(TAG, "[ * ] Callback: Receive music info from codec decoder, sample_rate=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
            i2s_stream_set_clk(audio_pipeline_components.i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
        }
    }
    return ESP_OK;
}
esp_err_t create_audio_pipeline(audio_pipeline_components_t *components, codec_type_t codec_type, const char *uri)
{
    
    if (components == NULL)
    {
        ESP_LOGE(TAG, "audio_pipeline_components_t pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (uri == NULL)
    {
        ESP_LOGE(TAG, "URI is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Creating audio pipeline for codec type: %s with URI: %s", codec_type_to_string(codec_type), uri);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    components->pipeline = audio_pipeline_init(&pipeline_cfg);
    if (components->pipeline == NULL) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        return ESP_FAIL;
    }

    audio_element_handle_t source_stream = NULL;
    audio_element_handle_t filter_stream = NULL;

    // Detect if we need special handling for an ICY stream
    if (strstr(uri, "stream.pacificaservice.org")) {
        ESP_LOGI(TAG, "ICY Stream detected, using TCP stream and sync filter for %s", uri);
        tcp_stream_cfg_t tcp_cfg = TCP_STREAM_CFG_DEFAULT();
        tcp_cfg.host = "stream.pacificaservice.org";
        tcp_cfg.port = 9000;
        tcp_cfg.event_handler = _icy_stream_event_handle;
        tcp_cfg.type = AUDIO_STREAM_READER;
        source_stream = tcp_stream_init(&tcp_cfg);

        audio_element_cfg_t filter_cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
        filter_stream = mp3_sync_filter_init(&filter_cfg);
    } else {
        // Standard HTTP stream handling for all other stations
        http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
        http_cfg.event_handle = _http_stream_event_handle;
        http_cfg.type = AUDIO_STREAM_READER;
        http_cfg.enable_playlist_parser = true;
        source_stream = http_stream_init(&http_cfg);
    }

    if (source_stream == NULL) {
        ESP_LOGE(TAG, "Failed to initialize source stream");
        ret = ESP_FAIL;
        goto cleanup;
    }
    components->http_stream_reader = source_stream; // Store source handle

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

    switch (codec_type) {
        case CODEC_TYPE_AAC:
            ESP_LOGD(TAG, "Creating AAC decoder");
            aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
            components->codec_decoder = aac_decoder_init(&aac_cfg);
            break;
        case CODEC_TYPE_MP3:
            ESP_LOGD(TAG, "Creating MP3 decoder");
            mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
            components->codec_decoder = mp3_decoder_init(&mp3_cfg);
            break;
        case CODEC_TYPE_OGG:
            ESP_LOGD(TAG, "Creating OGG decoder");
            ogg_decoder_cfg_t ogg_cfg = DEFAULT_OGG_DECODER_CONFIG();
            components->codec_decoder = ogg_decoder_init(&ogg_cfg);
            break;
        default:
            ESP_LOGE(TAG, "Unsupported codec type: %d", codec_type);
            ret = ESP_ERR_INVALID_ARG;
            goto cleanup;
    }
    if (components->codec_decoder == NULL) {
        ESP_LOGE(TAG, "Failed to initialize %s decoder", codec_type_to_string(codec_type));
        ret = ESP_FAIL;
        goto cleanup;
    }
    audio_element_set_event_callback(components->codec_decoder, codec_event_cb, NULL);

    // Register elements
    audio_pipeline_register(components->pipeline, source_stream, "source");
    if (filter_stream) {
        audio_pipeline_register(components->pipeline, filter_stream, "filter");
    }
    audio_pipeline_register(components->pipeline, components->codec_decoder, "codec");
    audio_pipeline_register(components->pipeline, components->i2s_stream_writer, "i2s");

    // Link elements
    if (filter_stream) {
        ESP_LOGI(TAG, "Linking [source] -> [filter] -> [codec] -> [i2s]");
        const char *link_tag[4] = {"source", "filter", "codec", "i2s"};
        ret = audio_pipeline_link(components->pipeline, &link_tag[0], 4);
    } else {
        ESP_LOGI(TAG, "Linking [source] -> [codec] -> [i2s]");
        const char *link_tag[3] = {"source", "codec", "i2s"};
        ret = audio_pipeline_link(components->pipeline, &link_tag[0], 3);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register audio elements to pipeline");
        goto cleanup;
    }

    // For standard HTTP streams, we still need to set the URI
    if (filter_stream == NULL) {
        if (audio_element_set_uri(components->http_stream_reader, uri) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set URI for http_stream_reader");
            ret = ESP_FAIL;
            goto cleanup;
        }
    }

    ESP_LOGI(TAG, "Audio pipeline with %s codec created successfully", codec_type_to_string(codec_type));
    return ESP_OK;

cleanup:
    ESP_LOGE(TAG, "Cleaning up due to error during pipeline creation");
    // The destroy_audio_pipeline function handles deinitialization of all components
    // that might have been created. We can call it to be safe.
    // To avoid double-free, we manually deinit the filter if it was created.
    if (filter_stream) {
        audio_element_deinit(filter_stream);
    }
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
        audio_pipeline_deinit(components->pipeline);
        components->pipeline = NULL;
    }
    return ret;
}

esp_err_t destroy_audio_pipeline(audio_pipeline_components_t *components)
{
    if (components == NULL)
    {
        ESP_LOGE(TAG, "audio_pipeline_components_t pointer is NULL for destroy");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Destroying audio pipeline");

    if (components->pipeline)
    {
        audio_pipeline_stop(components->pipeline);
        audio_pipeline_wait_for_stop(components->pipeline);
        audio_pipeline_terminate(components->pipeline);
        audio_pipeline_deinit(components->pipeline);  // deinits all elements
        components->pipeline = NULL;
    }
    components->http_stream_reader = NULL;
    components->codec_decoder = NULL;
    components->i2s_stream_writer = NULL;

    ESP_LOGI(TAG, "Audio pipeline destroyed successfully");
    return ESP_OK;
}