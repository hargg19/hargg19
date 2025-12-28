/*
 * WS2812 driver for GD32F350CBT6 using PB9 (TIMER16_CH0) + DMA
 *
 * This implementation matches the definitions in lib/ws2812/ws2812.h:
 * - WS2812_TIMER  -> TIMER16
 * - WS2812_GPIO   -> GPIOB / PB9
 * - WS2812_DMA_CHANNEL -> DMA_CH3 (and IRQ: DMA_Channel3_4_IRQHandler)
 *
 * Requirements (in ws2812.h or project config):
 * - WS2812_BIT_PERIOD  : timer period (ARR) for ~1.25us base (in timer ticks)
 * - WS2812_BIT1_HIGH   : CCR value for a '1' bit (high time)
 * - WS2812_BIT0_HIGH   : CCR value for a '0' bit (high time)
 *
 * Make sure the vector table calls DMA_Channel3_4_IRQHandler (declared in ws2812.h).
 */

#include "ws2812.h"
#include "delay.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "gd32f3x0.h"

/* Fallback AF for PB9 if not defined elsewhere */
#ifndef WS2812_GPIO_AF
#define WS2812_GPIO_AF GPIO_AF_2
#endif

/* Reset pulses (timer periods) for >50..60us reset low */
#ifndef WS2812_RESET_PULSES
#define WS2812_RESET_PULSES 300
#endif

/* Internal state */
typedef struct {
    uint16_t num_pixels;
    uint8_t brightness;
    ws2812_effect_t current_effect;
    ws2812_color_t effect_color;
    uint32_t last_update;
    uint32_t effect_param;
    ws2812_color_t pixels[WS2812_MAX_LEDS];
} ws2812_state_t;

static ws2812_state_t ws2812_state;

/* DMA busy flag */
static volatile uint8_t dma_busy = 0;

/* Pixel buffer used for preparing DMA stream (stores GRB as in ws2812_color_t) */
static ws2812_color_t pixel_buffer[WS2812_MAX_LEDS];

/* DMA buffer: 24 words per LED (one word per bit) + reset padding */
static uint16_t dma_buffer[WS2812_MAX_LEDS * 24 + WS2812_RESET_PULSES + 8];

/* Forward declarations */
static void ws2812_setup_timer_dma(void);
static void ws2812_fill_dma_buffer(void);
static void ws2812_dma_complete_handler(void);
static void update_internal_from_state(void);
static void set_pixel_state(uint16_t index, ws2812_color_t color);
static void set_all_state(ws2812_color_t color);

/* IRQ wrapper declared in header: DMA_Channel3_4_IRQHandler */
void DMA_Channel3_4_IRQHandler(void)
{
    ws2812_dma_complete_handler();
}

/* Internal DMA completion handler */
static void ws2812_dma_complete_handler(void)
{
    /* Check transfer complete flag for configured channel */
    if (dma_interrupt_flag_get(WS2812_DMA_CHANNEL, DMA_INT_FLAG_FTF)) {
        /* clear flag */
        dma_interrupt_flag_clear(WS2812_DMA_CHANNEL, DMA_INT_FLAG_FTF);

        /* disable DMA channel and timer */
        dma_channel_disable(WS2812_DMA_CHANNEL);
        timer_disable(WS2812_TIMER);
        timer_counter_value_config(WS2812_TIMER, 0);

        dma_busy = 0;
    }
}

