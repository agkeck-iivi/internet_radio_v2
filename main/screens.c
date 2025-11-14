/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include "screens.h"
#include "esp_log.h"
#include "station_data.h"

extern float g_bitrate_kbps;
extern int current_station;

static lv_obj_t* bitrate_label = NULL;
static lv_obj_t* callsign_label = NULL;
static lv_obj_t* city_label = NULL;
static lv_obj_t* volume_slider = NULL;
static lv_obj_t* station_roller = NULL;

static lv_obj_t* home_screen_obj = NULL;
static lv_obj_t* station_selection_screen_obj = NULL;



void update_bitrate_label(float bitrate)
{
    if (bitrate_label)
    {
        lv_label_set_text_fmt(bitrate_label, "%d KBPS", (int)bitrate);
    }
}
void update_station_name(const char* name)
{
    if (callsign_label) {
        lv_label_set_text(callsign_label, name);
    }
}

void update_station_city(const char* city)
{
    if (city_label) {
        lv_label_set_text(city_label, city);
    }
}

void update_volume_slider(int volume)
{
    if (volume_slider)
    {
        lv_slider_set_value(volume_slider, volume, LV_ANIM_ON);
    }
}

void update_station_roller(int new_station_index)
{
    if (station_roller)
    {
        lv_roller_set_selected(station_roller, new_station_index, LV_ANIM_ON);
    }
}

static void create_home_screen_widgets(lv_obj_t* parent)
{
    lv_display_t* disp = lv_display_get_default();
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
    lv_obj_set_style_bg_color(volume_slider, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    // slider border
    // lv_obj_set_style_border_width(volume_slider, 1, LV_PART_MAIN);
    // lv_obj_set_style_border_color(volume_slider, lv_color_black(), LV_PART_MAIN);

    lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(volume_slider, LV_OPA_TRANSP, LV_PART_KNOB); // Hide the knob

    // 2. Volume Percentage Label
    // Make the label a child of the slider for easier positioning relative to it
    // lv_obj_t *volume_label = lv_label_create(volume_slider);
    // lv_label_set_text_fmt(volume_label, "%d%%", (int)lv_slider_get_value(volume_slider));
    // lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_8, 0); // Use default font
    // lv_obj_align(volume_label, LV_ALIGN_TOP_MID, 0, 0);                  // Align to the top-middle of the slider

    // Add an event handler to update the volume percentage label when the slider value changes
    // lv_obj_add_event_cb(volume_slider, NULL, LV_EVENT_VALUE_CHANGED, volume_label);

    // 3. Container for Call Sign and City text
    // This container will take up the remaining width of the screen to the right of the slider
    lv_obj_t* text_container = lv_obj_create(parent);
    lv_obj_set_size(text_container, screen_width - slider_width, screen_height);
    lv_obj_align_to(text_container, volume_slider, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0); // Transparent background
    lv_obj_set_style_border_width(text_container, 0, 0);       // No border
    lv_obj_set_flex_flow(text_container, LV_FLEX_FLOW_COLUMN); // Arrange children vertically
    // Center children horizontally and vertically within the container
    lv_obj_set_style_pad_row(text_container, 2, 0); // Set the vertical gap between children
    lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 4. Call Sign Label
    callsign_label = lv_label_create(text_container);
    // Use Montserrat 24 for a larger call sign. Ensure LV_FONT_MONTSERRAT_24 is enabled in lv_conf.h
    lv_obj_set_style_text_font(callsign_label, &lv_font_montserrat_32, 0);

    // 5. City Label
    city_label = lv_label_create(text_container);
    lv_obj_set_style_text_font(city_label, &lv_font_montserrat_12, 0); // Use default font for smaller text
    // bitrate label
    bitrate_label = lv_label_create(text_container);
    lv_label_set_text_fmt(bitrate_label, "%3.0f KBPS", g_bitrate_kbps);
    lv_obj_set_style_text_font(bitrate_label, &lv_font_montserrat_14, 0); // Use default font for smaller text
}

static void create_station_selection_screen_widgets(lv_obj_t* parent)
{
    // lv_coord_t line_width_pct = 90;
    // lv_coord_t line_width = (screen_width * line_width_pct) / 100;
    // lv_coord_t line_y = screen_height / 2;
    // lv_coord_t line_x_start = (screen_width - line_width) / 2;
    // lv_coord_t line_x_end = line_x_start + line_width;

    // lv_point_t line_points[] = { {0, 0}, {0, 0} };
    // line_points[0].x = line_x_start;
    // line_points[0].y = line_y;
    // line_points[1].x = line_x_end;
    // line_points[1].y = line_y;

    // static lv_style_t style_line; // Style can remain static
    // lv_style_init(&style_line);
    // lv_style_set_line_width(&style_line, 1);
    // lv_style_set_line_color(&style_line, lv_color_white());

    // lv_obj_t* line = lv_line_create(scr);
    // lv_line_set_points(line, line_points, 2);
    // lv_obj_add_style(line, &style_line, 0);

    // Build the options string for the roller
    char roller_options[1024] = { 0 }; // Make sure this is large enough
    for (int i = 0; i < station_count; i++) {
        strcat(roller_options, radio_stations[i].call_sign);
        if (i < station_count - 1) {
            strcat(roller_options, "\n");
        }
    }

    /*Create a roller*/
    station_roller = lv_roller_create(parent);
    lv_roller_set_options(station_roller,
        roller_options,
        LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(station_roller, 3);
    lv_obj_set_width(station_roller, lv_pct(80));
    lv_obj_center(station_roller);

    // Use a smaller font and remove the inverted selection style
    lv_obj_set_style_text_font(station_roller, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(station_roller, LV_OPA_TRANSP, LV_PART_SELECTED);
    lv_obj_set_style_text_color(station_roller, lv_palette_main(LV_PALETTE_BLUE), LV_PART_SELECTED);
    lv_obj_set_style_text_line_space(station_roller, 4, 0); // Reduce space between items


    // Set initial station
    lv_roller_set_selected(station_roller, current_station, LV_ANIM_OFF);

    // // Event handler for when the roller value changes
    // lv_obj_add_event_cb(station_roller, event_handler, LV_EVENT_ALL, NULL);
}

void screens_init(lv_display_t* disp)
{
    home_screen_obj = lv_obj_create(NULL);
    station_selection_screen_obj = lv_obj_create(NULL);

    create_home_screen_widgets(home_screen_obj);
    create_station_selection_screen_widgets(station_selection_screen_obj);

    // Start on the home screen
    lv_screen_load(home_screen_obj);
}

void switch_to_home_screen(void)
{
    lv_screen_load(home_screen_obj);
}

void switch_to_station_selection_screen(void)
{
    lv_screen_load(station_selection_screen_obj);
}