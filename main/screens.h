#ifndef SCREENS_H
#define SCREENS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Initializes all UI screens.
     * @param disp Pointer to the LVGL display.
     */
    void screens_init(lv_display_t* disp);

    /**
     * @brief Switches the active view to the home screen.
     */
    void switch_to_home_screen(void);

    /**
     * @brief Switches the active view to the station selection screen.
     */
    void switch_to_station_selection_screen(void);
    /**
     * @brief Updates the station name label on the screen.
     * @param name The new station name to display.
     */
    void update_station_name(const char* name);

    /**
     * @brief Updates the station city label on the screen.
     * @param city The new city name to display.
     */
    void update_station_city(const char* city);

    /**
     * @brief Updates the bitrate label on the screen.
     * @param bitrate The new bitrate value in kbps.
     */
    void update_bitrate_label(float bitrate);

    /**
     * @brief Updates the volume slider on the screen.
     * @param volume The new volume value (0-100).
     */
    void update_volume_slider(int volume);

    /**
     * @brief Updates the station roller to a new station index.
     * @param new_station_index The index of the new station to select.
     */
    void update_station_roller(int new_station_index);

#ifdef __cplusplus
}
#endif

#endif // SCREENS_H