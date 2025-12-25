#include "i2c_lcd.h"
#include "delay.h"
#include <stdint.h>
#include <stdio.h>
#include "stdarg.h"
#include <stdbool.h>
#include <string.h>

// --- Variabel Internal ---
static uint8_t _backlight_state = PCF_BL;
static uint8_t _lcd_initialized = 0;

// --- Custom Character untuk Bargraph ---
static uint8_t custom_chars[8][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0%
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // 12.5%
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}, // 25%
    {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C}, // 37.5%
    {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E}, // 50%
    {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, // 62.5%
    {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00}, // 75%
    {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00}  // 87.5%
};

// --- I2C Bus Recovery ---
void i2c_bus_reset(void) {
    uint8_t i = 0;
    
    gpio_mode_set(I2C_LCD_GPIO, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, I2C_LCD_SCL_PIN);
    gpio_output_options_set(I2C_LCD_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, I2C_LCD_SCL_PIN);
    
    gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SCL_PIN, SET);
    
    gpio_mode_set(I2C_LCD_GPIO, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, I2C_LCD_SDA_PIN);
    delay_us(10);
    
    if (gpio_input_bit_get(I2C_LCD_GPIO, I2C_LCD_SDA_PIN) == RESET) {
        for (i = 0; i < 9; i++) {
            gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SCL_PIN, RESET);
            delay_us(5);
            gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SCL_PIN, SET);
            delay_us(5);
        }
        
        gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SDA_PIN, RESET);
        delay_us(5);
        gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SCL_PIN, SET);
        delay_us(5);
        gpio_bit_write(I2C_LCD_GPIO, I2C_LCD_SDA_PIN, SET);
        delay_us(5);
    }
    
    i2c_software_reset_config(I2C_LCD_PERIPH, I2C_SRESET_RESET);
    i2c_software_reset_config(I2C_LCD_PERIPH, I2C_SRESET_SET);
    
    gpio_mode_set(I2C_LCD_GPIO, GPIO_MODE_AF, GPIO_PUPD_PULLUP, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);
    gpio_output_options_set(I2C_LCD_GPIO, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);
    gpio_af_set(I2C_LCD_GPIO, GPIO_AF_1, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);
    
    delay_ms(10);
}

uint8_t i2c_check_bus_status(void) {
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    
    while (i2c_flag_get(I2C_LCD_PERIPH, I2C_FLAG_I2CBSY)) {
        if (--timeout == 0) {
            i2c_bus_reset();
            return 1;
        }
    }
    return 0;
}

uint8_t i2c_wait_flag(uint32_t flag) {
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    
    while (!i2c_flag_get(I2C_LCD_PERIPH, flag)) {
        if (--timeout == 0) {
            i2c_check_bus_status();
            return 1;
        }
    }
    return 0;
}

// --- Fungsi I2C dengan Recovery ---
uint8_t lcd_write_pcf_with_recovery(uint8_t data) {
    uint8_t retry_count = 2; // Cukup 2 retry
    uint8_t result;
    
    do {
        if (i2c_check_bus_status()) {
            delay_ms(1);
        }
        
        i2c_start_on_bus(I2C_LCD_PERIPH);
        
        result = i2c_wait_flag(I2C_FLAG_SBSEND);
        if (result) {
            i2c_stop_on_bus(I2C_LCD_PERIPH);
            continue;
        }
        
        i2c_master_addressing(I2C_LCD_PERIPH, I2C_LCD_ADDR, I2C_TRANSMITTER);
        
        result = i2c_wait_flag(I2C_FLAG_ADDSEND);
        if (result) {
            i2c_stop_on_bus(I2C_LCD_PERIPH);
            continue;
        }
        i2c_flag_clear(I2C_LCD_PERIPH, I2C_FLAG_ADDSEND);
        
        i2c_data_transmit(I2C_LCD_PERIPH, data);
        
        result = i2c_wait_flag(I2C_FLAG_TBE);
        if (result) {
            i2c_stop_on_bus(I2C_LCD_PERIPH);
            continue;
        }
        
        result = i2c_wait_flag(I2C_FLAG_BTC);
        if (result) {
            i2c_stop_on_bus(I2C_LCD_PERIPH);
            continue;
        }
        
        i2c_stop_on_bus(I2C_LCD_PERIPH);
        return 0;
        
    } while (--retry_count > 0);
    
    i2c_bus_reset();
    return 1;
}

// --- Fungsi Internal LCD ---
static void lcd_send_4bits(uint8_t data_bits) {
    uint8_t output = data_bits | _backlight_state;
    
    lcd_write_pcf_with_recovery(output);
    
    output |= PCF_EN;
    lcd_write_pcf_with_recovery(output);
    
    delay_us(1);
    
    output &= ~PCF_EN;
    lcd_write_pcf_with_recovery(output);
    
    delay_us(50);
}

static void lcd_send_byte(uint8_t value, bool is_data) {
    uint8_t high_nibble = (value & 0xF0);
    uint8_t low_nibble = ((value & 0x0F) << 4);
    
    if (is_data) {
        high_nibble |= PCF_RS;
        low_nibble |= PCF_RS;
    }
    
    lcd_send_4bits(high_nibble);
    lcd_send_4bits(low_nibble);
}

static void lcd_create_custom_chars(void) {
    uint8_t i, j;
    
    lcd_send_command(LCD_SETCGRAMADDR | 0x00);
    
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            lcd_send_data(custom_chars[i][j]);
        }
    }
    
    lcd_send_command(LCD_SETDDRAMADDR);
}