static void ws2812_setup_timer_dma(void)
{
    rcu_periph_clock_enable(WS2812_GPIO_RCC);
    rcu_periph_clock_enable(WS2812_TIMER_RCC);
    rcu_periph_clock_enable(WS2812_DMA_RCC);
    
    /* Penting: Pastikan RCU_CFG0 atau clock sistem sudah benar untuk TIMER16 */
    syscfg_deinit();
    syscfg_dma_remap_enable(SYSCFG_DMA_REMAP_TIMER16);

    gpio_mode_set(WS2812_GPIO, GPIO_MODE_AF, GPIO_PUPD_NONE, WS2812_PIN);
    gpio_output_options_set(WS2812_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, WS2812_PIN);
    gpio_af_set(WS2812_GPIO, WS2812_GPIO_AF, WS2812_PIN);


    /* DMA Config */
    dma_deinit(WS2812_DMA_CHANNEL);
    dma_parameter_struct dma_initpara;
    dma_struct_para_init(&dma_initpara);

    dma_initpara.direction    = DMA_MEMORY_TO_PERIPHERAL;
    dma_initpara.memory_addr  = (uint32_t)dma_buffer;
    dma_initpara.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_initpara.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_initpara.periph_addr  = (uint32_t)&TIMER_CH0CV(WS2812_TIMER); 
    dma_initpara.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_initpara.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_initpara.priority     = DMA_PRIORITY_ULTRA_HIGH; // Naikkan prioritas
    dma_init(WS2812_DMA_CHANNEL, &dma_initpara);

    dma_interrupt_enable(WS2812_DMA_CHANNEL, DMA_INT_FTF);
    nvic_irq_enable(WS2812_DMA_IRQn, 0U, 0U);

    timer_deinit(WS2812_TIMER);
    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara); // Inisialisasi struct dengan nilai default

    timer_initpara.prescaler         = 0; 
    timer_initpara.period            = 123; // 108MHz / 135 = 800kHz
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init(WS2812_TIMER, &timer_initpara);

    timer_oc_parameter_struct timer_ocpara;
    
    timer_ocpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocpara.outputnstate = TIMER_CCXN_ENABLE;
    timer_ocpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocpara.ocnidlestate  = TIMER_OCN_IDLE_STATE_LOW;
    timer_ocpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_channel_output_config(WS2812_TIMER, TIMER_CH_0, &timer_ocpara);

    //timer_channel_output_pulse_value_config(WS2812_TIMER, TIMER_CH_0, &dma_buffer);
    /* PWM Mode 0 atau 1 oke, yang penting konsisten dengan BIT1/BIT0_HIGH */
    timer_channel_output_mode_config(WS2812_TIMER, TIMER_CH_0, TIMER_OC_MODE_PWM1);
    timer_channel_output_shadow_config(WS2812_TIMER, TIMER_CH_0, TIMER_OC_SHADOW_ENABLE);
    
    timer_primary_output_config(WS2812_TIMER, ENABLE);
    
    /* Gunakan Update DMA Request untuk stabilitas timing WS2812 */
    timer_dma_enable(WS2812_TIMER, TIMER_DMA_UPD); 

    
}

static void ws2812_fill_dma_buffer(void)
{
    uint32_t idx = 0;
    const uint16_t h_val = (uint16_t)WS2812_BIT1_HIGH;
    const uint16_t l_val = (uint16_t)WS2812_BIT0_HIGH;

    for (uint16_t led = 0; led < ws2812_state.num_pixels; led++) {
        ws2812_color_t color = pixel_buffer[led];
        
        // Urutan WS2812 adalah G -> R -> B
        uint8_t data[3] = { color.g, color.r, color.b };

        for (uint8_t i = 0; i < 3; i++) {
            uint8_t v = data[i];
            // Unrolling 8 bit: Langsung cek bitmask satu per satu
            dma_buffer[idx++] = (v & 0x80) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x40) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x20) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x10) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x08) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x04) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x02) ? h_val : l_val;
            dma_buffer[idx++] = (v & 0x01) ? h_val : l_val;
        }
    }

    // 2. Tambahkan Reset Pulse (mengisi buffer dengan 0)
    // Gunakan <= WS2812_MAX_LEDS * 24 + WS2812_RESET_PULSES untuk proteksi
    uint32_t total = (uint32_t)(ws2812_state.num_pixels * 24 + WS2812_RESET_PULSES);
    
    for (; idx < total; idx++) { // <--- 'total' digunakan di sini
    dma_buffer[idx] = 0;
}
}

/* Update internal pixel_buffer from ws2812_state.pixels applying brightness */
static void update_internal_from_state(void)
{
    for (uint16_t i = 0; i < ws2812_state.num_pixels; i++) {
        /* Apply brightness scaling (ws2812_color_dim expects GRB input) */
        pixel_buffer[i] = ws2812_color_dim(ws2812_state.pixels[i], ws2812_state.brightness);
    }
}

/* Set pixel in internal state */
static void set_pixel_state(uint16_t index, ws2812_color_t color)
{
    if (index < ws2812_state.num_pixels) {
        ws2812_state.pixels[index] = color;
    }
}

