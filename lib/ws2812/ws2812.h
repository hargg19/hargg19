#ifndef __WS2812_H__
#define __WS2812_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "gd32f3x0.h"

// =============================================
// Konfigurasi Hardware untuk GD32F350
// =============================================
#define WS2812_MAX_LEDS        20     // Maksimum jumlah LED yang didukung

// Pin dan Port untuk WS2812 - PB9 (TIMER16_CH0)
#define WS2812_GPIO            GPIOB
#define WS2812_PIN             GPIO_PIN_9
#define WS2812_PIN_NUM         9
#define WS2812_GPIO_RCC        RCU_GPIOB

// Timer untuk DMA trigger (menggunakan BOP GPIO)
#define WS2812_TIMER           TIMER16
#define WS2812_TIMER_RCC       RCU_TIMER16

// DMA channel untuk transfer data
// TIMER16 menggunakan DMA Channel 3 untuk Update event
#define WS2812_DMA             DMA1
#define WS2812_DMA_CHANNEL     DMA_CH3     // <-- Perbaikan
#define WS2812_DMA_RCC         RCU_DMA1
#define WS2812_DMA_IRQn        DMA_Channel3_4_IRQn  // <-- Perbaikan

// =============================================
// Konfigurasi Timing WS2812 untuk GD32F350
// =============================================
// GD32F350 APB2 Clock: 108 MHz (TIMER16 clock = 108 MHz)
// WS2812 membutuhkan:
// - BIT_PERIOD: ~1.25us (full bit time) = 135 ticks @ 108MHz
// - BIT1_HIGH: ~0.9us (high time untuk '1') = 97 ticks
// - BIT0_HIGH: ~0.35us (high time untuk '0') = 38 ticks

#define WS2812_BIT_PERIOD      135    // ~1.25us (full period)
#define WS2812_BIT1_HIGH       97     // ~0.9us high (untuk bit '1')
#define WS2812_BIT0_HIGH       38     // ~0.35us high (untuk bit '0')

// =============================================
// Tipe Data
// =============================================

// Struktur warna (format GRB untuk WS2812)
typedef struct {
    uint8_t g;      // Green
    uint8_t r;      // Red
    uint8_t b;      // Blue
} ws2812_color_t;

// Jenis efek yang tersedia
typedef enum {
    WS2812_EFFECT_OFF = 0,
    WS2812_EFFECT_SOLID_COLOR,
    WS2812_EFFECT_RAINBOW,
    WS2812_EFFECT_BREATHING,
    WS2812_EFFECT_METEOR_CENTER_DUAL,
    WS2812_EFFECT_WAVE_PING_PONG,
    WS2812_EFFECT_MAX
} ws2812_effect_t;

// =============================================
// Fungsi Deklarasi External
// =============================================

/**
 * @brief Fungsi untuk mendapatkan waktu dalam milidetik
 * @return Waktu saat ini dalam milidetik
 * @note Harus diimplementasikan di aplikasi utama (misal: menggunakan SysTick)
 */
extern uint32_t get_millis(void);

// =============================================
// Fungsi Inisialisasi dan Konfigurasi
// =============================================

/**
 * @brief Inisialisasi driver WS2812
 * @param num_leds Jumlah LED yang akan dikontrol (maks WS2812_MAX_LEDS)
 */
void ws2812_init(uint16_t num_leds);

/**
 * @brief Mengatur kecerahan global (0-255)
 * @param brightness Nilai kecerahan (0 = mati, 255 = maksimum)
 */
void ws2812_set_brightness(uint8_t brightness);

/**
 * @brief Mendapatkan nilai kecerahan saat ini
 * @return Nilai kecerahan (0-255)
 */
uint8_t ws2812_get_brightness(void);

/**
 * @brief Mendapatkan jumlah LED yang dikonfigurasi
 * @return Jumlah LED
 */
uint16_t ws2812_get_count(void);

// =============================================
// Fungsi Kontrol LED Dasar
// =============================================

