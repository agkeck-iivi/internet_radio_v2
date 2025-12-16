/* Play internet radio stations

    based on esp-adf code: pipeline_living_stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "audio_common.h"
#include "audio_event_iface.h"
#include "audio_pipeline_manager.h"
#include "board.h"
#include "driver/gpio.h"
#include "encoders.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_peripherals.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ir_rmt.h"
#include "lvgl_ssd1306_setup.h"
#include "nvs_flash.h"
#include "screens.h"
#include "sdkconfig.h"
#include "station_data.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include <string.h>

static const char *TAG = "INTERNET_RADIO";

// Volume control
#define INITIAL_VOLUME 0

#define IR_TX_GPIO_NUM 20
#define BOSE_ON_BUTTON_GPIO 47
#define BOSE_OFF_BUTTON_GPIO 21
#define BUTTON_POLLING_PERIOD_MS 100

#define BITRATE_UPDATE_INTERVAL_MS 1000
#define TYPICAL_STARTUP_DURATION_US                                            \
  (15 * 1000 * 1000) // 15 seconds in microseconds, typical startup time beyond
                     // which failure to connect suggests a need to reboot

// oled screen with lvgl
extern int station_count; // from station_data.c
static lv_display_t *display;

// global variables for easier access in callbacks
audio_pipeline_components_t audio_pipeline_components = {0};
int current_station = 0;

static audio_board_handle_t board_handle =
    NULL; // make this global during debugging
static audio_event_iface_handle_t evt = NULL;
static esp_periph_set_handle_t periph_set = NULL;
rmt_channel_handle_t g_ir_tx_channel = NULL;

volatile int g_bitrate_kbps = 0;
// Button Handles
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void save_current_station_to_nvs(int station_index) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing!",
             esp_err_to_name(err));
    return;
  }

  err = nvs_set_i32(nvs_handle, "station_idx", station_index);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) writing 'station_idx' to NVS!",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Saved current_station = %d to NVS", station_index);
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) committing 'station_idx' to NVS!",
             esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
}

void change_station(int new_station_index) {
  esp_err_t ret;

  if (new_station_index < 0 || new_station_index >= station_count) {
    ESP_LOGE(TAG, "Invalid station index: %d", new_station_index);
    return;
  }

  // Only change if the new station is different from the current one
  if (new_station_index == current_station) {
    ESP_LOGI(TAG, "Station %d is already selected. No change needed.",
             new_station_index);
    return;
  }

  ESP_LOGI(TAG, "Destroying current pipeline...");
  destroy_audio_pipeline(&audio_pipeline_components);

  current_station = new_station_index;
  ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station,
           radio_stations[current_station].call_sign,
           radio_stations[current_station].city);
  sync_station_encoder_index(); // Sync encoder's internal state
  save_current_station_to_nvs(current_station);
  update_station_name(radio_stations[current_station].call_sign);
  update_station_city(radio_stations[current_station].city);

  ret = create_audio_pipeline(&audio_pipeline_components,
                              radio_stations[current_station].codec,
                              radio_stations[current_station].uri);
  if (ret != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Failed to create new audio pipeline for station %s, %s. Error: %d",
        radio_stations[current_station].call_sign,
        radio_stations[current_station].city, ret);
    return;
  }

  ESP_LOGI(TAG, "Starting new audio pipeline");
  ret = audio_pipeline_run(audio_pipeline_components.pipeline);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to run new audio pipeline. Error: %d", ret);
    destroy_audio_pipeline(&audio_pipeline_components);
  }
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
    case WIFI_PROV_START:
      switch_to_provisioning_screen();
      ESP_LOGI(TAG, "Provisioning started");
      break;
    case WIFI_PROV_CRED_RECV: {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG,
               "Received Wi-Fi credentials"
               "\n\tSSID: %s\n\tPassword: %s",
               (const char *)wifi_sta_cfg->ssid,
               (const char *)wifi_sta_cfg->password);
      break;
    }
    case WIFI_PROV_CRED_FAIL: {
      wifi_prov_sta_fail_reason_t *reason =
          (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG,
               "Provisioning failed!\n\tReason: %s"
               "\n\tPlease reset to factory and retry.",
               (*reason == WIFI_PROV_STA_AUTH_ERROR)
                   ? "Wi-Fi station authentication failed"
                   : "Wi-Fi station not found");
      break;
    }
    case WIFI_PROV_CRED_SUCCESS:
      switch_to_home_screen();
      ESP_LOGI(TAG, "Provisioning successful");
      break;
    case WIFI_PROV_END:
      /* De-initialize manager once provisioning is finished */
      wifi_prov_mgr_deinit();
      break;
    default:
      break;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
    esp_wifi_connect();
  }
}

