#ifndef ENCODERS_H
#define ENCODERS_H

#include "audio_hal.h"
#include "board.h"


#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Initializes the rotary encoders using the PCNT peripheral.
     * @param board_handle Handle to the audio board for volume control.
     * @param initial_volume The initial volume value (0-100) to set for the counter.
     */
    void init_encoders(audio_board_handle_t board_handle, int initial_volume);

#ifdef __cplusplus
}
#endif

#endif // ENCODERS_H