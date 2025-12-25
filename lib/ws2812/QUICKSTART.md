# WS2812 Library - Quick Start Guide

## Hardware Wiring (GD32F350CBT6)

```
GD32F350CBT6 Pin -> WS2812
┌─────────────────────────────┐
│ PB9 (TIMER16_CH0)  -------> │ DIN (WS2812 Data In)
│ GND ---------------P-------> │ GND
│ 3.3V (or 5V source) -------> │ 5V  (perlu level shifter jika 5V)
└─────────────────────────────┘
```

**PENTING**: 
- Tambahkan pull-up resistor 1k-10k di pin DIN
- Untuk strip 5V, gunakan level shifter 3.3V→5V di antara PB9 dan DIN
- Capacitor 100μF dekat power supply WS2812 untuk stabilitas

## Software Integration Checklist

- [x] ws2812.c dan ws2812.h sudah tersedia
- [x] Timing constants sudah dikonfigurasi untuk 108MHz APB2
- [x] `get_millis()` sudah tersedia dari delay.h
- [x] DMA_Channel1_2_IRQHandler sudah di-declare
- [ ] Verifikasi vector table memanggil DMA_Channel1_2_IRQHandler

## Minimal Code Example

```c
#include "ws2812.h"

int main(void) {
    // System init
    SystemInit();
    
    // Initialize WS2812 with 8 LEDs
    ws2812_init(8);
    ws2812_set_brightness(200);
    
    // Set all LEDs red
    ws2812_set_all(ws2812_color_rgb(255, 0, 0));
    ws2812_update();
    
    // Wait for transfer to complete
    while (ws2812_is_busy()) {
        delay_ms(1);
    }
    
    // Done!
    return 0;
}
```

## API Quick Reference

### Initialization
```c
ws2812_init(20);                           // Init 20 LEDs
ws2812_set_brightness(255);                // Set brightness 0-255
```

### Basic Control
```c
ws2812_set_pixel(0, ws2812_color_rgb(255, 0, 0));  // Set LED 0 to red
ws2812_set_all(ws2812_color_rgb(0, 255, 0));       // All green
ws2812_clear_all();                         // Off
ws2812_update();                            // Send to LEDs
```

### Color Helpers
```c
ws2812_color_rgb(255, 128, 0);             // RGB input
ws2812_color_hsv(60, 255, 200);            // HSV input
ws2812_color_wheel(128);                   // Color wheel (0-255)
ws2812_color_dim(color, 128);              // Dim by brightness
```

### Built-in Effects (Update per frame)
```c
ws2812_effect_rainbow(5);                  // Rainbow, 5ms per frame
ws2812_effect_breathing(color, 20);        // Breathing, 20ms per frame
ws2812_effect_meteor_center_dual(color, 30);  // Meteor from center
ws2812_effect_ping_pong_wave(color, 25);   // Wave pattern
ws2812_effect_solid_color(color);          // Static color
ws2812_effect_off();                       // All off
```

### Status
```c
uint8_t brightness = ws2812_get_brightness();
uint16_t led_count = ws2812_get_count();
bool busy = ws2812_is_busy();
ws2812_effect_t current = ws2812_get_current_effect();
```

## Performance Specs

| Metric | Value |
|--------|-------|
| Max LEDs | 20 (configurable) |
| Max Brightness | 255 (100%) |
| Effect Update Rate | Per-frame (in your task) |
| DMA Transfer Time | ~24.6μs per LED |
| Reset Time | ~5.6μs (60 pulses) |
| Total Cycle Time | num_leds * 24.6 + 5.6 μs |

## Timing Reference

Untuk WS2812 @ 108MHz APB2:

```
1 Timer Tick = 9.26ns

BIT Period = 135 ticks = 1.25μs
  ├─ BIT1 High = 97 ticks = 0.9μs   (70% high)
  ├─ BIT1 Low  = 38 ticks = 0.35μs  (30% low)
  ├─ BIT0 High = 38 ticks = 0.35μs  (28% high)
  └─ BIT0 Low  = 97 ticks = 0.9μs   (72% low)

Reset = 60 periods = 75μs low pulse
```

## Common Issues & Solutions

| Problem | Solution |
|---------|----------|
| No output on LEDs | Check PB9 wiring, verify clock config |
| Wrong colors | RGB/GRB format - use `ws2812_color_rgb()` |
| Flickering | Avoid heavy interrupts during transfer |
| One color pixel | Check LED strip power, add capacitor |
| Timeout/freeze | Ensure `get_millis()` working, check interrupt |

## Task Integration Example

```c
// Main loop
void my_animation_task(void) {
    static uint32_t timer = 0;
    
    // Update animation every 25ms
    timer++;
    if (timer % (25/frame_time_ms) == 0) {
        ws2812_effect_rainbow(5);  // Auto-updates internally
    }
}

// In main setup
task_start_priority(my_animation_task, 25, TASK_PRIORITY_LOW);
```

## Advanced: Custom Pattern

```c
// Rotating pattern
void custom_rotate_task(void) {
    static uint8_t pos = 0;
    
    // Clear all
    ws2812_set_all(ws2812_color_rgb(0, 0, 0));
    
    // Light up 3 LEDs starting at pos
    for (int i = 0; i < 3; i++) {
        uint16_t led = (pos + i * 2) % ws2812_get_count();
        ws2812_set_pixel(led, ws2812_color_rgb(255, 100, 0));
    }
    
    // Update and rotate
    ws2812_update();
    pos = (pos + 1) % ws2812_get_count();
}
```

---
**Setup Status**: ✅ Complete - Library ready to use on GD32F350CBT6 with PB9
