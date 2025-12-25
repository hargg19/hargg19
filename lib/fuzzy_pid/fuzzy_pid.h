#ifndef FUZZY_PID_H
#define FUZZY_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Definisikan M_PI jika belum didefinisikan
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Mode operasi
typedef enum {
    MODE_SOLDER_T12,
    MODE_HOT_AIR
} fuzzy_mode_t;

// Struktur Fuzzy PID
typedef struct {
    // Parameter PID
    float Kp, Ki, Kd;
    float setpoint;
    float feedback;
    float dt;
    
    // State variables
    float error;
    float prev_error;
    float integral;
    float derivative;
    float output;
    float prev_output;
    
    // Filter state
    float filtered_error;
    float filtered_derivative;
    float output_smoother;
    
    // Configuration
    fuzzy_mode_t mode;
    float max_power;
    float deadband;
    float output_resolution;
} fuzzy_pid_t;

// Konstanta
#define FUZZY_PID_DT_MS 10.0f  // 10ms sampling time
#define T12_MAX_POWER 100.0f   // 100% power untuk T12
#define HOT_AIR_MAX_POWER 100.0f // 100% power untuk hot air

// Fungsi publik
void fuzzy_pid_init(fuzzy_pid_t *fp, fuzzy_mode_t mode);
void fuzzy_pid_set_mode(fuzzy_pid_t *fp, fuzzy_mode_t mode);
void fuzzy_pid_reset(fuzzy_pid_t *fp);
void fuzzy_pid_set_setpoint(fuzzy_pid_t *fp, float setpoint);
float fuzzy_pid_update(fuzzy_pid_t *fp);
void fuzzy_pid_tune(fuzzy_pid_t *fp, float kp_scale, float ki_scale, float kd_scale);
void fuzzy_pid_set_deadband(fuzzy_pid_t *fp, float percent);

#ifdef __cplusplus
}
#endif

#endif // FUZZY_PID_H