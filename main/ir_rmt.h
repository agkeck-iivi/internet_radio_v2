#ifndef IR_RMT_H
#define IR_RMT_H

#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Enumeration for different Bose IR commands.
     */
    typedef enum {
        BOSE_CMD_AUX,
        BOSE_CMD_ON_OFF,
    } bose_ir_command_t;

    /**
     * @brief Initializes the RMT peripheral for IR transmission.
     * @param tx_gpio_num The GPIO pin to use for the IR transmitter.
     * @return A handle to the created RMT TX channel.
     */
    rmt_channel_handle_t init_ir_rmt(gpio_num_t tx_gpio_num);

    /**
     * @brief Sends a specific Bose IR command.
     * @param tx_channel The RMT TX channel handle.
     * @param command The command to send from the bose_ir_command_t enum.
     * @return ESP_OK on success, or an error code on failure.
     */
    esp_err_t send_bose_ir_command(rmt_channel_handle_t tx_channel, bose_ir_command_t command);

#ifdef __cplusplus
}
#endif

#endif // IR_RMT_H