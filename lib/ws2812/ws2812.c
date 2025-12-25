#include "ws2812.h"
#include "delay.h"
#include <string.h>

// Variabel internal
static uint16_t _num_leds = 0;
static uint8_t _brightness = 255;
static bool _is_busy = false;
static rgb_color_t _led_buffer[WS2812_MAX_LEDS];
static uint8_t _dma_buffer[WS2812_MAX_LEDS * 24 + 50]; // 24 bits per LED + reset

// Konversi clock ke timer ticks
static uint32_t _ns_to_ticks(uint32_t ns) {
    uint32_t timer_freq = rcu_clock_freq_get(CK_APB2) * 2; // Timer clock frequency
    return (ns * timer_freq) / 1000000000UL;
}

// Inisialisasi WS2812
void ws2812_init(uint16_t num_leds) {
    if (num_leds > WS2812_MAX_LEDS) {
        num_leds = WS2812_MAX_LEDS;
    }
    _num_leds = num_leds;

// ISR untuk DMA selesai
static void ws2812_dma_irq_handler(void) {
    if (dma_interrupt_flag_get(DMA_CH0, DMA_INT_FLAG_FTF)) {
        dma_interrupt_flag_clear(DMA_CH0, DMA_INT_FLAG_FTF);
        dma_busy = 0;
        
        // Pause timer dan reset ke state awal
        timer_disable(TIMER1);
        timer_counter_value_config(TIMER1, 0);
    }
}

static void ws2812_setup_pwm_dma(void) {
    // Setup TIMER1 untuk PWM 800kHz
    // APB1 clock = 108MHz, timer clock = 108MHz
    // PSC = 0, ARR = 1250 -> freq = 108MHz / 1250 = 86.4kHz
    // Kita gunakan PSC = 0, ARR = 135 -> freq = 108MHz / 135 = 800kHz
    
    // 1. Enable TIMER1 clock
    rcu_periph_clock_enable(RCU_TIMER1);
    
    // 2. Setup TIMER1 PWM
    timer_deinit(TIMER1);
    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler = 0;              // No prescaler
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.period = WS2812_BIT_PERIOD - 1;  // 1250-1 = 1249
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
    timer_init(TIMER1, &timer_initpara);
    
    // 3. Configure CH0 output (PB9)
    timer_oc_parameter_struct timer_ocpara;
    timer_oc_struct_para_init(&timer_ocpara);
    timer_ocpara.ocpolarity = TIMER_OC_POLARITY_HIGH;
    timer_ocpara.outputstate = TIMER_CCX_ENABLE;
    timer_channel_output_config(TIMER1, TIMER_CH_0, &timer_ocpara);
    
    // Setup PWM mode
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_0, 0);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_0, TIMER_OC_MODE_PWM0);
    
    // 4. Enable GPIO PB9 untuk TIMER1 CH0
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_9);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    gpio_af_set(GPIOB, GPIO_AF_1, GPIO_PIN_9);  // AF1 untuk TIMER1 CH0
    
    // 5. Setup DMA untuk mengirim CCR data
    // DMA_CH0 untuk TIMER1_CH0
    rcu_periph_clock_enable(RCU_DMA);
    dma_deinit(DMA_CH0);
    
    dma_parameter_struct dma_initpara;
    dma_struct_para_init(&dma_initpara);
    dma_initpara.direction = DMA_MEMORY_TO_PERIPHERAL;
    dma_initpara.memory_addr = (uint32_t)dma_buffer;
    dma_initpara.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_initpara.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_initpara.periph_addr = (uint32_t)&TIMER_CH0CV(TIMER1);
    dma_initpara.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_initpara.periph_width = DMA_PERIPH_WIDTH_16BIT;
    dma_initpara.priority = DMA_PRIORITY_HIGH;
    dma_init(DMA_CH0, &dma_initpara);
    
    // Enable DMA interrupt
    dma_interrupt_enable(DMA_CH0, DMA_INT_FTF);
    
    // Enable TIMER1 DMA request untuk CH0
    timer_dma_enable(TIMER1, TIMER_DMA_CH0D);
}

// --- Fungsi API ---
void ws2812_init(uint16_t num_leds_param) {
    if (num_leds_param > WS2812_MAX_LEDS) {
        num_leds_param = WS2812_MAX_LEDS;
    }
    
    num_leds = num_leds_param;
    ws2812_state.num_pixels = num_leds_param;

    // Setup TIMER1 PWM dan DMA
    ws2812_setup_pwm_dma();
    
    // Clear buffers
    memset(pixel_buffer, 0, sizeof(pixel_buffer));
    memset(ws2812_state.pixels, 0, sizeof(ws2812_state.pixels));
    memset(dma_buffer, 0, sizeof(dma_buffer));
    
    // Initial state
    ws2812_state.brightness = 255;
    ws2812_state.current_effect = WS2812_EFFECT_OFF;
    
    // Send initial reset
    dma_busy = 0;
}

