#ifndef I2C_LCD_H
#define I2C_LCD_H

#include <stdint.h>
#include "gd32f3x0.h"

// === Konfigurasi Pin & Periferal ===
#define I2C_LCD_PERIPH      I2C0
#define I2C_LCD_ADDR        0x4E

#define I2C_LCD_GPIO        GPIOB
#define I2C_LCD_SCL_PIN     GPIO_PIN_6
#define I2C_LCD_SDA_PIN     GPIO_PIN_7

#define I2C_LCD_GPIO_RCC    RCU_GPIOB
#define I2C_LCD_RCC_RCC     RCU_I2C0

// === Timeout Configuration ===
#define I2C_TIMEOUT_MS      100
#define I2C_TIMEOUT_COUNT   100000

// === PCF8574 ===
#define PCF_RS              (1U << 0)
#define PCF_RW              (1U << 1)
#define PCF_EN              (1U << 2)
#define PCF_BL              (1U << 3)

// === LCD ===
#define LCD_ROWS            2
#define LCD_COLS            16

// === HD44780 Commands ===
#define LCD_CLEARDISPLAY        0x01
#define LCD_RETURNHOME          0x02
#define LCD_ENTRYMODESET        0x04
#define LCD_DISPLAYCONTROL      0x08
#define LCD_CURSORSHIFT         0x10
#define LCD_FUNCTIONSET         0x20
#define LCD_SETCGRAMADDR        0x40
#define LCD_SETDDRAMADDR        0x80

#define LCD_8BITMODE            0x10
#define LCD_4BITMODE            0x00
#define LCD_2LINE               0x08
#define LCD_1LINE               0x00
#define LCD_5x10DOTS            0x04
#define LCD_5x8DOTS             0x00

#define LCD_DISPLAYON           0x04
#define LCD_DISPLAYOFF          0x00
#define LCD_CURSORON            0x02
#define LCD_CURSOROFF           0x00
#define LCD_BLINKON             0x01
#define LCD_BLINKOFF            0x00

#define LCD_ENTRYRIGHT          0x00
#define LCD_ENTRYLEFT           0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// === Fungsi API ===
void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_print_char(char c);
void lcd_print_string(const char* str);
void lcd_send_command(uint8_t cmd);
void lcd_send_data(uint8_t data);
void lcd_set_backlight(bool state);

// Fungsi utilitas
void lcd_print_string_at(const char *str, uint8_t col, uint8_t row);
void lcd_print_int(int value);
void lcd_print_float(float value, uint8_t decimals);

// I2C Bus Recovery
void i2c_bus_reset(void);
uint8_t i2c_check_bus_status(void);

// Bargraph Functions
void lcd_draw_bargraph(float power_percent, uint8_t row, uint8_t col_start, uint8_t width);
void lcd_display_t12_info(float temp_actual, float temp_setpoint, float power_percent, uint8_t row);
void lcd_display_hotair_info(float temp_actual, float temp_setpoint, uint8_t row);

#endif