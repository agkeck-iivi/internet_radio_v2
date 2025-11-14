/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "encoders.h"
 //  #include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"

#include "nvs_flash.h"
#include "audio_hal.h"
#include "esp_sleep.h"
#include <limits.h>

#include "screens.h"
#include "board.h"

static const char* TAG = "encoders";

// extern audio_board_handle_t board_handle;

extern int station_count;
extern int current_station;

#define VOLUME_GPIO_A 2
#define VOLUME_GPIO_B 42
#define VOLUME_PRESS_GPIO 1

#define STATION_GPIO_A 2
#define STATION_GPIO_B 42
#define STATION_PRESS_GPIO 1

// polling periods
#define VOLUME_POLLING_PERIOD_MS 100
#define VOLUME_PRESS_POLLING_PERIOD_MS 100

#define STATION_POLLING_PERIOD_MS 100
#define STATION_PRESS_POLLING_PERIOD_MS 100

typedef struct
{
    pcnt_unit_handle_t pcnt_unit;
    int speed;
    int value;
    int adjust;
    audio_board_handle_t board_handle;
} limited_pulse_counter_t;

typedef struct
{
    pcnt_unit_handle_t pcnt_unit;
    int current_index;
    const int* values;
    int num_values;
} cyclic_pulse_counter_t;

// Mute functionality state
static bool is_muted = false;
static int volume_before_mute = 0;
static limited_pulse_counter_t* g_volume_counter_ptr = NULL; // Pointer to the volume counter for shared access

static void save_volume_to_nvs(int volume)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing volume!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(nvs_handle, "volume", volume);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing 'volume' to NVS!", esp_err_to_name(err));
    }
    else {
        ESP_LOGD(TAG, "Saved volume = %d to NVS", volume);
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) committing 'volume' to NVS!", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}


// update the volume by clamping to range 0-100.  The adjust parameter is used to adapt the current
// pulse count to the clamped volume.  In this way one can adjust well below 0 but after ajustment it only
// take a click to get back above 0.
void update_volume_pulse_counter(void* pvParameters)
{
    limited_pulse_counter_t* counter = (limited_pulse_counter_t*)pvParameters;
    g_volume_counter_ptr = counter; // Store pointer for global access

    // Restore initial volume to PCNT counter state
    // The counter is at 0, so we need to adjust it to match the initial volume
    int initial_count_offset = (counter->value - counter->adjust) * 4 / counter->speed;
    pcnt_unit_clear_count(counter->pcnt_unit);
    // This is a bit of a hack to set the counter. There's no direct 'set_count'
    if (initial_count_offset != 0) {
        // We can't directly set the count, but we can adjust our 'adjust' value
        // so that the next calculation is correct.
        // The formula is: new_volume = count / 4 * speed + adjust
        // We want new_volume to be initial_volume when count is 0.
        counter->adjust = counter->value;
    }

    for (;;)
    {
        int count;
        ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &count));
        int new_volume = count / 4 * counter->speed + counter->adjust;
        if (new_volume < 0)
        {
            counter->adjust = -count / 4 * counter->speed;
            new_volume = 0;
        }
        else if (new_volume > 100)
        {
            counter->adjust = 100 - count / 4 * counter->speed;
            new_volume = 100;
        }
        if (new_volume != counter->value)
        {
            // If muted and user changes volume, unmute first
            if (is_muted) {
                is_muted = false;
                ESP_LOGI(TAG, "Unmuted by volume change");
            }

            counter->value = new_volume;
            ESP_LOGI(TAG, "Setting volume to: %d", new_volume);
            audio_hal_set_volume(counter->board_handle->audio_hal, new_volume);
            update_volume_slider(new_volume);
            save_volume_to_nvs(new_volume);
        }

        vTaskDelay(pdMS_TO_TICKS(VOLUME_POLLING_PERIOD_MS)); // Poll for volume changes
    }
}

