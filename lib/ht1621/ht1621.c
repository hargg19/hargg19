#include "ht1621.h"
#include "gd32f3x0.h"
#include "delay.h" // untuk delay_us(), delay_ms()
#include <string.h> // untuk memset

// Pastikan pin HT1621 didefinisikan di ht1621.h, contoh:
// #define HT_PORT        GPIOB
// #define HT_CS          GPIO_PIN_0
// #define HT_DATA        GPIO_PIN_1
// #define HT_WR          GPIO_PIN_2

// === Fungsi pembantu (harus didefinisikan di .c atau .h) ===
static inline uint8_t symbol_config_mask(uint8_t addr) {
    // Sesuaikan dengan simbol yang overlap digit
    if (addr == 18 || addr == 16 || addr == 12 || addr == 10) return 0x01;
    return 0x00;
}

// --- Definisi Internal (Tidak berubah) ---
static const uint8_t seg_table[11] = {
    0xFA, 0x60, 0xD6, 0xF4, 0x6C, 0xBC, 0xBE, 0xE0, 0xFE, 0xFC, 0x00
};

static const uint8_t digit_addr_map[DIGIT_COUNT] = {18, 16, 14, 12, 10, 8};

static const symbol_config_t symbol_config[SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT] = {
    // Single symbols
    {18, 0x01}, {16, 0x01}, {12, 0x01}, {10, 0x01},
    // Packed symbols (Address 6)
    {6, 0x80}, {6, 0x40}, {6, 0x20}, {6, 0x08}, {6, 0x04}, {6, 0x02}, {6, 0x01},
    // Packed symbols (Address 2)
    {2, 0x80}, {2, 0x40}, {2, 0x20}, {2, 0x10}, {2, 0x08}, {2, 0x04}, {2, 0x02}, {2, 0x01},
    // Packed symbols (Address 0)
    {0, 0x80}, {0, 0x40}
};

static const bar_segment_config_t bar_segments[2] = {
    { 0, {0x01, 0x04, 0x02, 0x10, 0x20, 0x40} }, // Left Bar
    { 4, {0x08, 0x80, 0x20, 0x40, 0x04, 0x02} }  // Right Bar
};

// --- Buffer Internal ---
static uint8_t digit_buffer[DIGIT_COUNT];
static uint8_t symbol_buffer[32];
static uint8_t digit_shadow[DIGIT_COUNT];
static uint8_t symbol_shadow[32];
//static volatile bool adc_ready = FALSE;

// --- Core HT1621 Communication ---
void ht1621_gpio_init(void) {
    rcu_periph_clock_enable(RCU_GPIOB); // Sesuaikan port jika perlu

    // Konfigurasi CS, DATA, WR sebagai output push-pull 50MHz
    gpio_mode_set(HT_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, HT_CS | HT_DATA | HT_WR);
    gpio_output_options_set(HT_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, HT_CS | HT_DATA | HT_WR);

    // Set semua pin HIGH (idle state)
    gpio_bit_write(HT_PORT, HT_CS, SET);
    gpio_bit_write(HT_PORT, HT_DATA, SET);
    gpio_bit_write(HT_PORT, HT_WR, SET);
}

void ht1621_wrdata(uint8_t data, uint8_t bits) {
    for (uint8_t i = 0; i < bits; i++) {
        gpio_bit_write(HT_PORT, HT_WR, RESET);
        delay_us(2);
        if (data & 0x80) {
            gpio_bit_write(HT_PORT, HT_DATA, SET);
        } else {
            gpio_bit_write(HT_PORT, HT_DATA, RESET);
        }
        delay_us(1);
        gpio_bit_write(HT_PORT, HT_WR, SET);
        delay_us(2);
        data <<= 1;
    }
}

void ht1621_write_data(uint8_t address, uint8_t data) {
    gpio_bit_write(HT_PORT, HT_CS, RESET);
    ht1621_wrdata(0xA0, 3);          // Write mode
    ht1621_wrdata(address << 2, 6);  // Address (6-bit, dikali 4)
    ht1621_wrdata(data, 8);          // Data
    gpio_bit_write(HT_PORT, HT_CS, SET);
    delay_us(1);
}

void ht1621_send_command(uint8_t cmd) {
    gpio_bit_write(HT_PORT, HT_CS, RESET);
    ht1621_wrdata(0x80, 4); // Command mode
    ht1621_wrdata(cmd, 8);
    gpio_bit_write(HT_PORT, HT_CS, SET);
}

// --- Initialization ---
void ht1621_init(void) {
    ht1621_gpio_init();
    ht1621_send_command(0x52); // 1/3 bias, 4 commons
    ht1621_send_command(0x30); // System clock 256kHz
    ht1621_send_command(0x08); // Disable system timer
    ht1621_send_command(0x0A); // Disable watchdog
    ht1621_send_command(0x02); // Enable system
    ht1621_send_command(0x06); // Turn on LCD

    // Clear bar segments in buffer
    symbol_buffer[0] &= ~LEFT_BAR_CLEAR_MASK;
    symbol_buffer[4] &= ~RIGHT_BAR_CLEAR_MASK;

    ht1621_clear_all();
    delay_us(4);
}

void display_update_all(void) {
    display_update_digits();
    display_update_symbols();
}

// --- Clear Functions ---
void ht1621_clear_all(void) {
    memset(digit_buffer, 0, sizeof(digit_buffer));
    memset(symbol_buffer, 0, sizeof(symbol_buffer));
    memset(digit_shadow, 0xFF, sizeof(digit_shadow));
    memset(symbol_shadow, 0xFF, sizeof(symbol_shadow));
    display_update_all();
}

void ht1621_clear_digit(void) {
    memset(digit_buffer, 0, sizeof(digit_buffer));
    memset(digit_shadow, 0xFF, sizeof(digit_shadow));
    display_update_digits();
}

