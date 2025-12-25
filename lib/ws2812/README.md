# WS2812 LED Driver Library untuk GD32F350CBT6

Library ini menyediakan driver untuk mengontrol LED addressable WS2812/NeoPixel menggunakan mikrokontroler GD32F350CBT6 dengan Timer dan DMA.

## Spesifikasi Hardware

- **MCU**: GD32F350CBT6
- **Pin**: PB9
- **Timer**: TIMER16 (Ch0)
- **Clock**: 108 MHz (APB2)
- **DMA**: DMA Channel 1
- **Interrupt**: DMA_Channel1_2_IRQn

## Konfigurasi Timing WS2812

WS2812 membutuhkan sinyal PWM dengan timing yang presisi:

| Parameter | Nilai | Deskripsi |
|-----------|-------|-----------|
| WS2812_BIT_PERIOD | 135 ticks | ~1.25us (full bit period) |
| WS2812_BIT1_HIGH | 97 ticks | ~0.9us (high time untuk bit '1') |
| WS2812_BIT0_HIGH | 38 ticks | ~0.35us (high time untuk bit '0') |
| WS2812_RESET_PULSES | 60 | Reset pulse duration |

Timing didasarkan pada:
- APB2 Clock: 108 MHz
- Timer prescaler: 0 (no division)
- Setiap tick = 1/108 MHz ≈ 9.26 ns

## Fitur Utama

### Kontrol LED Dasar
- `ws2812_init()` - Inisialisasi driver
- `ws2812_set_pixel()` - Set warna LED individual
- `ws2812_set_all()` - Set semua LED dengan warna yang sama
- `ws2812_clear_all()` - Matikan semua LED
- `ws2812_update()` - Kirim data ke LED (non-blocking dengan DMA)
- `ws2812_is_busy()` - Cek status transfer DMA

### Kontrol Brightness
- `ws2812_set_brightness()` - Atur global brightness (0-255)
- `ws2812_get_brightness()` - Baca global brightness
- `ws2812_color_dim()` - Kurangi brightness warna individual

### Generator Warna
- `ws2812_color_rgb()` - Buat warna dari RGB
- `ws2812_color_hsv()` - Buat warna dari HSV
- `ws2812_color_wheel()` - Generator warna dari color wheel (0-255)

### Efek Animasi Built-in
- `ws2812_effect_solid_color()` - Warna statis
- `ws2812_effect_off()` - Matikan semua LED
- `ws2812_effect_rainbow()` - Efek pelangi berputar
- `ws2812_effect_breathing()` - Efek napas (naik-turun intensitas)
- `ws2812_effect_meteor_center_dual()` - Efek meteor dari tengah
- `ws2812_effect_ping_pong_wave()` - Efek gelombang bolak-balik

## Contoh Penggunaan

### Inisialisasi dan Setup Dasar
```c
#include "ws2812.h"

// Inisialisasi 8 LED
ws2812_init(8);
ws2812_set_brightness(200);  // 78% brightness

// Set semua LED merah
ws2812_set_all(ws2812_color_rgb(255, 0, 0));
ws2812_update();
```

### Kontrol LED Individual
```c
// Set LED ke-0 biru, LED ke-1 hijau
ws2812_set_pixel(0, ws2812_color_rgb(0, 0, 255));   // Blue
ws2812_set_pixel(1, ws2812_color_rgb(0, 255, 0));   // Green
ws2812_update();
```

### Menggunakan HSV
```c
// Buat warna merah menggunakan HSV (Hue=0, Saturation=255, Value=255)
ws2812_color_t red = ws2812_color_hsv(0, 255, 255);
ws2812_set_all(red);
ws2812_update();
```

### Efek Animasi
```c
// Efek rainbow (update setiap 5ms)
void animation_loop(void) {
    ws2812_effect_rainbow(5);
}

// Atau efek breathing merah (update setiap 20ms)
void animation_loop(void) {
    ws2812_effect_breathing(ws2812_color_rgb(255, 0, 0), 20);
}
```

### Pattern Custom dengan Loop
```c
// Gradient merah ke biru
ws2812_color_t red = ws2812_color_rgb(255, 0, 0);
ws2812_color_t blue = ws2812_color_rgb(0, 0, 255);

for (uint16_t i = 0; i < ws2812_get_count(); i++) {
    uint8_t r = red.r - (red.r * i) / ws2812_get_count();
    uint8_t b = (blue.b * i) / ws2812_get_count();
    ws2812_set_pixel(i, ws2812_color_rgb(r, 0, b));
}
ws2812_update();
```

## Prasyarat

1. **delay.h** - Harus ada fungsi `get_millis()` untuk efek animasi
2. **Interrupt Handler** - Vector table harus memanggil `DMA_Channel1_2_IRQHandler()`
3. **Clock Configuration** - APB2 clock harus 108 MHz

## Integrasi ke Vector Table

Pastikan startup file atau interrupt handler memanggil:
```c
void DMA_Channel1_2_IRQHandler(void) {
    // Dipanggil otomatis dari ws2812.c
}
```

## Catatan Implementasi

### DMA Transfer
- Menggunakan DMA Channel 1 untuk memory-to-peripheral transfer
- Data dikomunikasikan via Timer CCR0 (Capture/Compare Register)
- Non-blocking: `ws2812_update()` langsung kembali, transfer berjalan di background

### Timing Kritis
- Setiap bit WS2812 membutuhkan akurasi timing ±150ns
- Library menggunakan PWM dengan DMA untuk presisi tinggi
- Jangan interrupt selama transfer; interrupt handler sudah menangani dengan baik

### Color Format
- Internal: GRB (Green-Red-Blue) sesuai spesifikasi WS2812
- Input functions menerima RGB, auto-convert ke GRB internally
- `ws2812_color_t` struct memiliki field: `.g`, `.r`, `.b`

### Brightness Scaling
- Global brightness diterapkan di `update_internal_from_state()`
- Menggunakan rounding untuk hasil yang lebih akurat: `(value * brightness + 127) / 255`

## Troubleshooting

### LED tidak menyala
- Periksa koneksi PB9
- Verifikasi timing parameters (WS2812_BIT_PERIOD, etc.)
- Pastikan clock configuration benar

### Warna tidak sesuai
- Library menggunakan format GRB, bukan RGB
- Verifikasi menggunakan `ws2812_color_rgb()` untuk input yang lebih mudah

### Efek animasi tidak smooth
- Pastikan `get_millis()` terimplementasi dengan baik
- Increase interrupt priority jika perlu

### DMA error
- Pastikan `DMA_Channel1_2_IRQHandler()` terdaftar di vector table
- Check bahwa DMA clock (`RCU_DMA`) enabled

## Reference

- [WS2812 Datasheet](https://datasheets.maximintegrated.com/en/ds/WS2812B.pdf)
- GD32F350 Reference Manual
- GD32F3x0 SPL Library Documentation

## Lisensi

Library ini merupakan bagian dari proyek GD32F350 dan bebas digunakan untuk keperluan non-komersial.
