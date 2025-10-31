#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <u8g2.h>

#include "sdkconfig.h"
#include "u8g2_esp32_hal.h"

// CLK - GPIO14  SCL on lcd panel
// #define PIN_CLK 14
#define PIN_CLK 12

// MOSI - GPIO 13 sda on lcd panel
// #define PIN_MOSI 13
#define PIN_MOSI 11

// RESET - GPIO 26 rst on lcd panel
// #define PIN_RESET 26
#define PIN_RESET 13

// DC - GPIO 27 cd on lcd panel?
// #define PIN_DC 27
#define PIN_DC 14

// CS - GPIO 15 cs on lcd panel
// #define PIN_CS 15
#define PIN_CS 8
static char tag[] = "test_ssd1306";

void task_test_ssd1306(void *ignore)
{
  u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
  u8g2_esp32_hal.bus.spi.clk = PIN_CLK;
  u8g2_esp32_hal.bus.spi.mosi = PIN_MOSI;
  u8g2_esp32_hal.bus.spi.cs = PIN_CS;
  u8g2_esp32_hal.dc = PIN_DC;
  u8g2_esp32_hal.reset = PIN_RESET;
  u8g2_esp32_hal_init(u8g2_esp32_hal);

  // this might be the init for the oled display:  maybe u8g2_Setup_ssd1306_128x64_noname

  u8g2_t u8g2; // a structure which will contain all the data for one display
  
  u8g2_Setup_ssd1306_128x64_noname_f(
      &u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb,
      u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure

  u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in
                           // sleep mode after this,

  u8g2_SetPowerSave(&u8g2, 0); // wake up display
  u8x8_SetContrast(&u8g2.u8x8, 160);
  u8g2_ClearBuffer(&u8g2);
  u8g2_DrawBox(&u8g2, 10, 20, 20, 30);
  u8g2_SetFont(&u8g2, u8g2_font_helvR10_tr);
  u8g2_DrawStr(&u8g2, 0, 15, "Hello World!");
  u8g2_SendBuffer(&u8g2);
  char buf[100];
  int count = 0;
  // uint8_t contrast = 0;

  while (1)
  {
    u8g2_ClearBuffer(&u8g2);
    // ESP_LOGI(tag, "printing count %d", count);
    ESP_LOGI(tag, "printing count %d", count);
    snprintf(&buf[0], 100, "count: %d", count++);
    u8g2_DrawStr(&u8g2, 0, 15, &buf[0]);
    snprintf(&buf[0], 100, "13 count: %d", count * 13);
    u8g2_DrawStr(&u8g2, 0, 15 * 2, &buf[0]);
    snprintf(&buf[0], 100, "17 count: %d", count * 17);
    u8g2_DrawStr(&u8g2, 0, 15 * 3, &buf[0]);
    snprintf(&buf[0], 100, "19 count: %d", count * 19);
    u8g2_DrawStr(&u8g2, 0, 15 * 4, &buf[0]);
    u8g2_SendBuffer(&u8g2);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGD(tag, "All done!");

  vTaskDelete(NULL);
}
