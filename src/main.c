#include "gd32f3x0.h"
#include "delay.h"
#include "ht1621.h"
#include "fuzzy_pid.h"
#include "adc_sensor.h"
#include "pwm_timer0.h"
#include "i2c_lcd.h"
#include "stdlib.h"
#include "arm_math.h"

// Variabel global
static fuzzy_pid_t g_t12_pid;
static float g_setpoint = 280.0f;
static float g_t12_power = 0.0f;


// Prototipe task
void control_task(void);
void display_task(void);
void led_blink_task(void);
void lcd_update_task(void);  // Task untuk update LCD

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
    
    // Inisialisasi LCD - SIMPLE INIT
    lcd_init();
    lcd_clear();
    lcd_print_string_at("Solder Station", 0, 0);
    lcd_print_string_at("Initializing...", 0, 1);
    delay_ms(1000);
    
    // Inisialisasi lainnya
    ht1621_init();
    display_startup_animation();
    pwm_timer0_init();
    adc_sensor_init();
    adc_sensor_start();

    // Inisialisasi Fuzzy-PID
    fuzzy_pid_init(&g_t12_pid, MODE_SOLDER_T12);
    fuzzy_pid_set_setpoint(&g_t12_pid, g_setpoint);


    // Jalankan task
    task_start_priority(control_task, 5, TASK_PRIORITY_HIGH);      // 100 Hz
    task_start_priority(display_task, 100, TASK_PRIORITY_NORMAL);  // 10 Hz
    task_start_priority(lcd_update_task, 200, TASK_PRIORITY_NORMAL); // 5 Hz untuk LCD
    task_start_priority(led_blink_task, 500, TASK_PRIORITY_LOW);   // 2 Hz

    // Clear LCD dan tampilkan mode operasi
    lcd_clear();
    lcd_print_string_at("T12: ---/---C", 0, 0);
    lcd_print_string_at("Power: --%", 0, 1);

    while (1) {
        task_scheduler_run();
        __WFI();
    }
}

void control_task(void) {
    static float t12_temp_filtered = 0.0f;
    static float last_power = 0.0f;
    
    if (adc_sensor_get_data(&g_adc_data)) {
        // Filter suhu
        float alpha = 0.3f;
        t12_temp_filtered = (1.0f - alpha) * t12_temp_filtered + alpha * g_adc_data.t12_temp_c;
        
        // Update PID
        g_t12_pid.feedback = t12_temp_filtered;
        float power = fuzzy_pid_update(&g_t12_pid);
        
        // Deadband control
        float error = fabsf(g_setpoint - t12_temp_filtered);
        if (error < 2.0f && t12_temp_filtered > g_setpoint) {
            power = 0.0f;
        }
        
        // Smoothing power
        float smoothed_power = 0.7f * last_power + 0.3f * power;
        
        // Set PWM
        pwm_timer0_set_duty(PWM_CH_T12_HEATER, smoothed_power);
        last_power = smoothed_power;
        
        // Simpan untuk display
        g_t12_power = smoothed_power;
    }
}

