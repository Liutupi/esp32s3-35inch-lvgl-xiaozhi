#include <stdio.h>

#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_touch.h"

#include "lv_port.h"
#include "app_ui.h"
#include "app_net.h"
#include "app_time_weather.h"
#include "app_radio.h"
#include "app_audio.h"


#define EXAMPLE_DISPLAY_ROTATION LV_DISP_ROT_90
#define EXAMPLE_LCD_H_RES 320
#define EXAMPLE_LCD_V_RES 480
#define LCD_BUFFER_SIZE EXAMPLE_LCD_H_RES *EXAMPLE_LCD_V_RES

esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;

lv_disp_drv_t disp_drv;

static lv_disp_t *lvgl_disp;
static lv_indev_t *lvgl_touch_indev = NULL;

void lv_port_init(void);


extern "C" void app_main(void)
{

    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_init();

    bsp_display_init(&io_handle, &panel_handle, LCD_BUFFER_SIZE);
    
    bsp_display_brightness_init();
    bsp_display_set_brightness(100);
    bsp_touch_init(i2c_bus_handle, EXAMPLE_LCD_V_RES,EXAMPLE_LCD_H_RES , 1);
    lv_port_init();


    
    if (lvgl_port_lock(0))
    {
        // lv_demo_benchmark();
        // lv_demo_music();
        app_ui_create();
        lvgl_port_unlock();
    }

    app_net_start();
    app_audio_start(i2c_bus_handle);
    app_time_weather_start();
    app_radio_start();
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static lv_coord_t start_x = 0;
    static lv_coord_t start_y = 0;
    static bool was_pressed = false;
    touch_data_t touch_data;
    /*Save the pressed coordinates and the state*/
    bsp_touch_read();
    if (bsp_touch_get_coordinates(&touch_data))
    {
        last_x = touch_data.coords[0].x;
        last_y = touch_data.coords[0].y;
        if (!was_pressed) {
            start_x = last_x;
            start_y = last_y;
            was_pressed = true;
        }
        data->state = LV_INDEV_STATE_PR;
        app_ui_touch_update(last_x, last_y, touch_data.touch_num);
        //printf("x: %d, y: %d\n", last_x, last_y);
    }
    else
    {
        if (was_pressed) {
            app_ui_handle_swipe(last_x - start_x, last_y - start_y);
            was_pressed = false;
        }
        data->state = LV_INDEV_STATE_REL;
    }
    /*Set the last pressed coordinates*/
    data->point.x = last_x;
    data->point.y = last_y;
}

void lv_port_init(void)
{
    lvgl_port_cfg_t port_cfg = {};

    port_cfg.task_priority = 4;
    port_cfg.task_stack = 1024 * 5;
    port_cfg.task_affinity = 1;
    port_cfg.task_max_sleep_ms = 500;
    port_cfg.timer_period_ms = 5;
    lvgl_port_init(&port_cfg);

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = io_handle;
    disp_cfg.panel_handle = panel_handle;
    disp_cfg.buffer_size = LCD_BUFFER_SIZE;
    disp_cfg.sw_rotate = EXAMPLE_DISPLAY_ROTATION;
    disp_cfg.hres = EXAMPLE_LCD_H_RES;
    disp_cfg.vres = EXAMPLE_LCD_V_RES;
    disp_cfg.trans_size = LCD_BUFFER_SIZE / 10;
    disp_cfg.draw_wait_cb = NULL;
    disp_cfg.flags.buff_dma = false;
    disp_cfg.flags.buff_spiram = true;

    if (disp_cfg.sw_rotate == LV_DISP_ROT_180 || disp_cfg.sw_rotate == LV_DISP_ROT_NONE)
    {
        disp_cfg.hres = EXAMPLE_LCD_H_RES;
        disp_cfg.vres = EXAMPLE_LCD_V_RES;
    }
    else
    {
        disp_cfg.hres = EXAMPLE_LCD_V_RES;
        disp_cfg.vres = EXAMPLE_LCD_H_RES;
    }
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lvgl_touch_indev = lv_indev_drv_register(&indev_drv);
}
