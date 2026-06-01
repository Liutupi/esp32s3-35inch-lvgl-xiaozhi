#ifndef __BSP_TOUCH_H__
#define __BSP_TOUCH_H__

#include "driver/i2c_master.h"


#define EXAMPLE_PIN_TP_INT GPIO_NUM_47
#define EXAMPLE_PIN_TP_RST GPIO_NUM_48

#define MAX_TOUCH_MAX_POINTS    2

#define I2C_ST77922_ADDRESS    (0x55)

typedef struct{
    uint16_t x;
    uint16_t y;
}coords_t;

typedef struct {
    uint8_t touch_num;               // 实际触摸点数
    struct {
        uint16_t x;                  // X坐标
        uint16_t y;                  // Y坐标
        uint8_t id;                  // 触摸点ID
    } coords[10];      // 多点触摸坐标数组
} touch_data_t;



#ifdef __cplusplus
extern "C" {
#endif

void bsp_touch_init(i2c_master_bus_handle_t bus_handle, uint16_t width, uint16_t height, uint16_t rotation);
void bsp_touch_read(void);
bool bsp_touch_get_coordinates(touch_data_t *touch_data);
#ifdef __cplusplus
}
#endif


#endif