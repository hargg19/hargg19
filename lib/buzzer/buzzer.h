#ifndef BUZZER_H
#define BUZZER_H

#include "gd32f3x0.h"
#include <stdint.h>
#include <stdbool.h>

// Buzzer pin configuration
#define BUZZER_GPIO        GPIOB
#define BUZZER_PIN         GPIO_PIN_5
#define BUZZER_RCC         RCU_GPIOB

// Buzzer patterns
typedef enum {
    BEEP_NONE = 0,
    BEEP_SHORT,      // 1x short beep
    BEEP_DOUBLE,     // 2x beep
    BEEP_TRIPLE,     // 3x beep  
    BEEP_ERROR,      // 5x beep (error)
    BEEP_CONTINUOUS  // Continuous beep
} beep_pattern_t;

// Function prototypes
void buzzer_init(void);
void buzzer_beep(beep_pattern_t pattern);
void buzzer_task(void);  // Call this in your task scheduler
void buzzer_stop(void);
bool buzzer_is_active(void);

#endif // BUZZER_H