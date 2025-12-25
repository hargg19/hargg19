/*
 * WS2812 Interrupt Setup Guide
 * 
 * File ini menunjukkan bagaimana setup interrupt handler
 * untuk DMA Channel 1-2 di GD32F350
 */

/*
 * =====================================================
 * VECTOR TABLE SETUP
 * =====================================================
 * 
 * GD32F350 memiliki combined IRQ untuk DMA:
 * - DMA_Channel0_IRQHandler
 * - DMA_Channel1_2_IRQHandler  <-- Digunakan WS2812
 * - DMA_Channel3_4_IRQHandler
 * - DMA_Channel5_6_IRQHandler
 * 
 * WS2812 library mendeclare DMA_Channel1_2_IRQHandler
 * di ws2812.h dan implements di ws2812.c
 */

/*
 * =====================================================
 * OPSI 1: Dengan SPL Startup File (PlatformIO GD32)
 * =====================================================
 * 
 * Jika menggunakan startup file bawaan SPL, handler sudah
 * terdaftar otomatis. Yang perlu Anda lakukan:
 * 
 * 1. Include ws2812.h di file Anda
 * 2. Library secara otomatis register IRQ saat ws2812_init()
 * 3. Done!
 * 
 * Vector table entry di SPL sudah menunjuk ke:
 *   void DMA_Channel1_2_IRQHandler(void)
 */

/*
 * =====================================================
 * OPSI 2: Custom Startup File
 * =====================================================
 * 
 * Jika menggunakan custom startup file, pastikan
 * vector table ada entry untuk DMA Channel 1-2:
 */

/*
// Contoh di startup file (assembly atau C):

// Vector table entry
extern void DMA_Channel1_2_IRQHandler(void);

// Dalam vector table array:
const void (*g_pfnVectors[])(void) = {
    // ... other handlers ...
    DMA_Channel1_2_IRQHandler,    // IRQ 33 di GD32F350
    // ... more handlers ...
};

// Atau di linker script:
EXTERN(DMA_Channel1_2_IRQHandler)
*/

/*
 * =====================================================
 * OPSI 3: C-based Vector Table (Untuk Custom Setup)
 * =====================================================
 */

/*
#include "ws2812.h"

// Declare external handler (dari ws2812.c)
extern void DMA_Channel1_2_IRQHandler(void);

// Custom vector table
void (* const g_pfnVectors[])(void) = {
    // ... system handlers ...
    
    DMA_Channel1_2_IRQHandler,  // DMA Channel 1-2 interrupt
    
    // ... more handlers ...
};

// Enable NVIC interrupt
void setup_dma_interrupt(void) {
    // nvic_irq_enable sudah dipanggil oleh ws2812_init()
    // tapi jika perlu manual:
    nvic_irq_enable(DMA_Channel1_2_IRQn, 1, 0);
}
*/

/*
 * =====================================================
 * VERIFY SETUP
 * =====================================================
 */

/*
Untuk verifikasi handler sudah terdaftar:

1. Compile project - tidak boleh ada linker error
2. Check objdump untuk confirm handler ada:
   arm-none-eabi-objdump -t build_output | grep DMA_Channel1_2_IRQHandler
3. Coba jalankan ws2812_init() dan ws2812_update()
4. Monitor dengan debugger atau LED indicator
*/

/*
 * =====================================================
 * INTERRUPT PRIORITY
 * =====================================================
 */

/*
WS2812 DMA interrupt priority di set ke:
- Priority Group (PRIGROUP): Preemption = 1, Sub = 0
- Preemption Priority: 1
- Sub Priority: 0

Ini berarti:
- Bisa di-interrupt oleh handler dengan preemption priority 0
- Tidak bisa di-interrupt oleh handler dengan preemption priority > 1

Default OK untuk kebanyakan aplikasi. Jika perlu ubah,
edit di ws2812.c:

    nvic_irq_enable(WS2812_DMA_IRQn, 1U, 0U);
                                    ^^  ^^
                                  prio sub

*/

/*
 * =====================================================
 * DEBUGGING
 * =====================================================
 */

/*
Jika handler tidak dipanggil:

1. Verifikasi IRQ number benar:
   WS2812_DMA_IRQn = DMA_Channel1_2_IRQn
   
2. Cek DMA interrupt flag di handler:
   if (dma_interrupt_flag_get(WS2812_DMA_CHANNEL, DMA_INT_FLAG_FTF))
   
3. Enable NVIC interrupt:
   nvic_irq_enable(WS2812_DMA_IRQn, 1U, 0U);
   
4. Cek DMA channel enable:
   dma_channel_enable(WS2812_DMA_CHANNEL);
   
5. Verifikasi timer trigger:
   timer_dma_enable(WS2812_TIMER, TIMER_DMA_CH0D);

*/

/*
 * =====================================================
 * DENGAN RTOS/FREERTOS
 * =====================================================
 */

/*
Jika menggunakan RTOS seperti FreeRTOS:

1. WS2812 library already interrupt-safe
2. Handler tidak menggunakan blocking calls
3. Safe untuk call dari task atau ISR context
4. Pastikan priority benar di FreeRTOS config:
   
   configMAX_SYSCALL_INTERRUPT_PRIORITY = 5
   WS2812 priority = 1 (higher priority = lower number)
*/

#endif
