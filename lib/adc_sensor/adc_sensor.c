#include "adc_sensor.h"
#include <gd32f3x0_adc.h>
#include "arm_math.h"
#include "delay.h"
// Buffer DMA
static uint16_t adc_dma_buffer[ADC_BUFFER_SIZE];
volatile adc_sensor_t g_adc_data = {0};

/**
 * @brief Handler Interupsi Hardware ADC
 * Nama harus sesuai dengan startup file (.s)
 */
void ADC_CMP_IRQHandler(void)
{
			// Bersihkan flag agar interupsi berikutnya bisa masuk
        adc_interrupt_flag_clear(ADC_FLAG_EOIC);
    // Cek flag End of Inserted Conversion (EOIC) untuk Channel 0 (T12)
    if (SET == adc_interrupt_flag_get(ADC_FLAG_EOIC)) {
        delay_us(1);
        // Ambil data PA0 (T12) yang dibaca saat lembah PWM
        g_adc_data.t12_raw = adc_inserted_data_read(ADC_INSERTED_CHANNEL_0);
        
        // [Tempat Logika PID Cepat Nanti]
        
        
    }
}

// Fungsi konversi
float adc_raw_to_voltage(uint16_t raw) {
    return (raw * ADC_VREF) / ADC_MAX_VALUE;
}

float adc_compensate_op07_bias(float adc_voltage) {
    return (adc_voltage - OP07_BIAS_VOLTAGE) / THERMOCOUPLE_GAIN;
}

float adc_calc_ambient_temp(float ntc_voltage) {
    // Proteksi pembagian dengan nol atau nilai di luar batas
    if (ntc_voltage <= 0.05f || ntc_voltage >= (ADC_VREF - 0.05f)) return 25.0f;

    // RUMUS UNTUK NTC DI POSISI BAWAH (GND):
    // Vout = Vref * (R_ntc / (R_series + R_ntc))
    // Maka: R_ntc = (Vout * R_series) / (Vref - Vout)
    float r_ntc = (ntc_voltage * NTC_R_SERIES) / (ADC_VREF - ntc_voltage);
    
    float ln_r = logf(r_ntc / NTC_R0);
    float temp_k = 1.0f / ((1.0f / 298.15f) + (1.0f / NTC_BETA) * ln_r);
    
    return temp_k - 273.15f;
}

float adc_calc_thermocouple_temp(float tc_voltage, float ambient_temp) {
    float compensated_voltage = tc_voltage + (ambient_temp * THERMOCOUPLE_UV_PER_C * 1e-6f);
    return compensated_voltage / (THERMOCOUPLE_UV_PER_C * 1e-6f);
}

void adc_sensor_init(void) {
    // 1. Aktifkan Clock
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_ADC); 
    rcu_periph_clock_enable(RCU_DMA); 
    // Set clock ADC (Max 28MHz, 108MHz / 6 = 18MHz -> Aman)
    rcu_adc_clock_config(RCU_ADCCK_AHB_DIV3);

    // PA0=CH0 (Inserted), PA1=CH1 (Regular), PA2=CH2 (Regular)
    gpio_mode_set(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2);

    // 2. Inisialisasi ADC
    adc_deinit();
    
    // Scan Mode: WAJIB karena total ada 3 channel (0, 1, 2)
    adc_special_function_config(ADC_SCAN_MODE, ENABLE);
    // Continuous Mode: Agar Regular Group (CH1, CH2) berputar terus di DMA
    adc_special_function_config(ADC_CONTINUOUS_MODE, ENABLE);
    adc_data_alignment_config(ADC_DATAALIGN_RIGHT);
    // --- KONFIGURASI REGULAR GROUP (CH1 & CH2) ---
    adc_regular_channel_config(0, ADC_CHANNEL_1, ADC_SAMPLETIME_239POINT5);
    adc_regular_channel_config(1, ADC_CHANNEL_2, ADC_SAMPLETIME_239POINT5);
    adc_channel_length_config(ADC_REGULAR_CHANNEL, 2);
    // Matikan external trigger untuk Regular agar bisa jalan lewat Software/Continuous
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
		adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_NONE);

    // --- KONFIGURASI INSERTED GROUP (CH0) ---
    // Rank 0, Channel 0, Sample time cepat untuk pembacaan lembah
    adc_inserted_channel_config(0, ADC_CHANNEL_0, ADC_SAMPLETIME_239POINT5); 
    adc_channel_length_config(ADC_INSERTED_CHANNEL, 1);
    adc_inserted_channel_offset_config(ADC_INSERTED_CHANNEL_0, 15);
    // Pemicu Hardware: Timer 0 TRGO (Lembah PWM)
    adc_external_trigger_source_config(ADC_INSERTED_CHANNEL, ADC_EXTTRIG_INSERTED_T0_TRGO);
    adc_external_trigger_config(ADC_INSERTED_CHANNEL, ENABLE);
		adc_resolution_config(ADC_RESOLUTION_12B);
		
    // 3. Konfigurasi DMA untuk Regular Group
    dma_deinit(DMA_CH0);
    dma_parameter_struct dma_init_struct;
    dma_struct_para_init(&dma_init_struct);
    
    dma_init_struct.direction = DMA_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_addr = (uint32_t)adc_dma_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_init_struct.periph_addr = (uint32_t)(&ADC_RDATA);
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_init_struct.number = 2; // CH1 dan CH2
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    
    dma_init(DMA_CH0, &dma_init_struct);
    dma_circulation_enable(DMA_CH0);
    dma_channel_enable(DMA_CH0);
		
		/* --- INTERRUPT & DMA --- */
    adc_interrupt_enable(ADC_INT_EOIC); // Enable interupsi untuk Inserted
    nvic_irq_enable(ADC_CMP_IRQn, 0, 0);
		
    // 4. Aktifkan ADC
    adc_dma_mode_enable(); // Hubungkan ADC ke DMA
    adc_enable();
    
    delay_ms(1);
    adc_calibration_enable();

    // Jalankan pemicu pertama untuk Regular Group
    // Karena Continuous Mode AKTIF, dia akan jalan terus selamanya
    adc_software_trigger_enable(ADC_REGULAR_CHANNEL);
}


void adc_sensor_start(void) {
    dma_channel_enable(DMA_CH0);
		
}

void adc_sensor_stop(void) {
    dma_channel_disable(DMA_CH0);
}

uint8_t adc_sensor_get_data(volatile adc_sensor_t *data) {
		__disable_irq();
    // 1. Ambil data dari Inserted Group (T12 yang dipicu PWM)
    data->t12_raw = adc_inserted_data_read(ADC_INSERTED_CHANNEL_0);

    // 2. Ambil data dari DMA Buffer (Routine Group)
    data->hot_air_raw = adc_dma_buffer[0]; // CH2
    data->ntc_raw = adc_dma_buffer[1];     // CH3

    // 3. Konversi Fisik
    data->t12_voltage = adc_compensate_op07_bias(adc_raw_to_voltage(data->t12_raw));
    data->hot_air_voltage = adc_compensate_op07_bias(adc_raw_to_voltage(data->hot_air_raw));
    data->ntc_voltage = adc_raw_to_voltage(data->ntc_raw);

    data->ambient_temp_c = adc_calc_ambient_temp(data->ntc_voltage);
    data->t12_temp_c = adc_calc_thermocouple_temp(data->t12_voltage, data->ambient_temp_c);
    data->hot_air_temp_c = adc_calc_thermocouple_temp(data->hot_air_voltage, data->ambient_temp_c);
		__enable_irq();
    return 1;
}