void lcd_update_task(void) {
    static uint32_t tick_counter = 0;
    static uint8_t display_mode = 0;
    static uint8_t anim_pos = 0;
    char buffer[17];
    uint8_t i;
    
    tick_counter++;
    
    // Ganti display mode setiap 3 detik (3000ms / 250ms = 12 ticks)
    if (tick_counter % 12 == 0) {
        display_mode = (display_mode + 1) % 3;
    }
    
    // Convert floats to integers
    int temp_act = (int)(g_adc_data.t12_temp_c + 0.5f);
    int temp_set = (int)(g_setpoint + 0.5f);
    int power = (int)(g_t12_power + 0.5f);
    int temp_ha = (int)(g_adc_data.hot_air_temp_c + 0.5f);
    
    switch (display_mode) {
        case 0: // Mode 1: T12 Power Bargraph (full width) + Info
            // Baris atas: T12 Temp - "T12:245/280°C"
            buffer[0] = 'T'; buffer[1] = '1'; buffer[2] = '2'; buffer[3] = ':';
            
            // Temperature actual (3 digits)
            if (temp_act >= 100) {
                buffer[4] = '0' + (temp_act / 100);
                buffer[5] = '0' + ((temp_act / 10) % 10);
            } else if (temp_act >= 10) {
                buffer[4] = ' ';
                buffer[5] = '0' + (temp_act / 10);
            } else {
                buffer[4] = ' ';
                buffer[5] = ' ';
            }
            buffer[6] = '0' + (temp_act % 10);
            
            buffer[7] = '/';
            
            // Setpoint (3 digits)
            if (temp_set >= 100) {
                buffer[8] = '0' + (temp_set / 100);
                buffer[9] = '0' + ((temp_set / 10) % 10);
            } else if (temp_set >= 10) {
                buffer[8] = ' ';
                buffer[9] = '0' + (temp_set / 10);
            } else {
                buffer[8] = ' ';
                buffer[9] = ' ';
            }
            buffer[10] = '0' + (temp_set % 10);
            
            buffer[11] = 0xDF;  // ° symbol
            buffer[12] = 'C';
            buffer[13] = '\0';
            
            lcd_print_string_at(buffer, 0, 0);
            
            // Baris bawah: Power Bargraph 16 karakter penuh
            lcd_draw_bargraph((float)power, 1, 0, 16);
            break;
            
        case 1: // Mode 2: Detailed Power Info
            // Baris atas: "PWR: 65% SP:280"
            buffer[0] = 'P'; buffer[1] = 'W'; buffer[2] = 'R'; buffer[3] = ':';
            buffer[4] = ' ';
            
            // Power percentage (3 digits)
            if (power >= 100) {
                buffer[5] = '1';
                buffer[6] = '0';
                buffer[7] = '0';
            } else if (power >= 10) {
                buffer[5] = ' ';
                buffer[6] = '0' + (power / 10);
                buffer[7] = '0' + (power % 10);
            } else {
                buffer[5] = ' ';
                buffer[6] = ' ';
                buffer[7] = '0' + power;
            }
            
            buffer[8] = '%';
            buffer[9] = ' ';
            buffer[10] = 'S'; buffer[11] = 'P'; buffer[12] = ':';
            
            // Setpoint (3 digits)
            if (temp_set >= 100) {
                buffer[13] = '0' + (temp_set / 100);
                buffer[14] = '0' + ((temp_set / 10) % 10);
                buffer[15] = '0' + (temp_set % 10);
            } else if (temp_set >= 10) {
                buffer[13] = ' ';
                buffer[14] = '0' + (temp_set / 10);
                buffer[15] = '0' + (temp_set % 10);
            } else {
                buffer[13] = ' ';
                buffer[14] = ' ';
                buffer[15] = '0' + temp_set;
            }
            
            buffer[16] = '\0';
            lcd_print_string_at(buffer, 0, 0);
            
            // Baris bawah: Animated running bargraph
            {
                // Bersihkan baris
                lcd_set_cursor(0, 1);
                for (i = 0; i < 16; i++) {
                    lcd_print_char(' ');
                }
                
                // Gambar bargraph statis
                uint8_t graph_width = (uint8_t)((power * 16) / 100);
                lcd_set_cursor(0, 1);
                for (i = 0; i < graph_width; i++) {
                    lcd_print_char(0xFF); // Block karakter
                }
                
                // Gambar running indicator jika ada power
                if (graph_width > 0) {
                    lcd_set_cursor(anim_pos % graph_width, 1);
                    lcd_print_char(0x7E); // Karakter panah →
                }
                
                anim_pos = (anim_pos + 1) % 16;
            }
            break;
            
        case 2: // Mode 3: Hot Air Info + System Status
            // Baris atas: Hot Air Temperature - "HA : 300°C"
            buffer[0] = 'H'; buffer[1] = 'A'; buffer[2] = ' '; buffer[3] = ':';
            buffer[4] = ' ';
            
            // Hot air temperature (3 digits)
            if (temp_ha >= 100) {
                buffer[5] = '0' + (temp_ha / 100);
                buffer[6] = '0' + ((temp_ha / 10) % 10);
                buffer[7] = '0' + (temp_ha % 10);
            } else if (temp_ha >= 10) {
                buffer[5] = ' ';
                buffer[6] = '0' + (temp_ha / 10);
                buffer[7] = '0' + (temp_ha % 10);
            } else {
                buffer[5] = ' ';
                buffer[6] = ' ';
                buffer[7] = '0' + temp_ha;
            }
            
            buffer[8] = 0xDF;  // ° symbol
            buffer[9] = 'C';
            buffer[10] = '\0';
            
            lcd_print_string_at(buffer, 0, 0);
            
            // Baris bawah: Status dan PWM
            {
                int error = abs(temp_set - temp_act);
                const char* status;
                
                if (error < 5) {
                    status = "READY";
                } else if (power > 70) {
                    status = "HEATING";
                } else {
                    status = "STABLE";
                }
                
                // Format: "READY PWM:65%"
                // Copy status (max 7 chars)
                for (i = 0; i < 7 && status[i] != '\0'; i++) {
                    buffer[i] = status[i];
                }
                // Fill remaining with spaces
                for (; i < 7; i++) {
                    buffer[i] = ' ';
                }
                
                buffer[7] = ' ';
                buffer[8] = 'P'; buffer[9] = 'W'; buffer[10] = 'M'; buffer[11] = ':';
                
                // Power (2 digits)
                if (power >= 100) {
                    buffer[12] = '1';
                    buffer[13] = '0';
                } else if (power >= 10) {
                    buffer[12] = '0' + (power / 10);
                    buffer[13] = '0' + (power % 10);
                } else {
                    buffer[12] = ' ';
                    buffer[13] = '0' + power;
                }
                
                buffer[14] = '%';
                buffer[15] = '\0';
                
                lcd_print_string_at(buffer, 0, 1);
            }
            break;
    }
}

void display_task(void) {
    // Update 7-segment display
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

void led_blink_task(void) {
    static uint8_t state = false;
    gpio_bit_write(GPIOC, GPIO_PIN_13, state ? RESET : SET);
    state = !state;
}