/**
 * @brief Mengatur warna LED individu
 * @param index Indeks LED (0-based)
 * @param color Warna yang diinginkan
 */
void ws2812_set_pixel(uint16_t index, ws2812_color_t color);

/**
 * @brief Mengatur semua LED dengan warna yang sama
 * @param color Warna yang diinginkan
 */
void ws2812_set_all(ws2812_color_t color);

/**
 * @brief Mematikan semua LED
 */
void ws2812_clear_all(void);

/**
 * @brief Mengirim data ke LED strip (non-blocking)
 * @note Fungsi ini menggunakan DMA, jangan panggil berulang kali terlalu cepat
 */
void ws2812_update(void);

/**
 * @brief Mengecek apakah transfer DMA sedang berjalan
 * @return true jika sedang transfer, false jika siap
 */
bool ws2812_is_busy(void);

// =============================================
// Fungsi Generator Warna
// =============================================

/**
 * @brief Membuat warna dari komponen RGB
 * @param red Nilai merah (0-255)
 * @param green Nilai hijau (0-255)
 * @param blue Nilai biru (0-255)
 * @return Warna dalam format WS2812 (GRB)
 */
ws2812_color_t ws2812_color_rgb(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Membuat warna dari model HSV
 * @param hue Hue (0-255)
 * @param saturation Saturasi (0-255)
 * @param value Value/Brightness (0-255)
 * @return Warna dalam format WS2812 (GRB)
 */
ws2812_color_t ws2812_color_hsv(uint8_t hue, uint8_t saturation, uint8_t value);

/**
 * @brief Mengurangi kecerahan warna
 * @param color Warna yang akan dikurangi kecerahannya
 * @param brightness Tingkat kecerahan (0-255)
 * @return Warna dengan kecerahan yang dikurangi
 */
ws2812_color_t ws2812_color_dim(ws2812_color_t color, uint8_t brightness);

/**
 * @brief Generator warna dari color wheel (0-255)
 * @param wheel_pos Posisi di wheel (0-255)
 * @return Warna yang sesuai posisi wheel
 */
ws2812_color_t ws2812_color_wheel(uint8_t wheel_pos);

// =============================================
// Fungsi Efek Animasi
// =============================================

/**
 * @brief Efek pelangi (berputar)
 * @param speed Kecepatan animasi (semakin kecil semakin cepat)
 */
void ws2812_effect_rainbow(uint32_t speed);

/**
 * @brief Efek breathing (naik turun intensitas)
 * @param color Warna dasar
 * @param speed Kecepatan animasi (semakin kecil semakin cepat)
 */
void ws2812_effect_breathing(ws2812_color_t color, uint32_t speed);

/**
 * @brief Efek meteor dari tengah ke kedua sisi
 * @param color Warna meteor
 * @param speed Kecepatan animasi (semakin kecil semakin cepat)
 */
void ws2812_effect_meteor_center_dual(ws2812_color_t color, uint32_t speed);

/**
 * @brief Efek gelombang ping-pong dari tengah
 * @param color Warna gelombang
 * @param speed Kecepatan animasi (semakin kecil semakin cepat)
 */
void ws2812_effect_ping_pong_wave(ws2812_color_t color, uint32_t speed);

/**
 * @brief Efek warna solid (statis)
 * @param color Warna yang diinginkan
 */
void ws2812_effect_solid_color(ws2812_color_t color);

/**
 * @brief Matikan semua LED (efek off)
 */
void ws2812_effect_off(void);

/**
 * @brief Mendapatkan efek yang sedang aktif
 * @return Jenis efek saat ini
 */
ws2812_effect_t ws2812_get_current_effect(void);

// =============================================
// Fungsi Internal (untuk interupsi DMA)
// =============================================

/**
 * @brief Handler interupsi DMA Channel 3-4
 * @note Panggil dari vector table atau startup file
 * @note Untuk GD32F350, TIMER16 menggunakan DMA Channel 3
 */
void DMA_Channel3_4_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_H__ */