#include "audio_pipeline_manager.h"
#include "esp_log.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "flac_decoder.h"
#include "ogg_decoder.h"
#include "board.h" // For CONFIG_ESP32_C3_LYRA_V2_BOARD and I2S_STREAM_PDM_TX_CFG_DEFAULT

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
    case CODEC_TYPE_FLAC:
        return "FLAC";
    default:
        return "Unknown Codec";
    }
}

static int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
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
 // board works here   
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Creating audio pipeline for codec type: %s with URI: %s", codec_type_to_string(codec_type), uri);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    components->pipeline = audio_pipeline_init(&pipeline_cfg);
    if (components->pipeline == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline");
        ret = ESP_FAIL;
        goto cleanup;
    }

    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.event_handle = _http_stream_event_handle;
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = true;
    components->http_stream_reader = http_stream_init(&http_cfg);
    if (components->http_stream_reader == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP stream reader");
        ret = ESP_FAIL;
        goto cleanup;
    }
    // // custom reader to skip junk data before mp3 frames in shoutcast streams
    // // this should be switchable.
    // audio_element_set_read_cb(components->http_stream_reader, custom_read, NULL);

#if defined CONFIG_ESP32_C3_LYRA_V2_BOARD
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_PDM_TX_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    components->i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    if (components->i2s_stream_writer == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize I2S stream writer");
        ret = ESP_FAIL;
        goto cleanup;
    }


    switch (codec_type)
    {
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
    case CODEC_TYPE_FLAC:
        ESP_LOGD(TAG, "Creating FLAC decoder");
        flac_decoder_cfg_t flac_cfg = DEFAULT_FLAC_DECODER_CONFIG();
        components->codec_decoder = flac_decoder_init(&flac_cfg);
        break;
    default:
        ESP_LOGE(TAG, "Unsupported codec type: %d", codec_type);
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    if (components->codec_decoder == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize %s decoder", codec_type_to_string(codec_type));
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (audio_pipeline_register(components->pipeline, components->http_stream_reader, "http") != ESP_OK ||
        audio_pipeline_register(components->pipeline, components->codec_decoder, "codec") != ESP_OK ||
        audio_pipeline_register(components->pipeline, components->i2s_stream_writer, "i2s") != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register audio elements to pipeline");
        ret = ESP_FAIL;
        goto cleanup;
    }

    const char *link_tag[3] = {"http", "codec", "i2s"};
    if (audio_pipeline_link(components->pipeline, &link_tag[0], 3) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to link pipeline elements: http->%s->i2s", codec_type_to_string(codec_type));
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (audio_element_set_uri(components->http_stream_reader, uri) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set URI for http_stream_reader");
        ret = ESP_FAIL;
        goto cleanup;
    }

    ESP_LOGI(TAG, "Audio pipeline with %s codec created successfully", codec_type_to_string(codec_type));
    return ESP_OK;

cleanup:
    ESP_LOGE(TAG, "Cleaning up audio pipeline components due to error during creation");
    if (components->http_stream_reader)
    {
        audio_element_deinit(components->http_stream_reader);
        components->http_stream_reader = NULL;
    }
    if (components->codec_decoder)
    {
        audio_element_deinit(components->codec_decoder);
        components->codec_decoder = NULL;
    }
    if (components->i2s_stream_writer)
    {
        audio_element_deinit(components->i2s_stream_writer);
        components->i2s_stream_writer = NULL;
    }
    if (components->pipeline)
    {
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