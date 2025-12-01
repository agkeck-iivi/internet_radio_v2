#ifndef SCREENS_H
#define SCREENS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        UPDATE_BITRATE,
        UPDATE_STATION_NAME,
        UPDATE_STATION_CITY,
        UPDATE_VOLUME,
        UPDATE_STATION_ROLLER,
        SWITCH_TO_HOME,
        SWITCH_TO_STATION_SELECTION,
        SWITCH_TO_PROVISIONING
    } ui_update_type_t;

    typedef struct {
        ui_update_type_t type;
        union {
            int value;
            const char* str_value;
        } data;
    } ui_update_message_t;

    extern QueueHandle_t g_ui_queue;
    /**
     * @brief Initializes all UI screens.
     * @param disp Pointer to the LVGL display.
     */
    void screens_init(lv_display_t* disp);

    /**
     * @brief Processes all pending UI update messages from the queue.
     */
    void process_ui_updates(void);

    /**
     * @brief Switches the active view to the home screen.
     */
    void switch_to_home_screen(void);

    /**
     * @brief Switches the active view to the station selection screen.
     */
    void switch_to_station_selection_screen(void);

    /**
     * @brief Switches the active view to the provisioning screen.
     */
    void switch_to_provisioning_screen(void);
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
    void update_bitrate_label(int bitrate);

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