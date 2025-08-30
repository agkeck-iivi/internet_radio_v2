#ifndef MP3_SYNC_FILTER_H
#define MP3_SYNC_FILTER_H

#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      Creates an audio element handle to a filter that waits for
 *             the first MP3 sync word and discards all preceding data.
 *
 * @param      config  The configuration
 *
 * @return     The audio element handle
 */
audio_element_handle_t mp3_sync_filter_init(audio_element_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif // MP3_SYNC_FILTER_H
