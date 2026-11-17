#include "touch.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_axs15231b.h"
#include "esp_log.h"

#include "board.h"

static const char *TAG = "touch";

static i2c_master_bus_handle_t s_bus;
static esp_lcd_touch_handle_t s_tp;
static bool s_last_down;

esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus init failed");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    io_cfg.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &tp_io), TAG,
                        "touch panel io init failed");

    esp_lcd_touch_config_t tp_cfg = {
        // Native portrait extents; the generic esp_lcd_touch layer applies the
        // mirror/swap transform below to produce landscape UI coordinates.
        .x_max = PANEL_NATIVE_WIDTH - 1,
        .y_max = PANEL_NATIVE_HEIGHT - 1,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &s_tp), TAG,
                        "AXS15231B touch init failed");
    ESP_LOGI(TAG, "AXS15231B touch ready at 0x%02X", TOUCH_ADDR);
    return ESP_OK;
}

bool touch_read(int *x, int *y)
{
    if (!s_tp) {
        return false;
    }
    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;
    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        return false;
    }
    if (esp_lcd_touch_get_data(s_tp, pt, &count, 1) != ESP_OK || count == 0) {
        return false;
    }
    // The transform can leave one-past-the-edge values; clamp to the UI.
    int tx = pt[0].x;
    int ty = pt[0].y;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= LCD_WIDTH) tx = LCD_WIDTH - 1;
    if (ty >= LCD_HEIGHT) ty = LCD_HEIGHT - 1;
    if (x) *x = tx;
    if (y) *y = ty;
    return true;
}

bool touch_tapped(int *x, int *y)
{
    int tx = 0, ty = 0;
    bool down = touch_read(&tx, &ty);
    bool tap = down && !s_last_down;
    s_last_down = down;
    if (tap) {
        if (x) *x = tx;
        if (y) *y = ty;
        return true;
    }
    return false;
}