void ht1621_clear_symbol(void) {
    memset(symbol_buffer, 0, sizeof(symbol_buffer));
    memset(symbol_shadow, 0xFF, sizeof(symbol_shadow));
    display_update_symbols();
}

// --- Digit & Symbol Functions (TIDAK BERUBAH) ---
// Semua fungsi berikut: display_set_digit, display_update_digits,
// display_set_symbol, display_update_symbols, bar_set, dll.
// TIDAK mengandung libopencm3 ? TIDAK PERLU DIUBAH.

void display_set_digit(uint8_t position, uint8_t value) {
    if (position < DIGIT_COUNT) {
        uint8_t segment_data = seg_table[value % 10];
        digit_buffer[position] = segment_data;
    }
}

void display_update_digits(void) {
    for (uint8_t i = 0; i < DIGIT_COUNT; i++) {
        uint8_t addr = digit_addr_map[i];
        uint8_t current_digit_seg = digit_buffer[i];
        uint8_t current_symbol_part = symbol_buffer[addr] & symbol_config_mask(addr);
        uint8_t merged_data = (current_digit_seg & ~symbol_config_mask(addr)) | current_symbol_part;

        if (merged_data != symbol_shadow[addr]) {
            ht1621_write_data(addr, merged_data);
            digit_shadow[i] = current_digit_seg;
            symbol_shadow[addr] = merged_data;
        }
    }
}

void display_set_symbol(uint8_t symbol_index, uint8_t state) {
    if (symbol_index < (SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT)) {
        uint8_t addr = symbol_config[symbol_index].address;
        uint8_t mask = symbol_config[symbol_index].bit_mask;

        if (state) {
            symbol_buffer[addr] |= mask;
        } else {
            symbol_buffer[addr] &= ~mask;
        }
    }
}

void display_update_symbols(void) {
    const uint8_t used_addresses[] = {0, 2, 4, 6, 10, 12, 16, 18};
    for (uint8_t j = 0; j < sizeof(used_addresses); j++) {
        uint8_t addr = used_addresses[j];
        uint8_t merged_data = symbol_buffer[addr];
        for (uint8_t i = 0; i < DIGIT_COUNT; i++) {
            if (digit_addr_map[i] == addr) {
                uint8_t digit_part = digit_buffer[i] & ~symbol_config_mask(addr);
                merged_data = digit_part | symbol_buffer[addr];
                break;
            }
        }

        if (merged_data != symbol_shadow[addr]) {
            ht1621_write_data(addr, merged_data);
            symbol_shadow[addr] = merged_data;
        }
    }
}

void bar_set(bar_side_t side, uint8_t level) {
    if (level > BAR_LEVELS) level = BAR_LEVELS;

    if (side == BAR_LEFT || side == BAR_BOTH) {
        uint8_t addr = bar_segments[0].address;
        symbol_buffer[addr] &= ~LEFT_BAR_CLEAR_MASK;
        for (uint8_t i = 0; i < level; i++) {
            symbol_buffer[addr] |= bar_segments[0].bits[i];
        }
    }
    if (side == BAR_RIGHT || side == BAR_BOTH) {
        uint8_t addr = bar_segments[1].address;
        symbol_buffer[addr] &= ~RIGHT_BAR_CLEAR_MASK;
        for (uint8_t i = 0; i < level; i++) {
            symbol_buffer[addr] |= bar_segments[1].bits[i];
        }
    }
}

void bar_set_all(uint8_t left_level, uint8_t right_level) {
    bar_set(BAR_LEFT, left_level);
    bar_set(BAR_RIGHT, right_level);
}

void display_toggle_symbol(uint8_t symbol_index) {
    if (symbol_index < (SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT)) {
        uint8_t addr = symbol_config[symbol_index].address;
        uint8_t mask = symbol_config[symbol_index].bit_mask;
        symbol_buffer[addr] ^= mask;
    }
}

void display_set_symbols_bulk(const uint8_t* symbols, uint8_t count, uint8_t state) {
    for (uint8_t i = 0; i < count; i++) {
        uint8_t symbol_index = symbols[i];
        if (symbol_index >= (SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT)) continue;
        uint8_t addr = symbol_config[symbol_index].address;
        uint8_t mask = symbol_config[symbol_index].bit_mask;
        if (state) {
            symbol_buffer[addr] |= mask;
        } else {
            symbol_buffer[addr] &= ~mask;
        }
    }
}

void display_toggle_symbols_bulk(const uint8_t* symbols, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        uint8_t symbol_index = symbols[i];
        if (symbol_index >= (SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT)) continue;
        uint8_t addr = symbol_config[symbol_index].address;
        uint8_t mask = symbol_config[symbol_index].bit_mask;
        symbol_buffer[addr] ^= mask;
    }
}

void display_startup_animation(void) {
    for (uint8_t i = 0; i < (SINGLE_SYMBOL_COUNT + PACKED_SYMBOL_COUNT); i++) {
        uint8_t addr = symbol_config[i].address;
        uint8_t mask = symbol_config[i].bit_mask;
        symbol_buffer[addr] |= mask;
    }
    display_update_symbols();
    delay_ms(500);

    for (int8_t value = 9; value >= 0; value--) {
        uint8_t seg = seg_table[value];
        for (uint8_t pos = 0; pos < DIGIT_COUNT; pos++) {
            digit_buffer[pos] = seg;
        }
        display_update_digits();

        uint8_t bar_level = (value > 5) ? 6 : value;
        bar_set(BAR_LEFT, bar_level);
        bar_set(BAR_RIGHT, bar_level);
        display_update_symbols();

        delay_ms(200);
    }
    ht1621_clear_all();
}