void ws2812_set_pixel(uint16_t index, ws2812_color_t color) {
    if (index < num_leds) {
        pixel_buffer[index] = color;
    }
}

void ws2812_set_all(ws2812_color_t color) {
    for (uint16_t i = 0; i < num_leds; i++) {
        pixel_buffer[i] = color;
    }
}

void ws2812_clear_all(void) {
    ws2812_color_t black = {0, 0, 0};
    ws2812_set_all(black);
}

// Fungsi helper untuk mengisi DMA buffer dengan bit-stream
static void ws2812_fill_dma_buffer(void) {
    uint16_t buf_index = 0;
    
    // Untuk setiap LED
    for (uint16_t led = 0; led < num_leds; led++) {
        // Apply brightness
        ws2812_color_t color = ws2812_color_dim(pixel_buffer[led], ws2812_state.brightness);
        
        // Urutan WS2812: GRB
        uint8_t bytes[3] = {color.g, color.r, color.b};
        
        // Untuk setiap byte (G, R, B)
        for (uint8_t byte_idx = 0; byte_idx < 3; byte_idx++) {
            uint8_t byte_val = bytes[byte_idx];
            
            // Untuk setiap bit dalam byte (MSB first)
            for (int8_t bit_idx = 7; bit_idx >= 0; bit_idx--) {
                uint8_t bit = (byte_val >> bit_idx) & 0x01;
                
                // Masukkan nilai PWM compare untuk bit ini
                if (bit) {
                    dma_buffer[buf_index] = WS2812_BIT1_HIGH;  // Bit 1
                } else {
                    dma_buffer[buf_index] = WS2812_BIT0_HIGH;  // Bit 0
                }
                buf_index++;
            }
        }
    }
    
    // Tambahkan reset pulse (low untuk 60us, setara ~75 clock cycles at 1.25us per cycle)
    // Reset: kirim 0 untuk beberapa cycle
    for (uint16_t i = 0; i < 5; i++) {
        dma_buffer[buf_index++] = 0;
    }
}

void ws2812_update(void) {
    // Tunggu jika DMA masih berjalan
    while (dma_busy) {
        delay_us(10);
    }
    
    // Isi DMA buffer dengan data
    ws2812_fill_dma_buffer();
    
    uint16_t total_bits = num_leds * 24 + 5;  // 24 bits per LED + reset
    
    // Setup DMA untuk mengirim
    dma_busy = 1;
    dma_transfer_number_config(DMA_CH0, total_bits);
    dma_channel_enable(DMA_CH0);
    
    // Start TIMER1
    timer_enable(TIMER1);
}

bool ws2812_is_busy(void) {
    return dma_busy ? true : false;
}

// --- Color Functions ---
ws2812_color_t ws2812_color_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    ws2812_color_t color = {green, red, blue}; // GRB order untuk WS2812
    return color;
}

