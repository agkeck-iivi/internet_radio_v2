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

#define VOLUME_GPIO_A 2
#define VOLUME_GPIO_B 42
#define VOLUME_PRESS_GPIO 1

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
void update_limited_pulse_counter(void* pvParameters)
{
    limited_pulse_counter_t* counter = (limited_pulse_counter_t*)pvParameters;
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
            counter->value = new_volume;
            ESP_LOGI(TAG, "Setting volume to: %d", new_volume);
            audio_hal_set_volume(counter->board_handle->audio_hal, new_volume);
            update_volume_slider(new_volume);
            save_volume_to_nvs(new_volume);
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // Poll for volume changes
    }
}

void update_cyclic_value(void* pvParameters)
{
    cyclic_pulse_counter_t* counter = (cyclic_pulse_counter_t*)pvParameters;
    int last_step_count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &last_step_count));
    last_step_count /= 4; // Each detent is 4 counts

    for (;;)
    {
        const int fast_poll_ms = 250;           // 4 times per second
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

void init_encoders(audio_board_handle_t board_handle)
{
    // Configure GPIOs for the rotary encoder with pull-up resistors
    gpio_config_t encoder_gpio_config = {
        .pin_bit_mask = (1ULL << VOLUME_GPIO_A) | (1ULL << VOLUME_GPIO_B) | (1ULL << VOLUME_PRESS_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&encoder_gpio_config));

    ESP_LOGI(TAG, "install pcnt unit");
    pcnt_unit_config_t volume_unit_config = {
        .high_limit = INT16_MAX, // these are defined in <limits.h>  16 bit counter has type int (32bit?), force int16 limits
        .low_limit = INT16_MIN,
    };
    pcnt_unit_handle_t volume_pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&volume_unit_config, &volume_pcnt_unit));

    // limited_pulse_counter_t volume_counter;
    ESP_LOGI(TAG, "set glitch filter");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(volume_pcnt_unit, &filter_config));

    ESP_LOGI(TAG, "install pcnt channels");
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = VOLUME_GPIO_A,
        .level_gpio_num = VOLUME_GPIO_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(volume_pcnt_unit, &chan_a_config, &pcnt_chan_a));


    // pcnt_chan_config_t chan_b_config = {
    //     .edge_gpio_num = VOLUME_GPIO_B,
    //     .level_gpio_num = VOLUME_GPIO_A,
    // };
    // pcnt_channel_handle_t pcnt_chan_b = NULL;
    // ESP_ERROR_CHECK(pcnt_new_channel(volume_pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_LOGI(TAG, "set edge and level actions for pcnt channels");
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    // ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(volume_pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(volume_pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(volume_pcnt_unit));

    // // enable volume pulse counter
    limited_pulse_counter_t volume_counter;
    volume_counter.pcnt_unit = volume_pcnt_unit;
    volume_counter.value = 70; // this will be read from NVS in real application
    volume_counter.adjust = 70;
    volume_counter.speed = 2; // number of units of volume per encoder step
    volume_counter.board_handle = board_handle;
    ESP_LOGI(TAG, "start volume update task");
    xTaskCreate(update_limited_pulse_counter, "update_limited_pulse_counter", 4*1024, &volume_counter, 5, NULL);

    // enable cyclic counter

    // cyclic_pulse_counter_t cyclic_counter;
    // cyclic_counter.pcnt_unit = volume_pcnt_unit;
    // cyclic_counter.num_values = 10;
    // cyclic_counter.current_index = 0; // this will be read from NVS in real appliation
    // xTaskCreate(update_cyclic_value, "update_cyclic_value", 2048, &cyclic_counter, 5, NULL);

#if CONFIG_EXAMPLE_WAKE_UP_LIGHT_SLEEP
    // EC11 channel output high level in normal state, so we set "low level" to wake up the chip
    ESP_ERROR_CHECK(gpio_wakeup_enable(VOLUME_GPIO_A, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(esp_light_sleep_start());
#endif

    // Report counter value
    int pulse_count = 0;
    int event_count = 0;
    // while (1)
    // {
    //     if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(1000)))
    //     {
    //         // ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
    //     }
    //     else
    //     {
    //         ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
    //         ESP_LOGI(TAG, "Pulse count: %d", pulse_count);
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
    while (1)
    {
        vTaskDelay(portMAX_DELAY); // Main task has nothing to do, so it can block indefinitely
        // if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(1000)))
        // {
        //     ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
        // }
        // else
        // {
        //     ESP_ERROR_CHECK(pcnt_unit_get_count(volume_counter.pcnt_unit, &pulse_count));
        //     ESP_LOGI(TAG, "Pulse count: %d", pulse_count);
        //     ESP_LOGI(TAG, "volume: %d", volume_counter.value);
        // }
    }
}
