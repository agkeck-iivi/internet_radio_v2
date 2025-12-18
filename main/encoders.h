#ifndef ENCODERS_H
#define ENCODERS_H

// #include "audio_hal.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

void init_encoders(audio_board_handle_t board_handle, int initial_volume);

/**
 * @brief Initialize only the encoder button switches (GPIOs).
 * Call this early in boot to allow checking button state.
 */
void init_encoder_switches(void);

/**
 * @brief Check if the station encoder button is currently pressed.
 * @return true if pressed (LOW), false otherwise.
 */
bool is_station_switch_pressed(void);

/**
 * @brief Synchronizes the station encoder's internal index with the global
 * current_station.
 */
void sync_station_encoder_index(void);

#ifdef __cplusplus
}
#endif

#endif // ENCODERS_H