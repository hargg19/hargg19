#include "adc_sensor.h"
#include "gd32f3x0.h"
#include "delay.h"
#include <math.h>

// Buffer DMA harus berukuran 3 jika semua masuk Regular Group
// [0]=T12 (PA0), [1]=Hot Air (PA1), [2]=NTC (PA2)
static uint16_t adc_dma_buffer[3]; 
volatile adc_sensor_t g_adc_data = {0};

void adc_sensor_init(void) {
    // 1. Clock Enable
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_ADC); 
    rcu_periph_clock_enable(RCU_DMA); 
    rcu_adc_clock_config(RCU_ADCCK_AHB_DIV3);

    // 2. GPIO Konfigurasi
    gpio_mode_set(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2);

    // 3. DMA Konfigurasi (DMA_CH0 untuk ADC di GD32F3x0)
    dma_deinit(DMA_CH0);
    dma_parameter_struct dma_init_struct;
    dma_struct_para_init(&dma_init_struct);
    
    dma_init_struct.direction = DMA_PERIPHERAL_TO_MEMORY;
    dma_init_struct.memory_addr = (uint32_t)adc_dma_buffer;
    dma_init_struct.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_init_struct.periph_addr = (uint32_t)(&ADC_RDATA); // Register data regular
    dma_init_struct.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_init_struct.number = 3; // Mengambil 3 channel
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    
    dma_init(DMA_CH0, &dma_init_struct);
    dma_circulation_enable(DMA_CH0);
    

    // 4. ADC Konfigurasi (Regular Scan Mode)
    adc_deinit();
    
    adc_special_function_config(ADC_SCAN_MODE, ENABLE);
    adc_special_function_config(ADC_CONTINUOUS_MODE, ENABLE); // Trigger via Timer
    adc_data_alignment_config(ADC_DATAALIGN_RIGHT);
    
    // Urutan Scan: Rank 0=PA0, Rank 1=PA1, Rank 2=PA2
    adc_regular_channel_config(0, ADC_CHANNEL_0, ADC_SAMPLETIME_55POINT5);
    adc_regular_channel_config(1, ADC_CHANNEL_1, ADC_SAMPLETIME_239POINT5);
    adc_regular_channel_config(2, ADC_CHANNEL_2, ADC_SAMPLETIME_239POINT5);
    adc_channel_length_config(ADC_REGULAR_CHANNEL, 3);

    // Setup Trigger Hardware dari Timer 0 (PWM Valley)
    adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_T0_CH0);
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
    
     // 4. Aktifkan ADC
    adc_dma_mode_enable(); // Hubungkan ADC ke DMA
    adc_enable();
    
    delay_us(10);
    adc_calibration_enable();
    
}

void adc_sensor_start(void){
    dma_channel_enable(DMA_CH0);
}

void adc_sensor_stop(void){
    dma_channel_disable(DMA_CH0);
}


// Implementasi fungsi konversi sesuai header Anda
float adc_raw_to_voltage(uint16_t raw) {
    return (raw * ADC_VREF) / ADC_MAX_VALUE;
}

float adc_compensate_op07_bias(float adc_voltage) {
    return (adc_voltage - OP07_BIAS_VOLTAGE) / THERMOCOUPLE_GAIN;
}

float adc_calc_ambient_temp(float ntc_voltage) {
    if (ntc_voltage <= 0.05f || ntc_voltage >= (ADC_VREF - 0.05f)) return 25.0f;
    float r_ntc = (ntc_voltage * NTC_R_SERIES) / (ADC_VREF - ntc_voltage);
    float ln_r = logf(r_ntc / NTC_R0);
    float temp_k = 1.0f / ((1.0f / 298.15f) + (1.0f / NTC_BETA) * ln_r);
    return temp_k - 273.15f;
}

float adc_calc_thermocouple_temp(float tc_voltage, float ambient_temp) {
    // Voltase TC dalam Volt, THERMOCOUPLE_UV_PER_C dalam uV/C
    float compensated_voltage = tc_voltage + (ambient_temp * THERMOCOUPLE_UV_PER_C * 1e-6f);
    return compensated_voltage / (THERMOCOUPLE_UV_PER_C * 1e-6f);
}

uint8_t adc_sensor_get_data(volatile adc_sensor_t *data) {
    // Ambil raw data dari buffer DMA
    data->t12_raw      = adc_dma_buffer[0];
    data->hot_air_raw  = adc_dma_buffer[1];
    data->ntc_raw      = adc_dma_buffer[2];

    // Proses konversi sesuai rumus di header
    data->t12_voltage      = adc_raw_to_voltage(data->t12_raw);
    data->hot_air_voltage  = adc_raw_to_voltage(data->hot_air_raw);
    data->ntc_voltage      = adc_raw_to_voltage(data->ntc_raw);

    data->ambient_temp_c   = adc_calc_ambient_temp(data->ntc_voltage);
    
    // Gunakan fungsi kompensasi OP07 sebelum hitung suhu TC
    float v_tc_t12 = adc_compensate_op07_bias(data->t12_voltage);
    float v_tc_air = adc_compensate_op07_bias(data->hot_air_voltage);

    data->t12_temp_c       = adc_calc_thermocouple_temp(v_tc_t12, data->ambient_temp_c);
    data->hot_air_temp_c   = adc_calc_thermocouple_temp(v_tc_air, data->ambient_temp_c);

    data->data_ready = 1;
    return 1;
}