/* Set all pixels in internal state */
static void set_all_state(ws2812_color_t color)
{
    for (uint16_t i = 0; i < ws2812_state.num_pixels; i++) {
        ws2812_state.pixels[i] = color;
    }
}

/* Public API implementations */

void ws2812_init(uint16_t num_leds)
{
    if (num_leds == 0) return;
    if (num_leds > WS2812_MAX_LEDS) num_leds = WS2812_MAX_LEDS;

    /* Initialize state */
    memset(&ws2812_state, 0, sizeof(ws2812_state));
    ws2812_state.num_pixels = num_leds;
    ws2812_state.brightness = 255;
    ws2812_state.current_effect = WS2812_EFFECT_OFF;
    ws2812_state.last_update = 0;
    ws2812_state.effect_param = 0;

    /* Clear pixel & DMA buffers */
    memset(pixel_buffer, 0, sizeof(pixel_buffer));
    memset(dma_buffer, 0, sizeof(dma_buffer));
    memset(ws2812_state.pixels, 0, sizeof(ws2812_state.pixels));

    dma_busy = 0;

    /* Setup timer + dma for PB9 (TIMER16_CH0) */
    ws2812_setup_timer_dma();
}

void ws2812_set_brightness(uint8_t brightness)
{
    ws2812_state.brightness = (brightness > 255) ? 255 : brightness;
}

uint8_t ws2812_get_brightness(void)
{
    return ws2812_state.brightness;
}

uint16_t ws2812_get_count(void)
{
    return ws2812_state.num_pixels;
}

void ws2812_set_pixel(uint16_t index, ws2812_color_t color)
{
    if (index < ws2812_state.num_pixels) {
        ws2812_state.pixels[index] = color;
    }
}

void ws2812_set_all(ws2812_color_t color)
{
    set_all_state(color);
}

void ws2812_clear_all(void)
{
    ws2812_color_t black = {0, 0, 0};
    set_all_state(black);
}

/* Send data using DMA. Blocking only while previous transfer active. */
void ws2812_update(void)
{   if (ws2812_state.num_pixels > WS2812_MAX_LEDS) return; // Proteksi tambahan
    /* Wait previous transfer */
    while (dma_busy) {
        delay_us(1);
    }

    /* Prepare pixel buffer with applied brightness */
    update_internal_from_state();

    /* Fill DMA compare buffer */
    ws2812_fill_dma_buffer();

    uint32_t total_words = (uint32_t)(ws2812_state.num_pixels * 24 + WS2812_RESET_PULSES);

    /* Clear DMA flags */
    dma_interrupt_flag_clear(WS2812_DMA_CHANNEL, DMA_INT_FLAG_G);
    dma_interrupt_flag_clear(WS2812_DMA_CHANNEL, DMA_INT_FLAG_FTF);

    /* Configure transfer count and enable channel */
    dma_transfer_number_config(WS2812_DMA_CHANNEL, total_words);

    /* Ensure CCR is zero before start (line low) */
    timer_channel_output_pulse_value_config(WS2812_TIMER, TIMER_CH_0, 0);

    dma_busy = 1;

    /* Enable DMA channel then start timer */
    dma_channel_enable(WS2812_DMA_CHANNEL);
    timer_enable(WS2812_TIMER);
}

bool ws2812_is_busy(void)
{
    return dma_busy ? true : false;
}

/* Color helpers: note ws2812_color_t is GRB order in struct as defined in header.
 * We'll provide helpers that accept RGB/hsv and return ws2812_color_t (GRB fields).
 */
ws2812_color_t ws2812_color_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    ws2812_color_t c;
    c.g = green;
    c.r = red;
    c.b = blue;
    return c;
}

