#ifndef HT1621_H
#define HT1621_H

#include <stdint.h>

// === Konfigurasi Pin (SESUAIKAN DENGAN BOARD ANDA) ===
#define HT_PORT        GPIOB
#define HT_CS          GPIO_PIN_12
#define HT_DATA        GPIO_PIN_13
#define HT_WR          GPIO_PIN_14

// === Konstanta Display ===
#define DIGIT_COUNT     6
#define BAR_LEVELS      6
#define SINGLE_SYMBOL_COUNT 4
#define PACKED_SYMBOL_COUNT 17

// === Mask untuk clear bar ===
#define LEFT_BAR_CLEAR_MASK  (0x01 | 0x04 | 0x02 | 0x10 | 0x20 | 0x40)
#define RIGHT_BAR_CLEAR_MASK (0x08 | 0x80 | 0x20 | 0x40 | 0x04 | 0x02)

// === Enum ===
typedef enum {
    BAR_LEFT = 0,
    BAR_RIGHT,
    BAR_BOTH
} bar_side_t;

// === Struktur ===
typedef struct {
    uint8_t address;
    uint8_t bit_mask;
} symbol_config_t;

typedef struct {
    uint8_t address;
    uint8_t bits[BAR_LEVELS];
} bar_segment_config_t;

// === Fungsi API ===
void ht1621_init(void);
void ht1621_clear_all(void);
void ht1621_clear_digit(void);
void ht1621_clear_symbol(void);

void display_set_digit(uint8_t position, uint8_t value);
void display_update_digits(void);

void display_set_symbol(uint8_t symbol_index, uint8_t state);
void display_update_symbols(void);
void display_update_all(void);

void bar_set(bar_side_t side, uint8_t level);
void bar_set_all(uint8_t left_level, uint8_t right_level);

void display_toggle_symbol(uint8_t symbol_index);
void display_set_symbols_bulk(const uint8_t* symbols, uint8_t count, uint8_t state);
void display_toggle_symbols_bulk(const uint8_t* symbols, uint8_t count);

void display_startup_animation(void);

#endif