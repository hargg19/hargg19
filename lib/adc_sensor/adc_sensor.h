#ifndef ADC_SENSOR_H
#define ADC_SENSOR_H

#include <gd32f3x0.h>
#include <stdint.h>

// Konstanta
#define ADC_BUFFER_SIZE         3
#define ADC_VREF                3.3f
#define ADC_MAX_VALUE           4095.0f

// Kalibrasi OP07
#define OP07_BIAS_VOLTAGE       0.0f
#define THERMOCOUPLE_GAIN       146.0f
#define THERMOCOUPLE_UV_PER_C   40.0f

// Kalibrasi NTC
#define NTC_R0                  10000.0f
#define NTC_BETA                3950.0f
#define NTC_R_SERIES            10000.0f



// Struktur data sensor
typedef struct {
    uint16_t t12_raw;
    uint16_t hot_air_raw;
    uint16_t ntc_raw;
    float t12_voltage;
    float hot_air_voltage;
    float ntc_voltage;
    float t12_temp_c;
    float hot_air_temp_c;
    float ambient_temp_c;
    uint8_t data_ready;
} adc_sensor_t;

// Fungsi API
void adc_sensor_init(void);
void adc_sensor_start(void);
void adc_sensor_stop(void);
// Di adc_sensor.h
uint8_t adc_sensor_get_data(volatile adc_sensor_t *data);

// Fungsi konversi (publik jika perlu)
float adc_raw_to_voltage(uint16_t raw);
float adc_compensate_op07_bias(float adc_voltage);
float adc_calc_ambient_temp(float ntc_voltage);
float adc_calc_thermocouple_temp(float tc_voltage, float ambient_temp);
// Tambahkan ini di bagian akhir adc_sensor.h, sebelum #endif
extern volatile adc_sensor_t g_adc_data;

#endif