#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

namespace board::pins {

constexpr gpio_num_t kQspiCs = GPIO_NUM_45;
constexpr gpio_num_t kQspiPclk = GPIO_NUM_47;
constexpr gpio_num_t kQspiData0 = GPIO_NUM_21;
constexpr gpio_num_t kQspiData1 = GPIO_NUM_48;
constexpr gpio_num_t kQspiData2 = GPIO_NUM_40;
constexpr gpio_num_t kQspiData3 = GPIO_NUM_39;
constexpr gpio_num_t kQspiRst = GPIO_NUM_NC;
constexpr gpio_num_t kQspiTe = GPIO_NUM_38;
constexpr gpio_num_t kBacklight = GPIO_NUM_1;

constexpr gpio_num_t kTouchScl = GPIO_NUM_8;
constexpr gpio_num_t kTouchSda = GPIO_NUM_4;
constexpr int kTouchRst = -1;
constexpr int kTouchInt = -1;

constexpr spi_host_device_t kLcdSpiHost = SPI2_HOST;
constexpr i2c_port_t kTouchI2cPort = I2C_NUM_0;
constexpr uint32_t kTouchI2cHz = 400000;

/** Native panel GRAM resolution. */
constexpr uint16_t kPanelHRes = 320;
constexpr uint16_t kPanelVRes = 480;

/** Logical LVGL resolution (landscape; flush remaps 90° CW). */
constexpr uint16_t kLcdHRes = 480;
constexpr uint16_t kLcdVRes = 320;

constexpr uint8_t kLcdBitsPerPixel = 16;

}  // namespace board::pins