static void wifi_init_sta(void) {
  /* Start Wi-Fi in station mode */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "agk radio:";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}

#include "esp_timer.h" // Added for watchdog timer

/**
 * @brief Task to measure and log the data throughput in kbps.
 */
static void data_throughput_task(void *pvParameters) {
#define BITRATE_HISTORY_SIZE 10
  static int bitrate_history[BITRATE_HISTORY_SIZE] = {0};
  static int history_index = 0;

  uint64_t last_bytes_read = 0;
  uint64_t current_bytes_read;
  uint64_t bytes_read_in_interval;

  static uint64_t prev_idle_0 = 0;
  static uint64_t prev_idle_1 = 0;
  static uint64_t prev_total = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(BITRATE_UPDATE_INTERVAL_MS));

    current_bytes_read = g_bytes_read;
    bytes_read_in_interval = current_bytes_read - last_bytes_read;
    last_bytes_read = current_bytes_read;

    // Calculate current bitrate and add to history
    int current_bitrate =
        (bytes_read_in_interval * 8) / BITRATE_UPDATE_INTERVAL_MS;
    bitrate_history[history_index] = current_bitrate;
    history_index = (history_index + 1) % BITRATE_HISTORY_SIZE;

    // Calculate weighted moving average
    long weighted_sum = 0;
    int total_weight = 0;
    for (int i = 0; i < BITRATE_HISTORY_SIZE; i++) {
      int weight = i + 1; // Simple linear weight (1 for oldest, 10 for newest)
      int data_index = (history_index + i) % BITRATE_HISTORY_SIZE;
      weighted_sum += bitrate_history[data_index] * weight;
      total_weight += weight;
    }

    g_bitrate_kbps = weighted_sum / total_weight;
    update_bitrate_label(g_bitrate_kbps);

    // monitoring ram usage.  remove this for production
    size_t total_ram = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t used_ram = total_ram - free_ram;

    // Log RAM usage
    ESP_LOGI(TAG, "RAM: Used: %zu, Free: %zu, Total: %zu", used_ram, free_ram,
             total_ram);

    // Core Load Monitoring
    TaskStatus_t status;
    vTaskGetInfo(xTaskGetIdleTaskHandleForCPU(0), &status, pdFALSE, eInvalid);
    uint64_t idle_0 = status.ulRunTimeCounter;

    vTaskGetInfo(xTaskGetIdleTaskHandleForCPU(1), &status, pdFALSE, eInvalid);
    uint64_t idle_1 = status.ulRunTimeCounter;

    uint64_t current_total_time = esp_timer_get_time();

    if (prev_total > 0) {
      uint64_t total_diff = current_total_time - prev_total;
      uint64_t idle_0_diff = idle_0 - prev_idle_0;
      uint64_t idle_1_diff = idle_1 - prev_idle_1;

      // Ensure we don't divide by zero or get negative load due to
      // overflow/timing glitches
      if (total_diff > 0) {
        float load_0 = 100.0f * (1.0f - ((float)idle_0_diff / total_diff));
        float load_1 = 100.0f * (1.0f - ((float)idle_1_diff / total_diff));

        // Clamp to 0-100 range
        if (load_0 < 0)
          load_0 = 0;
        else if (load_0 > 100)
          load_0 = 100;
        if (load_1 < 0)
          load_1 = 0;
        else if (load_1 > 100)
          load_1 = 100;

        ESP_LOGI(TAG, "Core Load: Core 0: %.2f%%, Core 1: %.2f%%", load_0,
                 load_1);
      }
    }

    prev_idle_0 = idle_0;
    prev_idle_1 = idle_1;
    prev_total = current_total_time;

    // Watchdog check
    if (weighted_sum == 0) {
      int64_t uptime_us = esp_timer_get_time();
      if (uptime_us > TYPICAL_STARTUP_DURATION_US) {
        ESP_LOGE(TAG, "Watchdog triggered: Throughput is 0 for 10 samples. "
                      "Restarting...");
        esp_restart();
      }
    }
  }
}