ws2812_color_t ws2812_color_hsv(uint8_t hue, uint8_t saturation, uint8_t value)
{
    ws2812_color_t color = {0,0,0};

    if (saturation == 0) {
        /* grayscale */
        color.r = color.g = color.b = value;
        return color;
    }

    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;

    uint8_t p = (value * (255 - saturation)) >> 8;
    uint8_t q = (value * (255 - ((saturation * remainder) >> 8))) >> 8;
    uint8_t t = (value * (255 - ((saturation * (255 - remainder)) >> 8))) >> 8;

    ws2812_color_t tmp;
    switch (region) {
        case 0: tmp.r = value; tmp.g = t; tmp.b = p; break;
        case 1: tmp.r = q; tmp.g = value; tmp.b = p; break;
        case 2: tmp.r = p; tmp.g = value; tmp.b = t; break;
        case 3: tmp.r = p; tmp.g = q; tmp.b = value; break;
        case 4: tmp.r = t; tmp.g = p; tmp.b = value; break;
        default: tmp.r = value; tmp.g = p; tmp.b = q; break;
    }

    /* Convert to ws2812_color_t order (GRB fields) */
    ws2812_color_t out;
    out.g = tmp.g;
    out.r = tmp.r;
    out.b = tmp.b;
    return out;
}

ws2812_color_t ws2812_color_dim(ws2812_color_t color, uint8_t brightness)
{
    if (brightness >= 255) return color;

    ws2812_color_t out;
    const uint32_t rounding = 127;
    out.g = (uint8_t)(((uint32_t)color.g * brightness + rounding) / 255);
    out.r = (uint8_t)(((uint32_t)color.r * brightness + rounding) / 255);
    out.b = (uint8_t)(((uint32_t)color.b * brightness + rounding) / 255);
    return out;
}

