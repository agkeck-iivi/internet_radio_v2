/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "screens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "station_data.h"
#include <string.h>

extern int g_bitrate_kbps;
extern int current_station;

QueueHandle_t g_ui_queue;
static const char *TAG = "SCREENS";

static lv_obj_t *bitrate_label = NULL;
static lv_obj_t *callsign_label = NULL;
static lv_obj_t *city_label = NULL;
static lv_obj_t *volume_slider = NULL;
static lv_obj_t *station_roller = NULL;

static lv_obj_t *home_screen_obj = NULL;
static lv_obj_t *station_selection_screen_obj = NULL;

static lv_obj_t *message_screen_obj = NULL;
static lv_obj_t *message_label = NULL;

void update_bitrate_label(int bitrate) {
  ui_update_message_t msg = {.type = UPDATE_BITRATE, .data.value = bitrate};
  xQueueSend(g_ui_queue, &msg, 0);
}

void update_station_name(const char *name) {
  ui_update_message_t msg = {.type = UPDATE_STATION_NAME,
                             .data.str_value = name};
  xQueueSend(g_ui_queue, &msg, 0);
}

void update_station_city(const char *city) {
  ui_update_message_t msg = {.type = UPDATE_STATION_CITY,
                             .data.str_value = city};
  xQueueSend(g_ui_queue, &msg, 0);
}

void update_volume_slider(int volume) {
  ui_update_message_t msg = {.type = UPDATE_VOLUME, .data.value = volume};
  xQueueSend(g_ui_queue, &msg, 0);
}

void update_station_roller(int new_station_index) {
  ui_update_message_t msg = {.type = UPDATE_STATION_ROLLER,
                             .data.value = new_station_index};
  xQueueSend(g_ui_queue, &msg, 0);
}

void switch_to_provisioning_screen(void) {
  ui_update_message_t msg = {.type = SWITCH_TO_PROVISIONING};
  xQueueSend(g_ui_queue, &msg, 0);
}

void switch_to_ip_screen(void) {
  ui_update_message_t msg = {.type = SWITCH_TO_IP_SCREEN};
  xQueueSend(g_ui_queue, &msg, 0);
}

void update_ip_label(const char *ip) {
  ui_update_message_t msg = {.type = UPDATE_IP_LABEL, .data.str_value = ip};
  xQueueSend(g_ui_queue, &msg, 0);
}

void switch_to_reboot_screen(void) {
  ui_update_message_t msg = {.type = SWITCH_TO_REBOOT_SCREEN};
  xQueueSend(g_ui_queue, &msg, 0);
}

void process_ui_updates(void) {
  // Defensive check to ensure the queue has been created.
  if (g_ui_queue == NULL) {
    return;
  }

  ui_update_message_t msg;
  while (xQueueReceive(g_ui_queue, &msg, 0) == pdTRUE) {
    switch (msg.type) {
    case UPDATE_BITRATE:
      if (bitrate_label)
        lv_label_set_text_fmt(bitrate_label, "%d KBPS", msg.data.value);
      break;
    case UPDATE_STATION_NAME:
      if (callsign_label)
        lv_label_set_text(callsign_label, msg.data.str_value);
      break;
    case UPDATE_STATION_CITY:
      if (city_label)
        lv_label_set_text(city_label, msg.data.str_value);
      break;
    case UPDATE_VOLUME:
      if (volume_slider)
        lv_slider_set_value(volume_slider, msg.data.value, LV_ANIM_ON);
      break;
    case UPDATE_STATION_ROLLER:
      if (station_roller)
        lv_roller_set_selected(station_roller, msg.data.value, LV_ANIM_ON);
      break;
    case SWITCH_TO_HOME:
      if (home_screen_obj)
        lv_screen_load(home_screen_obj);
      break;
    case SWITCH_TO_STATION_SELECTION:
      if (station_selection_screen_obj)
        lv_screen_load(station_selection_screen_obj);
      break;
    case SWITCH_TO_PROVISIONING:
      if (message_screen_obj) {
        lv_label_set_text(message_label, "Setup WIFI with\nESP BLE Prov\napp");
        lv_screen_load(message_screen_obj);
      }
      break;
    // ... inside process_ui_updates default/switch ...
    case SWITCH_TO_REBOOT_SCREEN:
      if (message_screen_obj) {
        lv_label_set_text(message_label, "Rebooting");
        lv_screen_load(message_screen_obj);
      }
      break;
    case SWITCH_TO_IP_SCREEN:
      if (message_screen_obj) {
        // IP text is usually updated via UPDATE_IP_LABEL, but we can set a
        // placeholder if needed or just keep the previous text if we want to be
        // safe, but typically we want to clear or set placeholder
        // lv_label_set_text(message_label, "IP Address:\n...");
        lv_screen_load(message_screen_obj);
      }
      break;
    case UPDATE_IP_LABEL:
      if (message_label)
        lv_label_set_text(message_label, msg.data.str_value);
      break;
    default:
      ESP_LOGW(TAG, "Unknown UI update type: %d", msg.type);
      break;
    }
  }
}

