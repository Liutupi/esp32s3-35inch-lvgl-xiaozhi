#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "esp_log.h"
#include "bsp_touch.h"
#include "bsp_i2c.h"

#define TOUCH_WIDTH      320         // 触摸区域宽度
#define TOUCH_HEIGHT     480         // 触摸区域高度
#define MAX_TOUCH_POINTS 10          // 最大支持触摸点数（硬件上限）

// 寄存器地址
#define FW_VER           0x0000      // 固件版本
#define STATUS           0x0001      // 状态寄存器
#define DEVICE_CON       0x0002      // 设备配置
#define MAX_X            0x0005      // X轴最大值
#define MAX_Y            0x0007      // Y轴最大值
#define MAX_TOUCHES      0x0009      // 最大触摸点数寄存器
#define TOUCH_INFO       0x0010      // 触摸信息寄存器
#define GESTURE_INFO     0x0012      // 手势信息寄存器
#define TOUCH_POINT0     0x0014      // 触摸点0数据
#define TOUCH_POINT1     0x001B      // 触摸点1数据（每点间隔7字节：0x14+7=0x1B）
#define TOUCH_POINT2     0x0022
#define TOUCH_POINT3     0x0029
#define TOUCH_POINT4     0x0030
#define TOUCH_POINT5     0x0037
#define TOUCH_POINT6     0x003E
#define TOUCH_POINT7     0x0045
#define TOUCH_POINT8     0x004C
#define TOUCH_POINT9     0x0053



static uint16_t g_rotation = 0;
static uint16_t g_width = 320;
static uint16_t g_height = 480;
static uint8_t  max_supported_points = 0;
static i2c_master_dev_handle_t dev_handle;

touch_data_t g_touch_data;

// I2C读写函数（向指定寄存器读取数据）
static esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *rbuf, size_t rlen)
{
    // 寄存器地址转换为2字节（高8位+低8位）
    uint8_t wbuf[2] = { (reg >> 8) & 0xFF, reg & 0xFF };
    return i2c_master_transmit_receive(dev, wbuf, 2, rbuf, rlen, pdMS_TO_TICKS(1000));
}

void bsp_touch_read(void)
{
    uint8_t touch_info = 0;
    uint8_t point_data[7 * MAX_TOUCH_POINTS] = {0}; // 每点7字节，最多10点
    esp_err_t err;

    // 锁定I2C总线（假设bsp_i2c_lock已实现）
    if (bsp_i2c_lock(0)) {
    

    // 1. 读取触摸信息寄存器（TOUCH_INFO），判断是否有触摸
    err = i2c_read_reg(dev_handle, TOUCH_INFO, &touch_info, 1);
    if (err != ESP_OK || !(touch_info & 0x08)) { // 0x08位表示有触摸数据
        g_touch_data.touch_num = 0;
        bsp_i2c_unlock();
        return;
    }

    // 2. 读取所有触摸点数据（从TOUCH_POINT0开始，共7*N字节）
    err = i2c_read_reg(dev_handle, TOUCH_POINT0, point_data, 7 * max_supported_points);
    bsp_i2c_unlock(); // 解锁总线

    if (err != ESP_OK) {
        g_touch_data.touch_num = 0;
        return;
    }

    // 3. 解析触摸点数据
    g_touch_data.touch_num = 0;
    for (int i = 0; i < max_supported_points; i++) {
        uint8_t base_idx = i * 7; // 每个触摸点占7字节（0x14到0x1A为点0，以此类推）
        
        // 检查触摸点有效性（第1字节bit7为1表示有效）
        if (!(point_data[base_idx] & 0x80)) {
            continue;
        }

        // 解析ID（第1字节bit6-bit0，实际为触摸点编号）
        g_touch_data.coords[g_touch_data.touch_num].id = point_data[base_idx] & 0x7F;

        // 解析X坐标（第1字节低6位 + 第2字节，共14位）
        g_touch_data.coords[g_touch_data.touch_num].x = ((point_data[base_idx] & 0x3F) << 8) | point_data[base_idx + 1];

        // 解析Y坐标（第3字节低6位 + 第4字节，共14位）
        g_touch_data.coords[g_touch_data.touch_num].y = ((point_data[base_idx + 2] & 0x3F) << 8) | point_data[base_idx + 3];

        // 触摸点数累加（不超过硬件上限）
        if (++g_touch_data.touch_num >= max_supported_points) {
            break;
        }
    }
    }
}

bool bsp_touch_get_coordinates(touch_data_t *touch_data)
{
    if (!touch_data || g_touch_data.touch_num == 0) {
        return false;
    }

    // 复制并转换坐标（适配旋转角度）
    for (int i = 0; i < g_touch_data.touch_num; i++) {
        uint16_t raw_x = g_touch_data.coords[i].x;
        uint16_t raw_y = g_touch_data.coords[i].y;

        switch (g_rotation) {
            case 1: // 90度旋转
                touch_data->coords[i].x = raw_y;
                touch_data->coords[i].y = g_height - raw_x;
                break;
            case 2: // 180度旋转
                touch_data->coords[i].x = g_width - raw_x;
                touch_data->coords[i].y = g_height - raw_y;
                break;
            case 3: // 270度旋转
                touch_data->coords[i].x = g_width - raw_y;
                touch_data->coords[i].y = raw_x;
                break;
            default: // 0度（默认方向）
                touch_data->coords[i].x = raw_x;
                touch_data->coords[i].y = raw_y;
                break;
        }
        touch_data->coords[i].id = g_touch_data.coords[i].id; // 保留ID
    }
    touch_data->touch_num = g_touch_data.touch_num;
    return true;
}

void bsp_touch_init(i2c_master_bus_handle_t bus_handle, uint16_t width, uint16_t height, uint16_t rotation)
{
    g_rotation = rotation;
    g_width = width;
    g_height = height;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ST77922_ADDRESS,
        .scl_speed_hz = 400000,
        // .flags.disable_ack_check = true,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
    //CTP_GPIO初始化
    // 配置RST引脚为推挽输出
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << EXAMPLE_PIN_TP_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE 
    };
    gpio_config(&rst_cfg);
    // 配置INT引脚为上拉输入
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << EXAMPLE_PIN_TP_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 上拉确保空闲时为高电平
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // 下降沿触发（低电平有效）
    };
    gpio_config(&int_cfg);

    //复位
    gpio_set_level(EXAMPLE_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EXAMPLE_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待芯片初始化完成
    //获取最大触摸点数
    uint8_t max_touches;
    if (i2c_read_reg(dev_handle, MAX_TOUCHES, &max_touches, 1) == ESP_OK) {
        max_supported_points = (max_touches > 0 && max_touches <= MAX_TOUCH_POINTS) ? max_touches : 1;
    } else {
        max_supported_points = 1; // 通信失败默认支持1点
    }
    ESP_LOGI("TOUCH", "Max supported points: %d", max_supported_points);

}
    