ws2812_color_t ws2812_color_wheel(uint8_t wheel_pos)
{
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

/* --- Effects (use internal state) --- */

ws2812_effect_t ws2812_get_current_effect(void)
{
    return ws2812_state.current_effect;
}

void ws2812_effect_solid_color(ws2812_color_t color)
{
    ws2812_state.effect_color = color;
    set_all_state(color);
    update_internal_from_state();
    ws2812_update();
    ws2812_state.current_effect = WS2812_EFFECT_SOLID_COLOR;
    ws2812_state.effect_param = 0;
}

void ws2812_effect_off(void)
{
    ws2812_color_t black = {0,0,0};
    set_all_state(black);
    update_internal_from_state();
    ws2812_update();
    ws2812_state.current_effect = WS2812_EFFECT_OFF;
    ws2812_state.effect_param = 0;
}

void ws2812_effect_rainbow(uint32_t speed)
{
    static uint16_t wheel_pos = 0;
    uint32_t current_time = get_millis();

    if ((current_time - ws2812_state.last_update) >= speed) {
        ws2812_state.last_update = current_time;

        wheel_pos++;
        if (wheel_pos >= 256) wheel_pos = 0;
        uint8_t pos8 = (uint8_t)(wheel_pos & 0xFF);

        for (uint16_t i = 0; i < ws2812_state.num_pixels; i++) {
            uint8_t pixel_pos = (uint8_t)((pos8 + (i * 256 / ws2812_state.num_pixels)) & 0xFF);
            ws2812_state.pixels[i] = ws2812_color_wheel(pixel_pos);
        }

        update_internal_from_state();
        ws2812_update();
    }

    ws2812_state.current_effect = WS2812_EFFECT_RAINBOW;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_breathing(ws2812_color_t color, uint32_t speed)
{
    static uint16_t breath_index = 0;
    static const uint8_t sine_table[128] = {
        128,134,140,146,153,159,165,171,177,183,188,194,199,204,209,214,
        218,223,226,230,234,237,240,243,245,247,250,251,253,254,254,255,
        255,255,254,254,253,251,250,247,245,243,240,237,234,230,226,223,
        218,214,209,204,199,194,188,183,177,171,165,159,153,146,140,134,
        128,122,116,110,103,97,91,85,79,73,68,62,57,52,47,42,
        38,33,30,26,22,19,16,13,11,9,6,5,3,2,2,1,
        1,1,2,2,3,5,6,9,11,13,16,19,22,26,30,33,
        38,42,47,52,57,62,68,73,79,85,91,97,103,110,116,122
    };

    uint32_t current_time = get_millis();

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

        ws2812_color_t dimmed = ws2812_color_dim(ws2812_state.effect_color, brightness);
        set_all_state(dimmed);
        update_internal_from_state();
        ws2812_update();
    }

    ws2812_state.current_effect = WS2812_EFFECT_BREATHING;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_meteor_center_dual(ws2812_color_t color, uint32_t speed)
{
    static int16_t step = 0;
    uint32_t current_time = get_millis();
    uint16_t total_leds = ws2812_state.num_pixels;

    if (color.r != ws2812_state.effect_color.r ||
        color.g != ws2812_state.effect_color.g ||
        color.b != ws2812_state.effect_color.b) {
        ws2812_state.effect_color = color;
        step = 0;
    }

    if ((current_time - ws2812_state.last_update) >= speed) {
        ws2812_state.last_update = current_time;

        ws2812_color_t black = {0,0,0};
        set_all_state(black);

        uint16_t center_left = (total_leds - 1) / 2;
        uint16_t center_right = total_leds / 2;

        uint16_t max_dist = (total_leds + 1) / 2;
        uint16_t total_steps = max_dist * 2;

        uint16_t current_phase = (uint16_t)(step % total_steps);

        if (current_phase < max_dist) {
            uint16_t dist = current_phase;
            for (uint16_t i = 0; i <= dist; i++) {
                int16_t left = center_left - i;
                int16_t right = center_right + i;
                if (left >= 0) set_pixel_state((uint16_t)left, ws2812_state.effect_color);
                if (right < total_leds) set_pixel_state((uint16_t)right, ws2812_state.effect_color);
            }
        } else {
            uint16_t dist = total_steps - current_phase - 1;
            for (uint16_t i = 0; i <= dist; i++) {
                int16_t left = center_left - i;
                int16_t right = center_right + i;
                if (left >= 0) set_pixel_state((uint16_t)left, ws2812_state.effect_color);
                if (right < total_leds) set_pixel_state((uint16_t)right, ws2812_state.effect_color);
            }
        }

        update_internal_from_state();
        ws2812_update();

        step++;
        if (step >= (int16_t) ( ( (total_leds + 1) / 2 ) * 2 )) step = 0;
    }

    ws2812_state.current_effect = WS2812_EFFECT_METEOR_CENTER_DUAL;
    ws2812_state.effect_param = speed;
}

void ws2812_effect_ping_pong_wave(ws2812_color_t color, uint32_t speed)
{
    static int16_t wave_step = 0;
    uint32_t current_time = get_millis();
    uint16_t total_leds = ws2812_state.num_pixels;

    if (color.r != ws2812_state.effect_color.r ||
        color.g != ws2812_state.effect_color.g ||
        color.b != ws2812_state.effect_color.b) {
        ws2812_state.effect_color = color;
        wave_step = 0;
    }

    if ((current_time - ws2812_state.last_update) >= speed) {
        ws2812_state.last_update = current_time;

        ws2812_color_t black = {0,0,0};
        set_all_state(black);

        uint16_t max_steps = total_leds * 2;
        uint16_t current_step = (uint16_t)(wave_step % max_steps);

        if (current_step < total_leds) {
            /* Start from edges to center clearing center area progressively */
            for (uint16_t i = 0; i < total_leds; i++) {
                set_pixel_state(i, ws2812_state.effect_color);
            }

            uint16_t center_left = (total_leds / 2) - 1;
            uint16_t center_right = total_leds / 2;

            for (uint16_t i = 0; i <= current_step; i++) {
                int16_t left_pos = center_left - i;
                if (left_pos >= 0) set_pixel_state((uint16_t)left_pos, black);

                int16_t right_pos = center_right + i;
                if (right_pos < total_leds) set_pixel_state((uint16_t)right_pos, black);
            }
        } else {
            uint16_t step = current_step - total_leds;
            uint16_t center_left = (total_leds / 2) - 1;
            uint16_t center_right = total_leds / 2;

            for (uint16_t i = 0; i <= step; i++) {
                int16_t left_pos = i;
                if (left_pos <= center_left) set_pixel_state((uint16_t)left_pos, ws2812_state.effect_color);

                int16_t right_pos = total_leds - 1 - i;
                if (right_pos >= center_right) set_pixel_state((uint16_t)right_pos, ws2812_state.effect_color);
            }
        }

        update_internal_from_state();
        ws2812_update();

        wave_step++;
        if (wave_step >= (int16_t) (total_leds * 2)) wave_step = 0;
    }

    ws2812_state.current_effect = WS2812_EFFECT_WAVE_PING_PONG;
    ws2812_state.effect_param = speed;
}

/* End of file */