static void create_home_screen_widgets(lv_obj_t *parent) {
  lv_display_t *disp = lv_display_get_default();
  lv_coord_t screen_width = lv_display_get_horizontal_resolution(disp);
  lv_coord_t screen_height = lv_display_get_vertical_resolution(disp);

  // 1. Volume Control Slider
  volume_slider = lv_slider_create(parent);
  lv_coord_t slider_width = 6; // Width of the volume slider
  lv_obj_set_size(volume_slider, slider_width, screen_height);
  lv_obj_align(volume_slider, LV_ALIGN_LEFT_MID, 0, 0);
  lv_slider_set_range(volume_slider, 0, 100);
  lv_slider_set_value(volume_slider, 0, LV_ANIM_OFF);

  // Apply styles for a rounded rectangular appearance
  lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(volume_slider, lv_palette_main(LV_PALETTE_GREY),
                            LV_PART_MAIN);
  // slider border
  // lv_obj_set_style_border_width(volume_slider, 1, LV_PART_MAIN);
  // lv_obj_set_style_border_color(volume_slider, lv_color_black(),
  // LV_PART_MAIN);

  lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(volume_slider, lv_palette_main(LV_PALETTE_BLUE),
                            LV_PART_INDICATOR);

  lv_obj_set_style_bg_opa(volume_slider, LV_OPA_TRANSP,
                          LV_PART_KNOB); // Hide the knob

  // 2. Volume Percentage Label
  // Make the label a child of the slider for easier positioning relative to it
  // lv_obj_t *volume_label = lv_label_create(volume_slider);
  // lv_label_set_text_fmt(volume_label, "%d%%",
  // (int)lv_slider_get_value(volume_slider));
  // lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_8, 0); // Use
  // default font lv_obj_align(volume_label, LV_ALIGN_TOP_MID, 0, 0); // Align
  // to the top-middle of the slider

  // Add an event handler to update the volume percentage label when the slider
  // value changes lv_obj_add_event_cb(volume_slider, NULL,
  // LV_EVENT_VALUE_CHANGED, volume_label);

  // 3. Container for Call Sign and City text
  // This container will take up the remaining width of the screen to the right
  // of the slider
  lv_obj_t *text_container = lv_obj_create(parent);
  lv_obj_set_size(text_container, screen_width - slider_width, screen_height);
  lv_obj_align_to(text_container, volume_slider, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
  lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP,
                          0);                          // Transparent background
  lv_obj_set_style_border_width(text_container, 0, 0); // No border
  lv_obj_set_flex_flow(text_container,
                       LV_FLEX_FLOW_COLUMN); // Arrange children vertically
  // Center children horizontally and vertically within the container
  lv_obj_set_style_pad_row(text_container, 2,
                           0); // Set the vertical gap between children
  lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // 4. Call Sign Label
  callsign_label = lv_label_create(text_container);
  // Use Montserrat 24 for a larger call sign. Ensure LV_FONT_MONTSERRAT_24 is
  // enabled in lv_conf.h
  lv_obj_set_style_text_font(callsign_label, &lv_font_montserrat_32, 0);

  // 5. City Label
  city_label = lv_label_create(text_container);
  lv_obj_set_style_text_font(city_label, &lv_font_montserrat_12,
                             0); // Use default font for smaller text
  // bitrate label
  bitrate_label = lv_label_create(text_container);
  lv_label_set_text_fmt(bitrate_label, "%d KBPS", g_bitrate_kbps);
  lv_obj_set_style_text_font(bitrate_label, &lv_font_montserrat_14,
                             0); // Use default font for smaller text
}