static void volume_press_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Volume press button task started.");
    while (1) {
        if (gpio_get_level(VOLUME_PRESS_GPIO) == 0) { // Button is pressed (active low)
            // Debounce delay
            vTaskDelay(pdMS_TO_TICKS(50));
            // Wait for button release
            while (gpio_get_level(VOLUME_PRESS_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            is_muted = !is_muted; // Toggle mute state

            if (is_muted) {
                ESP_LOGI(TAG, "Muting volume");
                // Save current volume and mute
                if (g_volume_counter_ptr) {
                    volume_before_mute = g_volume_counter_ptr->value;
                }
                audio_hal_set_volume(g_volume_counter_ptr->board_handle->audio_hal, 0);
                update_volume_slider(0);
                // We don't save mute state to NVS, so it always starts unmuted
            }
            else {
                ESP_LOGI(TAG, "Unmuting volume to %d", volume_before_mute);
                // Restore volume
                if (g_volume_counter_ptr) {
                    // Sync counter state with the restored volume
                    g_volume_counter_ptr->value = volume_before_mute;
                    g_volume_counter_ptr->adjust = volume_before_mute;
                    pcnt_unit_clear_count(g_volume_counter_ptr->pcnt_unit);

                    audio_hal_set_volume(g_volume_counter_ptr->board_handle->audio_hal, volume_before_mute);
                    update_volume_slider(volume_before_mute);
                    save_volume_to_nvs(volume_before_mute);
                }
            }
            // Debounce after release
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(VOLUME_PRESS_POLLING_PERIOD_MS)); //
    }
}



void update_cyclic_value(void* pvParameters)
{
    cyclic_pulse_counter_t* counter = (cyclic_pulse_counter_t*)pvParameters;
    int last_step_count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &last_step_count));
    last_step_count /= 1; // Each detent is 2 counts

    for (;;)
    {
        const int fast_poll_ms = 20;           // 4 times per second
        const int slow_poll_ms = 1000;          // 1 time per second
        const int inactivity_timeout_ms = 1000; // dwell time (ms) before triggering action
        static int current_poll_ms = slow_poll_ms;
        static TickType_t last_change_time = 0;

        int raw_count;
        ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &raw_count));
        int current_step_count = raw_count / 4;

        if (current_step_count != last_step_count)
        {
            int step_delta = current_step_count - last_step_count;

            // Update index with wrapping
            int new_index = (counter->current_index + step_delta) % counter->num_values;
            if (new_index < 0)
            {
                new_index += counter->num_values;
            }
            counter->current_index = new_index;
            // counter->current_value = counter->values[counter->current_index];
            ESP_LOGI(TAG, "Cyclic index: %d", counter->current_index);
            update_station_roller(counter->current_index);
            // save_current_station_to_nvs(counter->current_index);
            last_step_count = current_step_count;

            // A change occurred, switch to fast polling and record the time
            current_poll_ms = fast_poll_ms;
            last_change_time = xTaskGetTickCount();
        }
        else if (current_poll_ms == fast_poll_ms && (xTaskGetTickCount() - last_change_time) > pdMS_TO_TICKS(inactivity_timeout_ms))
        {
            // Inactivity timeout reached, switch back to slow polling
            // perform change action here.
            ESP_LOGI(TAG, "Trigger the action");
            current_poll_ms = slow_poll_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(current_poll_ms));
    }
}

