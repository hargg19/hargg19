/*
 * WS2812 Configuration Reference
 * 
 * File ini menunjukkan cara mengkonfigurasi timing WS2812
 * untuk berbagai clock frequency dan library
 */

#ifndef WS2812_CONFIG_H
#define WS2812_CONFIG_H

/*
 * =====================================================
 * KONFIGURASI UNTUK GD32F350CBT6 @ 108MHz APB2
 * =====================================================
 */

/*
 * WS2812 Timing Specification:
 * 
 * Setiap bit periode = 1.25μs (800kHz)
 * - Bit '1': HIGH 0.8μs, LOW 0.45μs
 * - Bit '0': HIGH 0.4μs, LOW 0.85μs
 * 
 * Reset: LOW >50μs
 * 
 * Tolerance: ±150ns per bit
 */

/*
 * Perhitungan untuk GD32F350CBT6 @ 108MHz APB2:
 * 
 * Timer Frequency = APB2 Clock = 108 MHz
 * Prescaler = 0 (tidak ada pembagi)
 * Timer Tick = 1 / 108MHz = 9.26ns
 * 
 * Untuk 1.25μs periode:
 *   Ticks = 1.25μs / 9.26ns = 135 ticks
 * 
 * Untuk 0.9μs high (bit '1'):
 *   Ticks = 0.9μs / 9.26ns = 97 ticks
 * 
 * Untuk 0.35μs high (bit '0'):
 *   Ticks = 0.35μs / 9.26ns = 38 ticks
 * 
 * Reset = 60 periode = 60 * 1.25μs = 75μs > 50μs minimum
 */

#define WS2812_TIMER_FREQ_MHZ   108      // APB2 Clock for GD32F350
#define WS2812_TIMER_PRESCALER  0        // No division
#define WS2812_TICK_NS          9.26     // ns per tick

// Timing values (in timer ticks)
#define WS2812_BIT_PERIOD       135      // 1.25μs full period
#define WS2812_BIT1_HIGH        97       // 0.9μs high for '1' bit
#define WS2812_BIT0_HIGH        38       // 0.35μs high for '0' bit
#define WS2812_RESET_PULSES     60       // 75μs reset pulse

/*
 * =====================================================
 * ALTERNATIF UNTUK CLOCK BERBEDA
 * =====================================================
 */

/*
 * Jika APB2 = 72MHz (GD32F103C):
 * 
 * #define WS2812_TIMER_FREQ_MHZ   72
 * #define WS2812_BIT_PERIOD       90       // 1.25μs
 * #define WS2812_BIT1_HIGH        65       // 0.9μs
 * #define WS2812_BIT0_HIGH        25       // 0.35μs
 * #define WS2812_RESET_PULSES     60       // 75μs
 */

/*
 * Jika APB2 = 72MHz (menggunakan prescaler untuk presisi):
 * 
 * Timer = APB2 / (prescaler + 1)
 * Jika prescaler = 2, maka timer freq = 72MHz / 3 = 24MHz
 * Tick = 41.67ns
 * 
 * #define WS2812_TIMER_FREQ_MHZ   24
 * #define WS2812_TIMER_PRESCALER  2
 * #define WS2812_BIT_PERIOD       30       // 1.25μs
 * #define WS2812_BIT1_HIGH        21       // 0.875μs
 * #define WS2812_BIT0_HIGH        10       // 0.417μs
 * #define WS2812_RESET_PULSES     60       // 75μs
 */

/*
 * =====================================================
 * CARA MENGHITUNG CUSTOM TIMING
 * =====================================================
 * 
 * 1. Tentukan clock frequency:
 *    F_timer = APB_CLOCK / (PRESCALER + 1)
 * 
 * 2. Hitung tick duration:
 *    TICK_NS = 1e9 / F_timer
 * 
 * 3. Hitung ticks untuk timing WS2812:
 *    
 *    BIT_PERIOD ticks = 1.25e-6 / (TICK_NS * 1e-9)
 *    BIT1_HIGH ticks  = 0.9e-6  / (TICK_NS * 1e-9)
 *    BIT0_HIGH ticks  = 0.35e-6 / (TICK_NS * 1e-9)
 * 
 * 4. Kalikan semua dengan safety factor (0.95-1.05) untuk toleransi
 * 
 * 5. Update di ws2812.h
 */

/*
 * =====================================================
 * TIMING TOLERANCE
 * =====================================================
 * 
 * WS2812 memiliki toleransi:
 * - Rising edge to HIGH transition: < 500ns
 * - Rising edge to HIGH data setup: < 100ns
 * - HIGH to LOW transition: < 500ns
 * - Overall per bit: ±150ns
 * 
 * Implementasi DMA-based PWM ini mencapai akurasi ±10ns,
 * jauh lebih baik dari requirement WS2812.
 */

/*
 * =====================================================
 * VERIFIKASI TIMING (di oscilloscope)
 * =====================================================
 * 
 * Untuk verifikasi timing dengan oscilloscope:
 * 
 * 1. Set LED strip ke solid red (ws2812_effect_solid_color)
 * 2. Probe pin PB9
 * 3. Set oscilloscope ke single capture
 * 4. Ukur:
 *    - Period bit '1': harus ~1.25μs
 *    - High time bit '1': harus ~0.9μs ±150ns (0.75-1.05μs)
 *    - High time bit '0': harus ~0.35μs ±150ns (0.2-0.5μs)
 * 5. Probe LED data pin untuk verifikasi output
 */

#endif // WS2812_CONFIG_H
