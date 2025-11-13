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
     */
    void init_encoders(audio_board_handle_t board_handle);

#ifdef __cplusplus
}
#endif

#endif // ENCODERS_H