static void create_station_selection_screen_widgets(lv_obj_t *parent) {

  // Build the options string for the roller
  char roller_options[1024] = {0}; // Make sure this is large enough
  for (int i = 0; i < station_count; i++) {
    strcat(roller_options, radio_stations[i].call_sign);
    if (i < station_count - 1) {
      strcat(roller_options, "\n");
    }
  }

  /*Create a roller*/
  station_roller = lv_roller_create(parent);
  lv_roller_set_options(station_roller, roller_options,
                        LV_ROLLER_MODE_INFINITE);

  lv_roller_set_visible_row_count(station_roller, 3);
  lv_obj_set_width(station_roller, lv_pct(80));
  lv_obj_center(station_roller);

  // Use a smaller font and remove the inverted selection style
  lv_obj_set_style_text_font(station_roller, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(station_roller, LV_OPA_TRANSP, LV_PART_SELECTED);
  lv_obj_set_style_text_color(station_roller, lv_palette_main(LV_PALETTE_BLUE),
                              LV_PART_SELECTED);
  lv_obj_set_style_text_line_space(station_roller, 2,
                                   0); // Reduce space between items

  // Set initial station
  lv_roller_set_selected(station_roller, current_station, LV_ANIM_ON);

  // build gnomon
  static lv_point_precise_t line_points[] = {{0, 32}, {32, 32}};
  /*Create style*/
  static lv_style_t style_line;
  lv_style_init(&style_line);
  lv_style_set_line_width(&style_line, 6);
  lv_style_set_line_color(&style_line, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_line_rounded(&style_line, true);

  /*Create a line and apply the new style*/
  lv_obj_t *line1;
  lv_obj_t *line2;
  line1 = lv_line_create(parent);
  line2 = lv_line_create(parent);
  lv_line_set_points(line1, line_points, 2); /*Set the points*/
  lv_line_set_points(line2, line_points, 2); /*Set the points*/
  lv_obj_add_style(line1, &style_line, 0);
  lv_obj_add_style(line2, &style_line, 0);
  lv_obj_align(line1, LV_ALIGN_LEFT_MID, 0, -15);
  lv_obj_align(line2, LV_ALIGN_RIGHT_MID, 0, -15);

  // // Event handler for when the roller value changes
  // lv_obj_add_event_cb(station_roller, event_handler, LV_EVENT_ALL, NULL);
}

static void create_message_screen_widgets(lv_obj_t *parent) {
  message_label = lv_label_create(parent);
  lv_label_set_text(message_label, ""); // Initial empty text
  lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(message_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_line_space(message_label, 5, 0);
  lv_obj_center(message_label);
}

void screens_init(lv_display_t *disp) {
  g_ui_queue = xQueueCreate(10, sizeof(ui_update_message_t));
  home_screen_obj = lv_obj_create(NULL);
  station_selection_screen_obj = lv_obj_create(NULL);
  message_screen_obj = lv_obj_create(NULL);

  create_home_screen_widgets(home_screen_obj);
  create_station_selection_screen_widgets(station_selection_screen_obj);
  create_station_selection_screen_widgets(station_selection_screen_obj);
  create_message_screen_widgets(message_screen_obj);

  // Start on the home screen
  lv_screen_load(home_screen_obj);
}

void switch_to_home_screen(void) {
  ui_update_message_t msg = {.type = SWITCH_TO_HOME};
  xQueueSend(g_ui_queue, &msg, 0);
}

void switch_to_station_selection_screen(void) {
  ui_update_message_t msg = {.type = SWITCH_TO_STATION_SELECTION};
  xQueueSend(g_ui_queue, &msg, 0);
}