ws2812_color_t ws2812_color_hsv(uint8_t hue, uint8_t saturation, uint8_t value) {
    ws2812_color_t color = {0, 0, 0};
    
    if (saturation == 0) {
        // Grayscale
        color.r = color.g = color.b = value;
        return color;
    }
    
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;
    
    uint8_t p = (value * (255 - saturation)) >> 8;
    uint8_t q = (value * (255 - ((saturation * remainder) >> 8))) >> 8;
    uint8_t t = (value * (255 - ((saturation * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0:
            color.r = value; color.g = t; color.b = p;
            break;
        case 1:
            color.r = q; color.g = value; color.b = p;
            break;
        case 2:
            color.r = p; color.g = value; color.b = t;
            break;
        case 3:
            color.r = p; color.g = q; color.b = value;
            break;
        case 4:
            color.r = t; color.g = p; color.b = value;
            break;
        default:
            color.r = value; color.g = p; color.b = q;
            break;
    }
    
    // Convert to GRB order
    uint8_t temp = color.r;
    color.r = color.g;
    color.g = temp;
    
    return color;
}

ws2812_color_t ws2812_color_dim(ws2812_color_t color, uint8_t brightness) {
    if (brightness >= 255) {
        return color;
    }
    
    ws2812_color_t dimmed;
    uint32_t rounding = 127;
    
    dimmed.g = (uint8_t)(((uint32_t)color.g * brightness + rounding) / 255);
    dimmed.r = (uint8_t)(((uint32_t)color.r * brightness + rounding) / 255);
    dimmed.b = (uint8_t)(((uint32_t)color.b * brightness + rounding) / 255);
    
    return dimmed;
}

ws2812_color_t ws2812_color_wheel(uint8_t wheel_pos) {
    wheel_pos = 255 - wheel_pos;
    if (wheel_pos < 85) {
        return ws2812_color_rgb(255 - wheel_pos * 3, 0, wheel_pos * 3);
    } else if (wheel_pos < 170) {
        wheel_pos -= 85;
        return ws2812_color_rgb(0, wheel_pos * 3, 255 - wheel_pos * 3);
    } else {
        wheel_pos -= 170;
        return ws2812_color_rgb(wheel_pos * 3, 255 - wheel_pos * 3, 0);
    }
}

void ws2812_set_brightness(uint8_t brightness) {
    if (brightness > 255) brightness = 255;
    ws2812_state.brightness = brightness;
}

uint8_t ws2812_get_brightness(void) {
    return ws2812_state.brightness;
}

uint16_t ws2812_get_count(void) {
    return num_leds;
}

// --- Internal State Functions ---
static void update_internal(void) {
    // Salin dari state buffer ke pixel buffer dengan brightness
    for (uint16_t i = 0; i < num_leds; i++) {
        pixel_buffer[i] = ws2812_color_dim(ws2812_state.pixels[i], ws2812_state.brightness);
    }
    ws2812_update();
}

static void set_pixel_state(uint16_t index, ws2812_color_t color) {
    if (index < ws2812_state.num_pixels) {
        ws2812_state.pixels[index] = color;
    }
}

static void set_all_state(ws2812_color_t color) {
    for (uint16_t i = 0; i < ws2812_state.num_pixels; i++) {
        ws2812_state.pixels[i] = color;
    }
}

// --- Efek Functions ---
void ws2812_effect_rainbow(uint32_t speed) {
    static uint32_t wheel_pos = 0;
    static uint32_t last_update = 0;
    uint32_t current_time = get_millis();
    
    if ((current_time - last_update) >= speed) {
        last_update = current_time;
        
        wheel_pos += 1;
        if (wheel_pos >= 256) wheel_pos = 0;
        
        uint8_t pos8 = wheel_pos & 0xFF;

        for (uint16_t i = 0; i < ws2812_state.num_pixels; i++) {
            uint8_t pixel_pos = (pos8 + (i * 256 / ws2812_state.num_pixels)) & 0xFF;
            ws2812_state.pixels[i] = ws2812_color_wheel(pixel_pos);
        }

        update_internal();
    }
    
    ws2812_state.current_effect = WS2812_EFFECT_RAINBOW;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_breathing(ws2812_color_t color, uint32_t speed) {
    static uint16_t breath_index = 0;
    uint32_t current_time = get_millis();
    
    static const uint8_t sine_table[128] = {
        128, 134, 140, 146, 153, 159, 165, 171,
        177, 183, 188, 194, 199, 204, 209, 214,
        218, 223, 226, 230, 234, 237, 240, 243,
        245, 247, 250, 251, 253, 254, 254, 255,
        255, 255, 254, 254, 253, 251, 250, 247,
        245, 243, 240, 237, 234, 230, 226, 223,
        218, 214, 209, 204, 199, 194, 188, 183,
        177, 171, 165, 159, 153, 146, 140, 134,
        128, 122, 116, 110, 103, 97, 91, 85,
        79, 73, 68, 62, 57, 52, 47, 42,
        38, 33, 30, 26, 22, 19, 16, 13,
        11, 9, 6, 5, 3, 2, 2, 1,
        1, 1, 2, 2, 3, 5, 6, 9,
        11, 13, 16, 19, 22, 26, 30, 33,
        38, 42, 47, 52, 57, 62, 68, 73,
        79, 85, 91, 97, 103, 110, 116, 122
    };
    
    if (color.r != ws2812_state.effect_color.r || 
        color.g != ws2812_state.effect_color.g || 
        color.b != ws2812_state.effect_color.b) {
        ws2812_state.effect_color = color;
        breath_index = 0;
    }

    if ((current_time - ws2812_state.last_update) >= speed) {
        ws2812_state.last_update = current_time;
        
        breath_index++;
        if (breath_index >= 128) breath_index = 0;
        
        uint8_t brightness = sine_table[breath_index];
        
        // Apply brightness ke warna dasar
        ws2812_color_t dimmed = ws2812_state.effect_color;
        if (brightness < 255) {
            dimmed = ws2812_color_dim(ws2812_state.effect_color, brightness);
        }
        
        set_all_state(dimmed);
        update_internal();
    }
    
    ws2812_state.current_effect = WS2812_EFFECT_BREATHING;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_meteor_center_dual(ws2812_color_t color, uint32_t speed) {
    static int16_t step = 0;
    static uint32_t last_update = 0;
    uint32_t current_time = get_millis();
    uint16_t total_leds = ws2812_state.num_pixels;

    if (color.r != ws2812_state.effect_color.r || 
        color.g != ws2812_state.effect_color.g || 
        color.b != ws2812_state.effect_color.b) {
        ws2812_state.effect_color = color;
        step = 0;
    }

    if ((current_time - last_update) >= speed) {
        last_update = current_time;

        ws2812_color_t black = {0, 0, 0};
        set_all_state(black);

        uint16_t center_left = (total_leds - 1) / 2;
        uint16_t center_right = total_leds / 2;

        uint16_t max_dist = (total_leds + 1) / 2;
        uint16_t total_steps = max_dist * 2;

        uint16_t current_phase = step % total_steps;

        if (current_phase < max_dist) {
            uint16_t dist = current_phase;
            for (uint16_t i = 0; i <= dist; i++) {
                int16_t left = center_left - i;
                int16_t right = center_right + i;
                if (left >= 0) set_pixel_state(left, ws2812_state.effect_color);
                if (right < total_leds) set_pixel_state(right, ws2812_state.effect_color);
            }
        } else {
            uint16_t dist = total_steps - current_phase - 1;
            for (uint16_t i = 0; i <= dist; i++) {
                int16_t left = center_left - i;
                int16_t right = center_right + i;
                if (left >= 0) set_pixel_state(left, ws2812_state.effect_color);
                if (right < total_leds) set_pixel_state(right, ws2812_state.effect_color);
            }
        }

        update_internal();

        step++;
        if (step >= total_steps) step = 0;
    }

    ws2812_state.current_effect = WS2812_EFFECT_METEOR_CENTER_DUAL;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_ping_pong_wave(ws2812_color_t color, uint32_t speed) {
    static int16_t wave_step = 0;
    static uint32_t last_update = 0;
    uint32_t current_time = get_millis();
    uint16_t total_leds = ws2812_state.num_pixels;
    
    if (color.r != ws2812_state.effect_color.r || 
        color.g != ws2812_state.effect_color.g || 
        color.b != ws2812_state.effect_color.b) {
        ws2812_state.effect_color = color;
        wave_step = 0;
    }

    if ((current_time - last_update) >= speed) {
        last_update = current_time;
        
        ws2812_color_t black = {0, 0, 0};
        set_all_state(black);
        
        uint16_t max_steps = total_leds * 2;
        uint16_t current_step = wave_step % max_steps;
        
        if (current_step < total_leds) {
            for (uint16_t i = 0; i < total_leds; i++) {
                set_pixel_state(i, ws2812_state.effect_color);
            }
            
            uint16_t center_left = (total_leds / 2) - 1;
            uint16_t center_right = total_leds / 2;
            
            for (uint16_t i = 0; i <= current_step; i++) {
                int16_t left_pos = center_left - i;
                if (left_pos >= 0) set_pixel_state(left_pos, black);
                
                int16_t right_pos = center_right + i;
                if (right_pos < total_leds) set_pixel_state(right_pos, black);
            }
        } else {
            uint16_t step = current_step - total_leds;
            uint16_t center_left = (total_leds / 2) - 1;
            uint16_t center_right = total_leds / 2;
            
            for (uint16_t i = 0; i <= step; i++) {
                int16_t left_pos = i;
                if (left_pos <= center_left) set_pixel_state(left_pos, ws2812_state.effect_color);
                
                int16_t right_pos = total_leds - 1 - i;
                if (right_pos >= center_right) set_pixel_state(right_pos, ws2812_state.effect_color);
            }
        }
        
        update_internal();
        
        wave_step++;
        if (wave_step >= max_steps) wave_step = 0;
    }
    
    ws2812_state.current_effect = WS2812_EFFECT_WAVE_PING_PONG;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_solid_color(ws2812_color_t color) {
    ws2812_state.effect_color = color;
    set_all_state(color);
    update_internal();
    ws2812_state.current_effect = WS2812_EFFECT_SOLID_COLOR;
}

void ws2812_effect_off(void) {
    ws2812_color_t black = {0, 0, 0};
    set_all_state(black);
    update_internal();
    ws2812_state.current_effect = WS2812_EFFECT_OFF;
}

ws2812_effect_t ws2812_get_current_effect(void) {
    return ws2812_state.current_effect;
}