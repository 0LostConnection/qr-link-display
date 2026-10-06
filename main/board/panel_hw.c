#include "board/panel_hw.h"

#include "board/lcd_init_cmds.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "board.panel_hw";

#define PIN_QSPI_CS      GPIO_NUM_45
#define PIN_QSPI_PCLK    GPIO_NUM_47
#define PIN_QSPI_DATA0   GPIO_NUM_21
#define PIN_QSPI_DATA1   GPIO_NUM_48
#define PIN_QSPI_DATA2   GPIO_NUM_40
#define PIN_QSPI_DATA3   GPIO_NUM_39
#define PIN_QSPI_RST     GPIO_NUM_NC
#define PIN_TOUCH_SCL    GPIO_NUM_8
#define PIN_TOUCH_SDA    GPIO_NUM_4

#define LCD_H_RES        320
#define LCD_V_RES        480
#define LCD_SPI_HOST     SPI2_HOST
#define TOUCH_I2C_PORT   I2C_NUM_0
#define TOUCH_I2C_HZ     400000

esp_err_t board_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel)
{
    ESP_RETURN_ON_FALSE(io && panel, ESP_ERR_INVALID_ARG, TAG, "null");

    const size_t max_transfer = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    const spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        PIN_QSPI_PCLK, PIN_QSPI_DATA0, PIN_QSPI_DATA1, PIN_QSPI_DATA2, PIN_QSPI_DATA3, max_transfer);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_config = AXS15231B_PANEL_IO_QSPI_CONFIG(PIN_QSPI_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, io), TAG,
                        "panel io");

    const axs15231b_vendor_config_t vendor_config = {
        .init_cmds = board_lcd_init_cmds,
        .init_cmds_size = (uint16_t)board_lcd_init_cmds_size,
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_QSPI_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_axs15231b(*io, &panel_config, panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "init");
    /* Driver treats the bool as "off"; false → DISPON (vendor BSP quirk). */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, false), TAG, "disp on");

    return ESP_OK;
}

esp_err_t board_touch_create(esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, TAG, "null");

    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_TOUCH_SDA,
        .scl_io_num = PIN_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master = {
            .clk_speed = TOUCH_I2C_HZ,
        },
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(TOUCH_I2C_PORT, &conf), TAG, "i2c cfg");
    ESP_RETURN_ON_ERROR(i2c_driver_install(TOUCH_I2C_PORT, conf.mode, 0, 0, 0), TAG, "i2c install");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_I2C_PORT, &tp_io_config, &tp_io), TAG,
        "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            /* Match landscape 90° CW flush remap. */
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, tp), TAG, "touch drv");
    return ESP_OK;
}
