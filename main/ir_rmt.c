#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
// #include "ir_nec_encoder.h"
#include "driver/rmt_encoder.h"
#include "ir_rmt.h"

#define IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us

static const char* TAG = "ir_rmt";

static const rmt_symbol_word_t bose_aux_signal[] = {
    {.duration0 = 1104, .level0 = 1, .duration1 = 1467, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1428, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 571, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9531, .level0 = 1, .duration1 = 4580, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 633, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1705, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 0, .level1 = 0},
};

static const rmt_symbol_word_t bose_on_off_signal[] = {
    {.duration0 = 1103, .level0 = 1, .duration1 = 1466, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 453, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 432, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 452, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9537, .level0 = 1, .duration1 = 4566, .level1 = 0},
    {.duration0 = 506, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1712, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 1703, .level1 = 0},
    {.duration0 = 506, .level0 = 1, .duration1 = 1704, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 0, .level1 = 0},
};

rmt_channel_handle_t init_ir_rmt(gpio_num_t tx_gpio_num)
{
    // ESP_LOGI(TAG, "create RMT RX channel");
    // rmt_rx_channel_config_t rx_channel_cfg = {
    //     .clk_src = RMT_CLK_SRC_DEFAULT,
    //     .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
    //     .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
    //     .gpio_num = EXAMPLE_IR_RX_GPIO_NUM,
    // };
    // rmt_channel_handle_t rx_channel = NULL;
    // ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

    // ESP_LOGI(TAG, "register RX done callback");
    // QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    // assert(receive_queue);
    // rmt_rx_event_callbacks_t cbs = {
    //     .on_recv_done = example_rmt_rx_done_callback,
    // };
    // ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, receive_queue));

    // // the following timing requirement is based on NEC protocol
    // rmt_receive_config_t receive_config = {
    //     .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
    //     .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
    // };

    ESP_LOGI(TAG, "create RMT TX channel");
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
        .trans_queue_depth = 4, // number of transactions that allowed to pending in the background, this example won't queue multiple transactions, so queue depth > 1 is sufficient
        .gpio_num = tx_gpio_num,
    };
    rmt_channel_handle_t tx_channel = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));

    ESP_LOGI(TAG, "modulate carrier to TX channel");
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000, // 38KHz
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));

    // ESP_LOGI(TAG, "install IR NEC encoder");
    // ir_nec_encoder_config_t nec_encoder_cfg = {
    //     .resolution = EXAMPLE_IR_RESOLUTION_HZ,
    // };
    // rmt_encoder_handle_t nec_encoder = NULL;
    // ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&nec_encoder_cfg, &nec_encoder));

    ESP_LOGI(TAG, "enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    // ESP_ERROR_CHECK(rmt_enable(rx_channel));

    return tx_channel;
}


esp_err_t send_bose_ir_command(rmt_channel_handle_t tx_channel, bose_ir_command_t command)
{
    if (!tx_channel) {
        ESP_LOGE(TAG, "Invalid RMT TX channel handle");
        return ESP_ERR_INVALID_ARG;
    }

    const rmt_symbol_word_t* signal_data = NULL;
    size_t signal_size = 0;

    switch (command) {
    case BOSE_CMD_AUX:
        signal_data = bose_aux_signal;
        signal_size = sizeof(bose_aux_signal);
        break;
    case BOSE_CMD_ON_OFF:
        signal_data = bose_on_off_signal;
        signal_size = sizeof(bose_on_off_signal);
        break;
    default:
        ESP_LOGE(TAG, "Unknown IR command: %d", command);
        return ESP_ERR_INVALID_ARG;
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // no loop
    };

    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));

    ESP_LOGI(TAG, "Transmitting BOSE IR command...");
    esp_err_t ret = rmt_transmit(tx_channel, copy_encoder, signal_data, signal_size, &transmit_config);

    rmt_del_encoder(copy_encoder);

    return ret;
}
