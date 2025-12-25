#include "buzzer.h"
#include "delay.h"  // Menggunakan get_millis() dari library Anda

// Internal state variables
static struct {
    beep_pattern_t pattern;
    uint8_t beep_count;
    uint8_t beep_total;
    uint32_t start_time;
    bool is_active;
    bool is_on;
    uint32_t beep_duration;
    uint32_t pause_duration;
} buzzer_state = {
    .pattern = BEEP_NONE,
    .beep_count = 0,
    .beep_total = 0,
    .start_time = 0,
    .is_active = false,
    .is_on = false,
    .beep_duration = 0,
    .pause_duration = 0
};

// Timing constants (milliseconds)
#define BEEP_SHORT_MS      100
#define BEEP_LONG_MS       300
#define BEEP_PAUSE_MS      150
#define BEEP_ERROR_PAUSE_MS 80

// Initialize buzzer GPIO
void buzzer_init(void) {
    // Enable GPIO clock
    rcu_periph_clock_enable(BUZZER_RCC);
    
    // Configure PB5 as output
    gpio_mode_set(BUZZER_GPIO, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BUZZER_PIN);
    gpio_output_options_set(BUZZER_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, BUZZER_PIN);
    
    // Ensure buzzer is off
    gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, RESET);
}

// Start a beep pattern
void buzzer_beep(beep_pattern_t pattern) {
    // Stop any ongoing beep
    buzzer_stop();
    
    // Set new pattern
    buzzer_state.pattern = pattern;
    buzzer_state.beep_count = 0;
    buzzer_state.is_active = true;
    buzzer_state.is_on = false;
    buzzer_state.start_time = get_millis();
    
    // Set total beeps based on pattern
    switch (pattern) {
        case BEEP_SHORT:
            buzzer_state.beep_total = 1;
            buzzer_state.beep_duration = BEEP_SHORT_MS;
            buzzer_state.pause_duration = 0;
            break;
        case BEEP_DOUBLE:
            buzzer_state.beep_total = 2;
            buzzer_state.beep_duration = BEEP_SHORT_MS;
            buzzer_state.pause_duration = BEEP_PAUSE_MS;
            break;
        case BEEP_TRIPLE:
            buzzer_state.beep_total = 3;
            buzzer_state.beep_duration = BEEP_SHORT_MS;
            buzzer_state.pause_duration = BEEP_PAUSE_MS;
            break;
        case BEEP_ERROR:
            buzzer_state.beep_total = 5;
            buzzer_state.beep_duration = BEEP_SHORT_MS;
            buzzer_state.pause_duration = BEEP_ERROR_PAUSE_MS;
            break;
        case BEEP_CONTINUOUS:
            buzzer_state.beep_total = 255; // Continuous
            buzzer_state.beep_duration = 0;
            buzzer_state.pause_duration = 0;
            break;
        default:
            buzzer_state.beep_total = 0;
            buzzer_state.is_active = false;
            return;
    }
    
    // Start first beep immediately
    if (pattern != BEEP_CONTINUOUS) {
        gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, SET);
        buzzer_state.is_on = true;
        buzzer_state.start_time = get_millis();
    } else {
        // Continuous beep - just turn on
        gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, SET);
        buzzer_state.is_on = true;
        buzzer_state.is_active = true;
    }
}

// Buzzer task - call this from your task scheduler (every 10-50ms)
void buzzer_task(void) {
    uint32_t current_time;
    uint32_t elapsed;
    
    if (!buzzer_state.is_active) return;
    
    current_time = get_millis();
    
    // Handle continuous beep separately
    if (buzzer_state.pattern == BEEP_CONTINUOUS) {
        // Continuous beep - will stay on until buzzer_stop() is called
        return;
    }
    
    elapsed = current_time - buzzer_state.start_time;
    
    if (buzzer_state.is_on) {
        // Buzzer is currently ON
        if (elapsed >= buzzer_state.beep_duration) {
            // Turn OFF buzzer
            gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, RESET);
            buzzer_state.is_on = false;
            buzzer_state.start_time = current_time;
            
            buzzer_state.beep_count++;
            if (buzzer_state.beep_count >= buzzer_state.beep_total) {
                // All beeps completed
                buzzer_state.is_active = false;
                return;
            }
        }
    } else {
        // Buzzer is currently OFF (pause between beeps)
        if (elapsed >= buzzer_state.pause_duration) {
            // Start next beep
            gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, SET);
            buzzer_state.is_on = true;
            buzzer_state.start_time = current_time;
        }
    }
}

// Stop buzzer immediately
void buzzer_stop(void) {
    gpio_bit_write(BUZZER_GPIO, BUZZER_PIN, RESET);
    buzzer_state.is_active = false;
    buzzer_state.is_on = false;
    buzzer_state.pattern = BEEP_NONE;
    buzzer_state.beep_count = 0;
}

// Check if buzzer is currently active
bool buzzer_is_active(void) {
    return buzzer_state.is_active;
}