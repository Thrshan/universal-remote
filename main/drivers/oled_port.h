#ifndef OLED_PORT_H
#define OLED_PORT_H

#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief u8g2 GPIO and delay callback for ESP32
 */
uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/**
 * @brief u8g2 I2C byte callback for ESP32
 */
uint8_t u8x8_byte_esp32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

// Match the working SH1106 examples. Its two-column RAM offset becomes
// two rows in portrait; the SSD1306 constructor leaves those rows unwritten.
static inline void oled_setup_portrait(u8g2_t *display,
                                      u8x8_msg_cb byte_cb, u8x8_msg_cb delay_cb)
{
    u8g2_Setup_sh1106_i2c_128x64_noname_f(display, U8G2_R1, byte_cb, delay_cb);
    u8g2_SetI2CAddress(display, 0x3C << 1);
}

#ifdef __cplusplus
}
#endif

#endif // OLED_PORT_H
