#ifndef AUDIO_PIPELINE_MANAGER_H
#define AUDIO_PIPELINE_MANAGER_H

#include "esp_err.h"
#include "audio_pipeline.h"
#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enumeration for supported audio codec types.
 */
typedef enum {
    CODEC_TYPE_MP3,
    CODEC_TYPE_AAC,
    CODEC_TYPE_OGG,
    CODEC_TYPE_FLAC
} codec_type_t;

/**
 * @brief Structure to hold handles for various audio pipeline components.
 */
typedef struct {
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader;
    audio_element_handle_t codec_decoder;
    audio_element_handle_t i2s_stream_writer;
} audio_pipeline_components_t;

/**
 * @brief Converts a codec_type_t enum to its string representation.
 */
const char *codec_type_to_string(codec_type_t codec);

/**
 * @brief Creates and configures an audio pipeline with the specified codec and URI.
 */
esp_err_t create_audio_pipeline(audio_pipeline_components_t *components, codec_type_t codec_type, const char *uri);

/**
 * @brief Stops, terminates, and deinitializes the audio pipeline and its components.
 */
esp_err_t destroy_audio_pipeline(audio_pipeline_components_t *components);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PIPELINE_MANAGER_H