#include <string.h>
#include "esp_log.h"
#include "audio_element.h"
#include "audio_common.h"
#include "mp3_sync_filter.h"

static const char *TAG = "MP3_SYNC_FILTER";

typedef struct {
    bool sync_found;
    int bytes_skipped;
} mp3_sync_filter_t;

static esp_err_t _mp3_sync_filter_destroy(audio_element_handle_t self) {
    mp3_sync_filter_t *filter = (mp3_sync_filter_t *)audio_element_getdata(self);
    audio_free(filter);
    return ESP_OK;
}

static esp_err_t _mp3_sync_filter_open(audio_element_handle_t self) {
    mp3_sync_filter_t *filter = (mp3_sync_filter_t *)audio_element_getdata(self);
    filter->sync_found = false;
    filter->bytes_skipped = 0;
    ESP_LOGI(TAG, "open, searching for first MP3 frame");
    return ESP_OK;
}

static esp_err_t _mp3_sync_filter_close(audio_element_handle_t self) {
    ESP_LOGI(TAG, "close");
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        mp3_sync_filter_t *filter = (mp3_sync_filter_t *)audio_element_getdata(self);
        filter->sync_found = false;
    }
    return ESP_OK;
}

static int _mp3_sync_filter_process(audio_element_handle_t self, char *in_buffer, int in_len) {
    mp3_sync_filter_t *filter = (mp3_sync_filter_t *)audio_element_getdata(self);
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;

    if (r_size <= 0) {
        return r_size; // Propagate EOF or error
    }

    if (filter->sync_found) {
        // Sync word already found, just pass data through
        w_size = audio_element_output(self, in_buffer, r_size);
        return w_size;
    }

    // Still searching for the first sync word.
    // A valid MP3 frame header starts with 11 bits of 1.
    for (int i = 0; i <= r_size - 4; i++) {
        if (((uint8_t)in_buffer[i] == 0xFF) && (((uint8_t)in_buffer[i + 1] & 0xE0) == 0xE0)) {
            // Basic validation to reduce false positives
            int mpeg_version = (in_buffer[i+1] >> 3) & 0x03;
            int layer = (in_buffer[i+1] >> 1) & 0x03;
            int bitrate_idx = (in_buffer[i+2] >> 4) & 0x0F;
            int sample_rate_idx = (in_buffer[i+2] >> 2) & 0x03;

            if (mpeg_version != 1 /* reserved */ &&
                layer != 0 /* reserved */ &&
                bitrate_idx != 15 /* forbidden */ &&
                sample_rate_idx != 3 /* reserved */)
            {
                filter->bytes_skipped += i;
                ESP_LOGI(TAG, "MP3 sync word found. Skipped a total of %d bytes.", filter->bytes_skipped);
                filter->sync_found = true;
                w_size = audio_element_output(self, &in_buffer[i], r_size - i);
                return w_size;
            }
        }
    }

    // No sync word found in this entire buffer, discard it and ask for more data.
    filter->bytes_skipped += r_size;
    ESP_LOGD(TAG, "No sync word in this block, discarding %d bytes", r_size);
    return r_size; // Consume the buffer by returning bytes read
}

audio_element_handle_t mp3_sync_filter_init(audio_element_cfg_t *cfg) {
    audio_element_handle_t el = audio_element_init(cfg);
    AUDIO_MEM_CHECK(TAG, el, return NULL);
    mp3_sync_filter_t *filter = audio_calloc(1, sizeof(mp3_sync_filter_t));
    AUDIO_MEM_CHECK(TAG, filter, { audio_element_deinit(el); return NULL; });
    
    audio_element_setdata(el, filter);
    audio_element_set_open_cb(el, _mp3_sync_filter_open);
    audio_element_set_close_cb(el, _mp3_sync_filter_close);
    audio_element_set_process_cb(el, _mp3_sync_filter_process);
    audio_element_set_destroy_cb(el, _mp3_sync_filter_destroy);
    audio_element_set_tag(el, "mp3_sync_filter");

    return el;
}
