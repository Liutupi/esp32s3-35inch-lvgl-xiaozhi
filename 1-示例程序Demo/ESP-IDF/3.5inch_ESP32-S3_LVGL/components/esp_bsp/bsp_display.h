#ifndef __BSP_DISPLAY_H__
#define __BSP_DISPLAY_H__

#include <stdio.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77922.h"

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

#define EXAMPLE_SPI_HOST SPI2_HOST

#define EXAMPLE_PIN_LCD_CS      GPIO_NUM_10
#define EXAMPLE_PIN_LCD_SCLK    GPIO_NUM_12
#define EXAMPLE_PIN_LCD_DATA0   GPIO_NUM_11
#define EXAMPLE_PIN_LCD_DATA1   GPIO_NUM_13
#define EXAMPLE_PIN_LCD_DATA2   GPIO_NUM_14
#define EXAMPLE_PIN_LCD_DATA3   GPIO_NUM_9

#define EXAMPLE_PIN_LCD_RST GPIO_NUM_NC
#define EXAMPLE_PIN_LCD_BL GPIO_NUM_41

#define LCD_BL_LEDC_TIMER LEDC_TIMER_0
#define LCD_BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define LCD_BL_LEDC_DUTY_RES LEDC_TIMER_10_BIT // Set duty resolution to 13 bits
#define LCD_BL_LEDC_DUTY (1024)                // Set duty to 50%. 1024 * 50% = 4096
#define LCD_BL_LEDC_FREQUENCY (5000)          // Frequency in Hertz. Set frequency at 5 kHz


#ifdef __cplusplus
extern "C" {
#endif

void bsp_display_init(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, size_t max_transfer_sz);
void bsp_display_brightness_init(void);
void bsp_display_set_brightness(uint8_t brightness);
uint8_t bsp_display_get_brightness(void);

#ifdef __cplusplus
}
#endif

#endif