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

#ifdef __cplusplus
}
#endif

#endif // OLED_PORT_H