static void bose_on_button_task(void *pvParameters) {
  ESP_LOGI(TAG, "Bose ON button task started.");
  while (1) {
    if (gpio_get_level(BOSE_ON_BUTTON_GPIO) ==
        0) {                         // Button is pressed (active low)
      vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
      if (gpio_get_level(BOSE_ON_BUTTON_GPIO) == 0) {
        ESP_LOGI(TAG, "Bose ON button pressed, sending AUX signal.");
        if (g_ir_tx_channel) {
          send_bose_ir_command(g_ir_tx_channel, BOSE_CMD_AUX);
        }
        // Wait for button release
        while (gpio_get_level(BOSE_ON_BUTTON_GPIO) == 0) {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLLING_PERIOD_MS));
  }
}

static void bose_off_button_task(void *pvParameters) {
  ESP_LOGI(TAG, "Bose OFF button task started.");
  while (1) {
    if (gpio_get_level(BOSE_OFF_BUTTON_GPIO) ==
        0) {                         // Button is pressed (active low)
      vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
      if (gpio_get_level(BOSE_OFF_BUTTON_GPIO) == 0) {
        ESP_LOGI(TAG,
                 "Bose OFF button pressed, sending AUX then ON/OFF signal.");
        if (g_ir_tx_channel) {
          send_bose_ir_command(g_ir_tx_channel, BOSE_CMD_AUX);
          vTaskDelay(
              pdMS_TO_TICKS(150)); // delay between commands for bose to respond
                                   // 150ms suffices, 100ms is too short
          send_bose_ir_command(g_ir_tx_channel, BOSE_CMD_ON_OFF);
        }
        // Wait for button release
        while (gpio_get_level(BOSE_OFF_BUTTON_GPIO) == 0) {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLLING_PERIOD_MS));
  }
}

void app_main(void) {
  int initial_volume = INITIAL_VOLUME;
  esp_log_level_set("*", ESP_LOG_DEBUG);
  // esp_log_level_set(TAG, ESP_LOG_DEBUG);
  // esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  nvs_handle_t nvs_handle;
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Reading current_station from NVS");
    int32_t station_from_nvs = 0; // default to 0
    err = nvs_get_i32(nvs_handle, "station_idx", &station_from_nvs);
    switch (err) {
    case ESP_OK:
      ESP_LOGI(TAG, "Successfully read current_station = %d",
               (int)station_from_nvs);
      if (station_from_nvs >= 0 && station_from_nvs < station_count) {
        current_station = station_from_nvs;
      } else {
        ESP_LOGW(TAG, "Invalid station index %d found in NVS, defaulting to 0",
                 (int)station_from_nvs);
        current_station = 0;
      }
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(TAG, "The value 'station_idx' is not initialized yet!");
      break;
    default:
      ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    // Read volume from NVS
    ESP_LOGI(TAG, "Reading volume from NVS");
    int32_t volume_from_nvs = INITIAL_VOLUME;
    err = nvs_get_i32(nvs_handle, "volume", &volume_from_nvs);
    switch (err) {
    case ESP_OK:
      ESP_LOGI(TAG, "Successfully read volume = %d", (int)volume_from_nvs);
      if (volume_from_nvs >= 0 && volume_from_nvs <= 100) {
        initial_volume = volume_from_nvs;
      } else {
        ESP_LOGW(TAG, "Invalid volume %d found in NVS, defaulting to %d",
                 (int)volume_from_nvs, INITIAL_VOLUME);
      }
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(TAG, "The value 'volume' is not initialized yet!");
      break;
    default:
      ESP_LOGE(TAG, "Error (%s) reading 'volume'!", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
  }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
  ESP_ERROR_CHECK(esp_netif_init());
#else
  tcpip_adapter_init();
#endif
  // Create the UI message queue before any UI tasks are started
  g_ui_queue = xQueueCreate(10, sizeof(ui_update_message_t));

  display = lvgl_ssd1306_setup();
  screens_init(display);
  update_station_name(radio_stations[current_station].call_sign);
  update_station_city(radio_stations[current_station].city);

  // start oled display test task,  remove after debugging
  // xTaskCreate(task_test_ssd1306, "u8g2_task", 4096, NULL, 5, NULL);

  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_prov_mgr_config_t config = {
      .scheme = wifi_prov_scheme_ble,
      .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

  ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
  // provisioned = false; // force provisioning for testing comment this line
  // for production
  uint8_t custom_service_uuid[] = {
      /* LSB <---------------------------------------
       * ---------------------------------------> MSB */
      0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
      0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
  };

  // Configure Bose control buttons
  gpio_config_t bose_button_gpio_config = {
      .pin_bit_mask =
          (1ULL << BOSE_ON_BUTTON_GPIO) | (1ULL << BOSE_OFF_BUTTON_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&bose_button_gpio_config));

  if (!provisioned || gpio_get_level(BOSE_OFF_BUTTON_GPIO) == 0) {
    if (provisioned) {
      ESP_LOGI(TAG, "Bose OFF button pressed. Resetting provisioning.");
      wifi_prov_mgr_reset_provisioning();
    }

    ESP_LOGI(TAG, "Starting provisioning");

    char service_name[20];
    get_device_service_name(service_name, sizeof(service_name));

    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL,
                                                     service_name, NULL));
  } else {
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi");
  }

  ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
  wifi_init_sta();
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true,
                      portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi Connected.");

  // wifi_event_group = xEventGroupCreate();

  // ESP_ERROR_CHECK(esp_event_loop_create_default());
  // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT,
  // ESP_EVENT_ANY_ID, &event_handler, NULL));
  // ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
  // &event_handler, NULL));
  // ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
  // &event_handler, NULL));

  // esp_netif_create_default_wifi_sta();

  // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  // ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // wifi_prov_mgr_config_t config = {
  //     .scheme = wifi_prov_scheme_ble,
  //     .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
  // };

  // ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

  // bool provisioned = false;
  // ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
  // provisioned = false; // force provisioning for testing comment this line
  // for production uint8_t custom_service_uuid[] = {
  //     /* LSB <---------------------------------------
  //      * ---------------------------------------> MSB */
  //     0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
  //     0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
  // };

  // if (!provisioned) {
  //     ESP_LOGI(TAG, "Starting provisioning");

  //     char service_name[20];
  //     get_device_service_name(service_name, sizeof(service_name));

  //     wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
  //     ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
  //     NULL, service_name, NULL));
  // }
  // else {
  //     ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi");
  //     wifi_prov_mgr_deinit();

  //     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
  //     ESP_EVENT_ANY_ID, &event_handler, NULL));
  //     /* Start Wi-Fi station */
  //     wifi_init_sta();

  // }

  // const TickType_t wifi_connect_timeout = portMAX_DELAY; //
  // pdMS_TO_TICKS(30000); // 30 seconds ESP_LOGI(TAG, "Waiting for Wi-Fi
  // connection..."); we should probably have a timeout here.  If it can't
  // connect, restart provisioning. EventBits_t bits =
  // xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
  // wifi_connect_timeout);

  // if ((bits & WIFI_CONNECTED_BIT) == 0) {
  //     ESP_LOGE(TAG, "Failed to connect to Wi-Fi within 30 seconds. Resetting
  //     provisioning and restarting.");
  //     // Erase provisioning data
  //     wifi_prov_mgr_reset_provisioning();
  //     vTaskDelay(pdMS_TO_TICKS(2000)); // Delay to allow logging
  //     esp_restart();
  // }

  ESP_LOGI(TAG, "Wi-Fi Connected.");

  esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
  periph_set = esp_periph_set_init(&periph_cfg);

  ESP_LOGI(TAG, "Start audio codec chip");
  board_handle = audio_board_init(); // Assign to global
  // explicit start the codec, I'm not sure why it was not started elsewhere.
  board_handle->audio_hal->audio_codec_ctrl(AUDIO_HAL_CODEC_MODE_BOTH,
                                            AUDIO_HAL_CTRL_START);
  audio_hal_set_volume(board_handle->audio_hal, initial_volume);
  update_volume_slider(initial_volume);
  // audio_hal_get_volume(board_handle->audio_hal, &temp_volume);
  // ESP_LOGI(TAG, "Initial volume set to %d", temp_volume);

  ESP_LOGI(TAG, "Set up  event listener");
  audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
  evt = audio_event_iface_init(&evt_cfg); // Assign to static global

  ESP_LOGI(TAG, "Listening event from peripherals");
  audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
                                 evt);

  xTaskCreate(bose_on_button_task, "bose_on_button_task", 2048, NULL, 5, NULL);
  xTaskCreate(bose_off_button_task, "bose_off_button_task", 2048, NULL, 5,
              NULL);

  ESP_LOGI(TAG, "Initializing IR RMT");
  g_ir_tx_channel = init_ir_rmt(IR_TX_GPIO_NUM);
  ESP_LOGI(TAG, "Sending Bose AUX signal");
  send_bose_ir_command(g_ir_tx_channel, BOSE_CMD_AUX);

  ESP_LOGI(TAG, "Starting initial stream: %s, %s",
           radio_stations[current_station].call_sign,
           radio_stations[current_station].city);
  err = create_audio_pipeline(&audio_pipeline_components,
                              radio_stations[current_station].codec,
                              radio_stations[current_station].uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create initial audio pipeline, error: %d", err);
    // Cleanup before returning
    if (evt)
      audio_event_iface_destroy(evt);
    if (periph_set)
      esp_periph_set_destroy(periph_set);
    // audio_board_deinit(board_handle); // If applicable
    return;
  }

  ESP_LOGI(TAG, "Start audio_pipeline");
  audio_pipeline_run(audio_pipeline_components.pipeline);

  xTaskCreate(data_throughput_task, "data_throughput_task", 3 * 1024, NULL, 5,
              NULL);

  //  start encoder pulse counters

  init_encoders(board_handle, initial_volume);

  while (1) {
    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
      continue;
    }
    ESP_LOGI(TAG, "Received event from element: %X, command: %d",
             (int)msg.source, msg.cmd);

    /* restart stream when the first pipeline element (http_stream_reader in
     * this case) receives stop event (caused by reading errors) */
    if (audio_pipeline_components.http_stream_reader &&
        msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
        msg.source == (void *)audio_pipeline_components.http_stream_reader &&
        msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
        (int)msg.data == AEL_STATUS_ERROR_OPEN) {
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

  if (periph_set) {
    esp_periph_set_stop_all(periph_set);
    if (evt) {
      audio_event_iface_remove_listener(
          esp_periph_set_get_event_iface(periph_set), evt);
    }
  }

  if (evt) {
    audio_event_iface_destroy(evt);
  }
  if (periph_set) {
    esp_periph_set_destroy(periph_set);
  }
}