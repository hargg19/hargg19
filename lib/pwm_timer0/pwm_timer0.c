#include "pwm_timer0.h"
#include "gd32f3x0.h"

// Frekuensi PWM (5 kHz)
#define PWM_FREQ_HZ         5000U
#define SYS_CLK_HZ          108000000U // Asumsi SystemCoreClock = 108 MHz

static uint32_t g_pwm_period = 0; // Disimpan agar bisa hitung duty

void pwm_timer0_init(void) {
    // 1. Enable clock (Tetap)
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_TIMER0);

    // 2. GPIO Config (Tetap)
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);
    gpio_af_set(GPIOA, GPIO_AF_2, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);

    // 3. Hitung periode untuk Center-Aligned
    // Pada mode Center-Aligned, frekuensi PWM adalah 1/2 dari frekuensi Edge-Aligned.
    // Maka g_pwm_period harus dibagi 2 agar frekuensi tetap 5kHz.
    uint16_t prescaler = 0; 
    g_pwm_period = (SYS_CLK_HZ / (PWM_FREQ_HZ * 2)) - 1; 

    // 4. Konfigurasi TIMER0
    timer_parameter_struct timer_cfg;
    timer_deinit(TIMER0);
    timer_struct_para_init(&timer_cfg);
    timer_cfg.prescaler         = prescaler;
    timer_cfg.period            = g_pwm_period;
    
    // --- RUBAH DISINI: Agar bisa memicu ADC di Lembah ---
    timer_cfg.alignedmode       = TIMER_COUNTER_CENTER_BOTH; 
    timer_cfg.counterdirection  = TIMER_COUNTER_UP; 
    // ---------------------------------------------------
    
    timer_cfg.clockdivision     = TIMER_CKDIV_DIV1;
    timer_cfg.repetitioncounter = 0;
    timer_init(TIMER0, &timer_cfg);

    // --- TAMBAHKAN DISINI: Master Mode Selection ---
    // Mengirim sinyal TRGO ke ADC setiap kali counter mencapai 0 (Lembah)
    timer_master_output_trigger_source_select(TIMER0, TIMER_TRI_OUT_SRC_UPDATE);
    // -----------------------------------------------

    // 5. Konfigurasi Channel Output (Tetap)
    timer_oc_parameter_struct ocpara;
    timer_channel_output_struct_para_init(&ocpara);
    ocpara.outputstate  = TIMER_CCX_ENABLE;
    ocpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    
    timer_channel_output_config(TIMER0, TIMER_CH_0, &ocpara);
    timer_channel_output_config(TIMER0, TIMER_CH_1, &ocpara);
    timer_channel_output_config(TIMER0, TIMER_CH_2, &ocpara);

    // 6. Mode PWM0 (Tetap)
    timer_channel_output_mode_config(TIMER0, TIMER_CH_0, TIMER_OC_MODE_PWM0);
    timer_channel_output_mode_config(TIMER0, TIMER_CH_1, TIMER_OC_MODE_PWM0);
    timer_channel_output_mode_config(TIMER0, TIMER_CH_2, TIMER_OC_MODE_PWM0);

    // --- RUBAH DISINI: Main Output Enable ---
    // Untuk TIMER0, ini WAJIB ENABLE agar PWM muncul di pin PA8/9/10
    timer_primary_output_config(TIMER0, ENABLE);
    // -----------------------------------------

    timer_enable(TIMER0);
}


// Fungsi untuk mengatur duty cycle
void pwm_timer0_set_duty(pwm_channel_t channel, float duty_percent) {
    // Batasi duty cycle maksimum berdasarkan channel
    float max_duty = 100.0f;
    switch (channel) {
        case PWM_CH_T12_HEATER:
            max_duty = T12_MAX_DUTY;
            break;
        case PWM_CH_HOT_AIR_HEATER:
        case PWM_CH_FAN:
        default:
            max_duty = HOT_AIR_MAX_DUTY; // atau FAN_MAX_DUTY
            break;
    }

    // Batasi input
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > max_duty) duty_percent = max_duty;

    // Hitung nilai pulse (CCR value)
    uint32_t pulse = (uint32_t)(duty_percent * g_pwm_period / 100.0f);

    // Set nilai compare untuk channel tertentu
    switch (channel) {
        case PWM_CH_T12_HEATER:
            timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_0, pulse); // PA8
            break;
        case PWM_CH_HOT_AIR_HEATER:
            timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_1, pulse); // PA9
            break;
        case PWM_CH_FAN:
            timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_2, pulse); // PA10
            break;
    }
}