void init_encoders(audio_board_handle_t board_handle, int initial_volume)
{

    // ESP_LOGI(TAG, "set glitch filter");
    static pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };

    // //*****************  volume encoder **********************
    // gpio_config_t volume_encoder_gpio_config = {
    //     .pin_bit_mask = (1ULL << VOLUME_GPIO_A) | (1ULL << VOLUME_GPIO_B) | (1ULL << VOLUME_PRESS_GPIO),
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE,
    // };
    // ESP_ERROR_CHECK(gpio_config(&volume_encoder_gpio_config));


    // ESP_LOGI(TAG, "install volume pcnt unit");
    // static pcnt_unit_config_t volume_unit_config = {
    //     .high_limit = INT16_MAX, // these are defined in <limits.h>  16 bit counter has type int (32bit?), force int16 limits
    //     .low_limit = INT16_MIN,
    // };
    // static pcnt_unit_handle_t volume_pcnt_unit = NULL;
    // ESP_ERROR_CHECK(pcnt_new_unit(&volume_unit_config, &volume_pcnt_unit));

    // // limited_pulse_counter_t volume_counter;
    // ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(volume_pcnt_unit, &filter_config));

    // ESP_LOGI(TAG, "install pcnt channels");
    // static pcnt_chan_config_t chan_a_config = {
    //     .edge_gpio_num = VOLUME_GPIO_A,
    //     .level_gpio_num = VOLUME_GPIO_B,
    // };
    // static pcnt_channel_handle_t pcnt_chan_a = NULL;
    // ESP_ERROR_CHECK(pcnt_new_channel(volume_pcnt_unit, &chan_a_config, &pcnt_chan_a));
    // ESP_LOGI(TAG, "set edge and level actions for pcnt channels");

    // ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    // ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));


    // ESP_LOGI(TAG, "enable pcnt unit");
    // ESP_ERROR_CHECK(pcnt_unit_enable(volume_pcnt_unit));
    // ESP_LOGI(TAG, "clear pcnt unit");
    // ESP_ERROR_CHECK(pcnt_unit_clear_count(volume_pcnt_unit));
    // ESP_LOGI(TAG, "start pcnt unit");
    // ESP_ERROR_CHECK(pcnt_unit_start(volume_pcnt_unit));

    // // Use heap allocation for the counter so it can be shared
    // limited_pulse_counter_t* volume_counter = malloc(sizeof(limited_pulse_counter_t));
    // if (!volume_counter) {
    //     ESP_LOGE(TAG, "Failed to allocate memory for volume counter");
    //     return;
    // }
    // volume_counter->pcnt_unit = volume_pcnt_unit;
    // volume_counter->value = initial_volume;
    // volume_counter->adjust = initial_volume;
    // volume_counter->speed = 5; // number of units of volume per encoder step
    // volume_counter->board_handle = board_handle;
    // ESP_LOGI(TAG, "start volume update task");
    // xTaskCreate(update_volume_pulse_counter, "update_volume_pulse_counter", 4 * 1024, volume_counter, 5, NULL);

    // xTaskCreate(volume_press_task, "volume_press_task", 2048, NULL, 5, NULL);




    // *********************  station encoder  **********************
    static gpio_config_t station_encoder_gpio_config = {
    .pin_bit_mask = (1ULL << STATION_GPIO_A) | (1ULL << STATION_GPIO_B) | (1ULL << STATION_PRESS_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&station_encoder_gpio_config));

    ESP_LOGI(TAG, "install station pcnt unit");
    static pcnt_unit_config_t station_unit_config = {
        .high_limit = INT16_MAX, // these are defined in <limits.h>  16 bit counter has type int (32bit?), force int16 limits
        .low_limit = INT16_MIN,
    };
    static pcnt_unit_handle_t station_pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&station_unit_config, &station_pcnt_unit));

    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(station_pcnt_unit, &filter_config));

    static pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = STATION_GPIO_B,
        .level_gpio_num = STATION_GPIO_A,
    };
    static pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(station_pcnt_unit, &chan_b_config, &pcnt_chan_b));


    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // enable cyclic counter
    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(station_pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(station_pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(station_pcnt_unit));

    static cyclic_pulse_counter_t cyclic_counter;
    cyclic_counter.pcnt_unit = station_pcnt_unit;
    cyclic_counter.num_values = station_count;
    cyclic_counter.current_index = current_station;
    xTaskCreate(update_cyclic_value, "update_cyclic_value", 2048, &cyclic_counter, 5, NULL);

}
