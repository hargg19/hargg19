#include "gd32f3x0.h"
#include "delay.h"
#include "ht1621.h"
#include "fuzzy_pid.h"
#include "adc_sensor.h"
#include "pwm_timer0.h"
#include "i2c_lcd.h"
#include "buzzer.h"
#include "ws2812.h"
#include <math.h>

// Variabel global
static fuzzy_pid_t g_t12_pid;
static float g_setpoint = 280.0f;
static float g_t12_power = 0.0f;
static uint8_t g_led_count = 8; // Jumlah LED WS2812

// Prototipe task
void control_task(void);
void display_task(void);
void led_blink_task(void);
void lcd_update_task(void);
void ws2812_task(void);
void buzzer_test_task(void);

int main(void) {
    SystemInit();

    // Enable debug
    dbg_periph_enable(DBG_LOW_POWER_DEEPSLEEP);
    dbg_periph_enable(DBG_LOW_POWER_SLEEP);
    dbg_periph_enable(DBG_LOW_POWER_STANDBY);

    // Inisialisasi GPIO LED (PC13)
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_13);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_13);
    gpio_bit_write(GPIOC, GPIO_PIN_13, SET);

    // Inisialisasi semua modul
    delay_init();
    
    // Test LCD
    lcd_init();
    lcd_clear();
    lcd_print_string_at("WS2812 Test", 0, 0);
    lcd_print_string_at("PB9 TIMER1 DMA", 0, 1);
    delay_ms(2000);
    
    // Inisialisasi WS2812
    ws2812_init(g_led_count);
    ws2812_set_brightness(50); // 50% brightness untuk testing
    
    // Test sederhana: Set semua LED merah
    ws2812_set_all(ws2812_color_rgb(255, 0, 0));
    ws2812_update();
    delay_ms(1000);
    
    // Test: Set semua LED hijau
    ws2812_set_all(ws2812_color_rgb(0, 255, 0));
    ws2812_update();
    delay_ms(1000);
    
    // Test: Set semua LED biru
    ws2812_set_all(ws2812_color_rgb(0, 0, 255));
    ws2812_update();
    delay_ms(1000);
    
    // Matikan LED
    ws2812_clear_all();
    ws2812_update();
    
    // Inisialisasi Buzzer
    buzzer_init();
    
    // Inisialisasi lainnya
    ht1621_init();
    display_startup_animation();
    pwm_timer0_init();
    adc_sensor_init();
    adc_sensor_start();
    
    // Inisialisasi Fuzzy-PID
    fuzzy_pid_init(&g_t12_pid, MODE_SOLDER_T12);
    fuzzy_pid_set_setpoint(&g_t12_pid, g_setpoint);

    // Enable FPU
    SCB->CPACR |= ((3UL << 10*2) | (3UL << 11*2));

    // Jalankan task
    task_start_priority(control_task, 5, TASK_PRIORITY_HIGH);      // 100 Hz
    task_start_priority(display_task, 100, TASK_PRIORITY_NORMAL);  // 10 Hz
    task_start_priority(lcd_update_task, 250, TASK_PRIORITY_LOW);  // 4 Hz
    task_start_priority(ws2812_task, 25, TASK_PRIORITY_LOW);  // 40 Hz untuk WS2812
    task_start_priority(buzzer_test_task, 10, TASK_PRIORITY_LOW);  // 100 Hz untuk buzzer
    task_start_priority(led_blink_task, 500, TASK_PRIORITY_LOW);   // 2 Hz

    // Clear LCD untuk operasi normal
    lcd_clear();

    while (1) {
        task_scheduler_run();
        __WFI();
    }
}

// Task WS2812 Test
// Task untuk kontrol WS2812
void ws2812_task(void) {
    static uint32_t effect_timer = 0;
    static uint8_t effect_index = 0;
    
    effect_timer++;
    
    // Ganti efek setiap 10 detik
    if (effect_timer % 400 == 0) {  // 10 detik (400 * 25ms)
        effect_index = (effect_index + 1) % 6;
        
        switch (effect_index) {
            case 0:
                ws2812_effect_rainbow(5);
                break;
            case 1:
                ws2812_effect_breathing(ws2812_color_rgb(255, 0, 0), 20); // Red breathing
                break;
            case 2:
                ws2812_effect_meteor_center_dual(ws2812_color_rgb(0, 255, 0), 30); // Green meteor
                break;
            case 3:
                ws2812_effect_ping_pong_wave(ws2812_color_rgb(0, 0, 255), 25); // Blue wave
                break;
            case 4:
                ws2812_effect_solid_color(ws2812_color_rgb(255, 255, 255)); // White
                break;
            case 5:
                ws2812_effect_off();
                break;
        }
    }
    
    // Update efek yang berjalan (kecuali solid/off)
    switch (ws2812_get_current_effect()) {
        case WS2812_EFFECT_RAINBOW:
            ws2812_effect_rainbow(5);
            break;
        case WS2812_EFFECT_BREATHING:
        case WS2812_EFFECT_METEOR_CENTER_DUAL:
        case WS2812_EFFECT_WAVE_PING_PONG:
            // Efek ini update sendiri di fungsi mereka
            break;
        default:
            break;
    }
}


