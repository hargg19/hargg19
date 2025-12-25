#ifndef PWM_TIMER0_H
#define PWM_TIMER0_H

// Jenis channel PWM
typedef enum {
    PWM_CH_T12_HEATER,
    PWM_CH_HOT_AIR_HEATER,
    PWM_CH_FAN
} pwm_channel_t;

// Konstanta
#define T12_MAX_DUTY        80.0f
#define HOT_AIR_MAX_DUTY    100.0f
#define FAN_MAX_DUTY        100.0f

// Fungsi API
void pwm_timer0_init(void);
void pwm_timer0_set_duty(pwm_channel_t channel, float duty_percent);

#endif