// --- Fungsi API ---
void lcd_init(void) {
    if (_lcd_initialized) return;
    
    // Reset bus dulu
    i2c_bus_reset();
    
    // Enable clocks
    rcu_periph_clock_enable(I2C_LCD_GPIO_RCC);
    rcu_periph_clock_enable(I2C_LCD_RCC_RCC);
    
    // Configure GPIO
    gpio_mode_set(I2C_LCD_GPIO, GPIO_MODE_AF, GPIO_PUPD_PULLUP, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);
    gpio_output_options_set(I2C_LCD_GPIO, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);
    gpio_af_set(I2C_LCD_GPIO, GPIO_AF_1, I2C_LCD_SCL_PIN | I2C_LCD_SDA_PIN);

    // Configure I2C - 400kHz Fast Mode
    i2c_deinit(I2C_LCD_PERIPH);
    i2c_clock_config(I2C_LCD_PERIPH, 400000, I2C_DTCY_2);
    i2c_mode_addr_config(I2C_LCD_PERIPH, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0x00);
    i2c_enable(I2C_LCD_PERIPH);
    
    // Tunggu LCD stabil
    delay_ms(50);
    
    // Initialization sequence
    lcd_send_4bits(0x30);
    delay_ms(5);
    lcd_send_4bits(0x30);
    delay_us(100);
    lcd_send_4bits(0x30);
    delay_us(100);
    lcd_send_4bits(0x20); // 4-bit mode
    delay_us(100);
    
    // Function set: 4-bit, 2-line, 5x8
    lcd_send_command(0x28);
    delay_us(50);
    
    // Display off
    lcd_send_command(0x08);
    delay_us(50);
    
    // Clear display
    lcd_send_command(0x01);
    delay_ms(2);
    
    // Entry mode
    lcd_send_command(0x06);
    delay_us(50);
    
    // Display on, cursor off
    lcd_send_command(0x0C);
    delay_us(50);
    
    // Buat custom characters
    lcd_create_custom_chars();
    
    // Set backlight
    lcd_write_pcf_with_recovery(_backlight_state);
    
    _lcd_initialized = 1;
}

void lcd_clear(void) {
    lcd_send_command(LCD_CLEARDISPLAY);
    delay_ms(2);
}

void lcd_home(void) {
    lcd_send_command(LCD_RETURNHOME);
    delay_ms(2);
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    static const uint8_t row_offsets[] = {0x00, 0x40};
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    lcd_send_command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

void lcd_print_char(char c) {
    lcd_send_data((uint8_t)c);
}

void lcd_print_string(const char* str) {
    while (*str) {
        lcd_print_char(*str++);
    }
}

void lcd_send_command(uint8_t cmd) {
    lcd_send_byte(cmd, false);
}

void lcd_send_data(uint8_t data) {
    lcd_send_byte(data, true);
}

void lcd_set_backlight(bool state) {
    if (state) {
        _backlight_state |= PCF_BL;
    } else {
        _backlight_state &= ~PCF_BL;
    }
    lcd_write_pcf_with_recovery(_backlight_state);
}

// --- Fungsi Utilitas ---
void lcd_print_string_at(const char *str, uint8_t col, uint8_t row) {
    lcd_set_cursor(col, row);
    lcd_print_string(str);
}

void lcd_print_int(int value) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%d", value);
    lcd_print_string(buffer);
}

void lcd_print_float(float value, uint8_t decimals) {
    char buffer[16];
    char format[10];
    
    snprintf(format, sizeof(format), "%%.%df", decimals);
    snprintf(buffer, sizeof(buffer), format, value);
    lcd_print_string(buffer);
}

// --- Bargraph Functions ---
void lcd_draw_bargraph(float power_percent, uint8_t row, uint8_t col_start, uint8_t width) {
    uint8_t i;
    uint8_t full_chars, partial_char;
    float power = power_percent;
    
    if (power < 0.0f) power = 0.0f;
    if (power > 100.0f) power = 100.0f;
    
    float scaled_power = (power / 100.0f) * (width * 8.0f);
    full_chars = (uint8_t)(scaled_power / 8.0f);
    partial_char = (uint8_t)(scaled_power - (full_chars * 8.0f));
    
    lcd_set_cursor(col_start, row);
    
    for (i = 0; i < full_chars; i++) {
        if (i < width) {
            lcd_print_char(0x05); // Full block
        }
    }
    
    if (full_chars < width) {
        if (partial_char > 0) {
            uint8_t char_index = (partial_char - 1);
            lcd_print_char(char_index);
            full_chars++;
        } else {
            lcd_print_char(' ');
            full_chars++;
        }
    }
    
    for (i = full_chars; i < width; i++) {
        lcd_print_char(' ');
    }
}

void lcd_display_t12_info(float temp_actual, float temp_setpoint, float power_percent, uint8_t row) {
    char buffer[17];
    
    if (row == 0) {
        // Baris 1: T12: 245/350°C
        snprintf(buffer, sizeof(buffer), "T12:%3.0f/%-3.0f%cC", 
                temp_actual, temp_setpoint, 0xDF);
        buffer[15] = '\0';
        lcd_print_string_at(buffer, 0, 0);
    } else {
        // Baris 2: Power bargraph
        lcd_draw_bargraph(power_percent, 1, 0, 12);
        
        // Persentase
        snprintf(buffer, sizeof(buffer), "%3.0f%%", power_percent);
        lcd_print_string_at(buffer, 12, 1);
    }
}

void lcd_display_hotair_info(float temp_actual, float temp_setpoint, uint8_t row) {
    char buffer[17];
    
    snprintf(buffer, sizeof(buffer), "HA:%4.0f/%-4.0f%cC", 
            temp_actual, temp_setpoint, 0xDF);
    buffer[15] = '\0';
    lcd_print_string_at(buffer, 0, row);
}