// Task Buzzer Test
void buzzer_test_task(void) {
    static uint32_t buzzer_timer = 0;
    static uint8_t buzzer_phase = 0;
    
    buzzer_timer++;
    
    // Ganti buzzer test setiap 5 detik
    if (buzzer_timer % 500 == 0) {  // 5 detik (500 * 10ms)
        buzzer_phase = (buzzer_phase + 1) % 5;
        
        switch (buzzer_phase) {
            case 0:
                buzzer_beep(BEEP_SHORT);  // 1x beep
                break;
            case 1:
                buzzer_beep(BEEP_DOUBLE); // 2x beep
                break;
            case 2:
                buzzer_beep(BEEP_TRIPLE); // 3x beep
                break;
            case 3:
                buzzer_beep(BEEP_ERROR);  // 5x beep
                break;
            case 4:
                buzzer_beep(BEEP_CONTINUOUS); // Continuous
                // Akan dimatikan oleh timer berikutnya
                break;
        }
    }
    
    // Matikan continuous beep setelah 1 detik
    if (buzzer_timer % 600 == 0 && buzzer_phase == 4) {
        buzzer_stop();
    }
    
    // Update buzzer state
    buzzer_task();
}

// Control task
void control_task(void) {
    static float t12_temp_filtered = 0.0f;
    static float last_power = 0.0f;
    
    if (adc_sensor_get_data(&g_adc_data)) {
        // Filter
        float alpha = 0.3f;
        t12_temp_filtered = (1.0f - alpha) * t12_temp_filtered + alpha * g_adc_data.t12_temp_c;
        
        // PID Update
        g_t12_pid.feedback = t12_temp_filtered;
        float power = fuzzy_pid_update(&g_t12_pid);
        
        // Deadband
        float error = (g_setpoint > t12_temp_filtered) ? 
                     (g_setpoint - t12_temp_filtered) : 
                     (t12_temp_filtered - g_setpoint);
        if (error < 2.0f && t12_temp_filtered > g_setpoint) {
            power = 0.0f;
        }
        
        // Smoothing
        float smoothed_power = 0.7f * last_power + 0.3f * power;
        
        // Set PWM
        pwm_timer0_set_duty(PWM_CH_T12_HEATER, smoothed_power);
        last_power = smoothed_power;
        
        // Simpan untuk display
        g_t12_power = smoothed_power;
    }
}

// LCD Update Task
void lcd_update_task(void) {
    char line[17];
    int temp_act, temp_set, power;
    
    // Convert floats to integers
    temp_act = (int)(g_adc_data.t12_temp_c + 0.5f);
    temp_set = (int)(g_setpoint + 0.5f);
    power = (int)(g_t12_power + 0.5f);
    
    // Line 1: Temperature
    line[0] = 'T'; line[1] = '1'; line[2] = '2'; line[3] = ':';
    
    if (temp_act >= 100) {
        line[4] = '0' + (temp_act / 100);
        line[5] = '0' + ((temp_act / 10) % 10);
    } else if (temp_act >= 10) {
        line[4] = ' ';
        line[5] = '0' + (temp_act / 10);
    } else {
        line[4] = ' ';
        line[5] = ' ';
    }
    line[6] = '0' + (temp_act % 10);
    
    line[7] = '/';
    
    if (temp_set >= 100) {
        line[8] = '0' + (temp_set / 100);
        line[9] = '0' + ((temp_set / 10) % 10);
    } else if (temp_set >= 10) {
        line[8] = ' ';
        line[9] = '0' + (temp_set / 10);
    } else {
        line[8] = ' ';
        line[9] = ' ';
    }
    line[10] = '0' + (temp_set % 10);
    
    line[11] = 0xDF;  // ° symbol
    line[12] = 'C';
    line[13] = '\0';
    
    lcd_print_string_at(line, 0, 0);
    
    // Line 2: WS2812 Status
    line[0] = 'W'; line[1] = 'S'; line[2] = '2'; line[3] = '8';
    line[4] = '1'; line[5] = '2'; line[6] = ':';
    
    // Show power as bargraph (simple)
    uint8_t bar = (power * 9) / 100;
    for (uint8_t i = 0; i < 9; i++) {
        line[7 + i] = (i < bar) ? '=' : ' ';
    }
    
    line[16] = '\0';
    lcd_print_string_at(line, 0, 1);
}

// 7-Segment Display Task
void display_task(void) {
    uint16_t t12_int = (uint16_t)(g_adc_data.t12_temp_c + 0.5f);
    uint16_t hotair_int = (uint16_t)(g_adc_data.hot_air_temp_c + 0.5f);

    if (t12_int > 550) t12_int = 550;
    if (hotair_int > 550) hotair_int = 550;

    display_set_digit(0, t12_int / 100);
    display_set_digit(1, (t12_int / 10) % 10);
    display_set_digit(2, t12_int % 10);

    display_set_digit(3, hotair_int / 100);
    display_set_digit(4, (hotair_int / 10) % 10);
    display_set_digit(5, hotair_int % 10);

    display_update_all();
}

// LED Blink Task
void led_blink_task(void) {
    static uint8_t state = false;
    gpio_bit_write(GPIOC, GPIO_PIN_13, state ? RESET : SET);
    state = !state;
}