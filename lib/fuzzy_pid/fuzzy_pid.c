#include "fuzzy_pid.h"
#include "arm_common_tables.h"

// Konstanta untuk akurasi tinggi
#define MIN_OUTPUT_RESOLUTION 0.001f  // 0.1% resolusi
#define FILTER_ALPHA 0.1f            // Koefisien filter low-pass
#define DEADBAND_THRESHOLD 0.1f      // Threshold deadband (0.1%)

// Fungsi keanggotaan dengan smooth transition
static float tri_mf(float x, float a, float b, float c) {
    if (x <= a || x >= c) return 0.0f;
    if (x < b) {
        float t = (x - a) / (b - a);
        return t * t;  // Kuadrat untuk smoothing
    }
    float t = (c - x) / (c - b);
    return t * t;
}

static float trap_mf(float x, float a, float b, float c, float d) {
    if (x <= a || x >= d) return 0.0f;
    if (x < b) {
        float t = (x - a) / (b - a);
        return 0.5f * (1.0f - cosf(t * M_PI));  // Smooth transition
    }
    if (x <= c) return 1.0f;
    float t = (d - x) / (d - c);
    return 0.5f * (1.0f - cosf(t * M_PI));
}

// Fungsi smoothing output
static float smooth_output(float current, float target, float alpha) {
    return alpha * target + (1.0f - alpha) * current;
}

// Enhanced fuzzy inference dengan lebih banyak aturan
static void fuzzy_inference(fuzzy_pid_t *fp) {
    float e = fp->error;
    float de = fp->derivative;
    
    // Hitung error persentase untuk akurasi 0.1%
    float e_percent = (fp->setpoint != 0.0f) ? 
                     (e / fp->setpoint * 100.0f) : e;
    
    // Normalisasi berdasarkan mode
    float e_norm, de_norm;
    if (fp->mode == MODE_SOLDER_T12) {
        e_norm = e_percent / 5.0f;   // ±5% error untuk T12
        de_norm = de / (fp->setpoint * 0.05f);  // 5% dari setpoint
    } else {
        e_norm = e_percent / 10.0f;  // ±10% error untuk hot air
        de_norm = de / (fp->setpoint * 0.1f);   // 10% dari setpoint
    }
    
    // Batasi dan beri smoothing
    e_norm = fmaxf(-1.0f, fminf(1.0f, e_norm));
    de_norm = fmaxf(-1.0f, fminf(1.0f, de_norm));
    
    // Filter input untuk mengurangi noise
    static float e_filtered = 0.0f, de_filtered = 0.0f;
    e_filtered = FILTER_ALPHA * e_norm + (1 - FILTER_ALPHA) * e_filtered;
    de_filtered = FILTER_ALPHA * de_norm + (1 - FILTER_ALPHA) * de_filtered;
    
    // Fuzzifikasi - hanya gunakan 5 MF dari 9 yang ada
    float e_neg_big = trap_mf(e_filtered, -1.0f, -1.0f, -0.8f, -0.4f);
    float e_neg_small = tri_mf(e_filtered, -0.8f, -0.4f, 0.0f);
    float e_zero = tri_mf(e_filtered, -0.1f, 0.0f, 0.1f);  // Area zero diperkecil
    float e_pos_small = tri_mf(e_filtered, 0.0f, 0.4f, 0.8f);
    float e_pos_big = trap_mf(e_filtered, 0.4f, 0.8f, 1.0f, 1.0f);
    
    // Fuzzifikasi delta error - hanya gunakan 5 MF
    float de_neg_big = trap_mf(de_filtered, -1.0f, -1.0f, -0.8f, -0.4f);
    float de_neg_small = tri_mf(de_filtered, -0.8f, -0.4f, 0.0f);
    float de_zero = tri_mf(de_filtered, -0.05f, 0.0f, 0.05f);  // Area zero sangat kecil
    float de_pos_small = tri_mf(de_filtered, 0.0f, 0.4f, 0.8f);
    float de_pos_big = trap_mf(de_filtered, 0.4f, 0.8f, 1.0f, 1.0f);
    
    // 25 aturan untuk kontrol lebih halus
    float rules[25];
    int idx = 0;
    
    // Rule matrix 5x5
    float e_mf_5[5] = {e_neg_big, e_neg_small, e_zero, e_pos_small, e_pos_big};
    float de_mf_5[5] = {de_neg_big, de_neg_small, de_zero, de_pos_small, de_pos_big};
    
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            rules[idx++] = fminf(e_mf_5[i], de_mf_5[j]);
        }
    }
    
    
    // Defuzzifikasi dengan metode centroid improvement
    float kp_sum = 0.0f, ki_sum = 0.0f, kd_sum = 0.0f;
    float weight_sum = 0.0f;
    
    if (fp->mode == MODE_SOLDER_T12) {
        // T12: Respons cepat dengan overshoot minimal
        float kp_matrix[5][5] = {
            {8.0f, 6.0f, 4.0f, 3.0f, 2.0f},
            {6.0f, 4.0f, 3.0f, 2.0f, 1.5f},
            {4.0f, 3.0f, 2.0f, 1.5f, 1.0f},
            {3.0f, 2.0f, 1.5f, 1.0f, 0.8f},
            {2.0f, 1.5f, 1.0f, 0.8f, 0.5f}
        };
        
        float ki_matrix[5][5] = {
            {0.8f, 0.6f, 0.4f, 0.2f, 0.1f},
            {0.6f, 0.4f, 0.2f, 0.15f, 0.08f},
            {0.4f, 0.2f, 0.1f, 0.08f, 0.05f},
            {0.2f, 0.15f, 0.08f, 0.05f, 0.03f},
            {0.1f, 0.08f, 0.05f, 0.03f, 0.02f}
        };
        
        float kd_matrix[5][5] = {
            {0.1f, 0.2f, 0.3f, 0.4f, 0.5f},
            {0.2f, 0.3f, 0.4f, 0.5f, 0.6f},
            {0.3f, 0.4f, 0.5f, 0.6f, 0.7f},
            {0.4f, 0.5f, 0.6f, 0.7f, 0.8f},
            {0.5f, 0.6f, 0.7f, 0.8f, 1.0f}
        };
        
        // Hitung weighted sum
        idx = 0;
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 5; j++) {
                float w = rules[idx];
                kp_sum += w * kp_matrix[i][j];
                ki_sum += w * ki_matrix[i][j];
                kd_sum += w * kd_matrix[i][j];
                weight_sum += w;
                idx++;
            }
        }
    } else {
        // Hot Air: Respons lebih halus
        float kp_matrix[5][5] = {
            {4.0f, 3.0f, 2.0f, 1.5f, 1.0f},
            {3.0f, 2.0f, 1.5f, 1.0f, 0.8f},
            {2.0f, 1.5f, 1.0f, 0.8f, 0.6f},
            {1.5f, 1.0f, 0.8f, 0.6f, 0.4f},
            {1.0f, 0.8f, 0.6f, 0.4f, 0.3f}
        };
        
        float ki_matrix[5][5] = {
            {0.4f, 0.3f, 0.2f, 0.1f, 0.05f},
            {0.3f, 0.2f, 0.15f, 0.08f, 0.04f},
            {0.2f, 0.15f, 0.1f, 0.06f, 0.03f},
            {0.15f, 0.1f, 0.08f, 0.05f, 0.02f},
            {0.1f, 0.08f, 0.06f, 0.04f, 0.01f}
        };
        
        float kd_matrix[5][5] = {
            {0.05f, 0.1f, 0.15f, 0.2f, 0.25f},
            {0.1f, 0.15f, 0.2f, 0.25f, 0.3f},
            {0.15f, 0.2f, 0.25f, 0.3f, 0.35f},
            {0.2f, 0.25f, 0.3f, 0.35f, 0.4f},
            {0.25f, 0.3f, 0.35f, 0.4f, 0.5f}
        };
        
        idx = 0;
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 5; j++) {
                float w = rules[idx];
                kp_sum += w * kp_matrix[i][j];
                ki_sum += w * ki_matrix[i][j];
                kd_sum += w * kd_matrix[i][j];
                weight_sum += w;
                idx++;
            }
        }
    }
    
    // Normalisasi dan batasi
    if (weight_sum > 1e-6f) {
        fp->Kp = kp_sum / weight_sum;
        fp->Ki = ki_sum / weight_sum;
        fp->Kd = kd_sum / weight_sum;
    } else {
        // Default values jika weight_sum terlalu kecil
        if (fp->mode == MODE_SOLDER_T12) {
            fp->Kp = 2.0f;
            fp->Ki = 0.1f;
            fp->Kd = 0.5f;
        } else {
            fp->Kp = 1.0f;
            fp->Ki = 0.05f;
            fp->Kd = 0.25f;
        }
    }
    
    // Adaptive gain berdasarkan ukuran error
    float error_scale = 1.0f - fminf(fabsf(e_percent) / 10.0f, 0.9f);
    fp->Ki *= error_scale;  // Kurangi Ki saat mendekati setpoint
    
    // Batasi gain
    if (fp->mode == MODE_SOLDER_T12) {
        fp->Kp = fmaxf(0.5f, fminf(10.0f, fp->Kp));
        fp->Ki = fmaxf(0.01f, fminf(1.0f, fp->Ki));
        fp->Kd = fmaxf(0.05f, fminf(2.0f, fp->Kd));
    } else {
        fp->Kp = fmaxf(0.3f, fminf(5.0f, fp->Kp));
        fp->Ki = fmaxf(0.005f, fminf(0.5f, fp->Ki));
        fp->Kd = fmaxf(0.02f, fminf(1.0f, fp->Kd));
    }
}

void fuzzy_pid_init(fuzzy_pid_t *fp, fuzzy_mode_t mode) {
    fp->setpoint = 0.0f;
    fp->feedback = 0.0f;
    fp->dt = FUZZY_PID_DT_MS / 1000.0f;
    fp->error = 0.0f;
    fp->prev_error = 0.0f;
    fp->integral = 0.0f;
    fp->derivative = 0.0f;
    fp->output = 0.0f;
    fp->prev_output = 0.0f;
    fp->mode = mode;
    fp->max_power = (mode == MODE_SOLDER_T12) ? T12_MAX_POWER : HOT_AIR_MAX_POWER;
    
    // Inisialisasi filter
    fp->filtered_error = 0.0f;
    fp->filtered_derivative = 0.0f;
    fp->output_smoother = 0.0f;
    
    // Parameter untuk akurasi tinggi
    fp->deadband = DEADBAND_THRESHOLD;
    fp->output_resolution = MIN_OUTPUT_RESOLUTION;
    
    // Inisialisasi gain default
    if (mode == MODE_SOLDER_T12) {
        fp->Kp = 2.0f;
        fp->Ki = 0.1f;
        fp->Kd = 0.5f;
    } else {
        fp->Kp = 1.0f;
        fp->Ki = 0.05f;
        fp->Kd = 0.25f;
    }
}

void fuzzy_pid_set_mode(fuzzy_pid_t *fp, fuzzy_mode_t mode) {
    fp->mode = mode;
    fp->max_power = (mode == MODE_SOLDER_T12) ? T12_MAX_POWER : HOT_AIR_MAX_POWER;
    fuzzy_pid_reset(fp);
}

void fuzzy_pid_reset(fuzzy_pid_t *fp) {
    fp->integral = 0.0f;
    fp->prev_error = fp->error;
    fp->prev_output = fp->output;
    fp->filtered_error = 0.0f;
    fp->filtered_derivative = 0.0f;
}

void fuzzy_pid_set_setpoint(fuzzy_pid_t *fp, float setpoint) {
    fp->setpoint = setpoint;
}

float fuzzy_pid_update(fuzzy_pid_t *fp) {
    // Hitung error
    fp->error = fp->setpoint - fp->feedback;
    
    // Filter error untuk mengurangi noise
    fp->filtered_error = FILTER_ALPHA * fp->error + 
                        (1.0f - FILTER_ALPHA) * fp->filtered_error;
    
    // Hitung derivative dengan filter
    float raw_derivative = (fp->filtered_error - fp->prev_error) / fp->dt;
    fp->filtered_derivative = FILTER_ALPHA * raw_derivative + 
                             (1.0f - FILTER_ALPHA) * fp->filtered_derivative;
    fp->derivative = fp->filtered_derivative;
    
    // Update integral dengan anti-windup yang lebih canggih
    float error_percent = (fp->setpoint != 0.0f) ? 
                         (fabsf(fp->error) / fp->setpoint * 100.0f) : 
                         fabsf(fp->error);
    
    // Conditional integration: hanya integral jika error cukup besar
    if (error_percent > 0.5f) {  // Hanya integral jika error > 0.5%
        fp->integral += fp->error * fp->dt * fp->Ki;
    } else {
        // Reset integral kecil untuk menghindari windup di steady state
        fp->integral *= 0.99f;
    }
    
    // Batasi integral dengan metode clamping
    float max_integral = fp->max_power / (fp->Ki + 1e-6f);
    if (fabsf(fp->integral) > max_integral) {
        fp->integral = (fp->integral > 0) ? max_integral : -max_integral;
    }
    
    // Jalankan fuzzy inference jika error signifikan
    if (error_percent > 0.1f) {  // Hanya fuzzy jika error > 0.1%
        fuzzy_inference(fp);
    } else {
        // Mode fine-tuning: gunakan gain kecil tetap
        fp->Kp = (fp->mode == MODE_SOLDER_T12) ? 0.5f : 0.3f;
        fp->Ki = (fp->mode == MODE_SOLDER_T12) ? 0.02f : 0.01f;
        fp->Kd = (fp->mode == MODE_SOLDER_T12) ? 0.1f : 0.05f;
    }
    
    // Hitung output PID
    float proportional = fp->Kp * fp->error;
    float integral = fp->Ki * fp->integral;
    float derivative = fp->Kd * fp->derivative;
    
    float raw_output = proportional + integral + derivative;
    
    // Apply deadband untuk menghindari hunting
    if (fabsf(fp->error) < (fp->setpoint * fp->deadband / 100.0f)) {
        raw_output = fp->prev_output * 0.9f;  // Perlahan turunkan output
    }
    
    // Smoothing output
    float alpha = (error_percent > 1.0f) ? 0.5f : 0.2f;
    fp->output = smooth_output(fp->prev_output, raw_output, alpha);
    
    // Quantize output untuk resolusi 0.1%
    float output_step = fp->max_power * fp->output_resolution;
    fp->output = roundf(fp->output / output_step) * output_step;
    
    // Batasi output
    fp->output = fmaxf(0.0f, fminf(fp->max_power, fp->output));
    
    // Simpan state
    fp->prev_error = fp->filtered_error;
    fp->prev_output = fp->output;
    
    return fp->output;
}

// Fungsi untuk tuning real-time
void fuzzy_pid_tune(fuzzy_pid_t *fp, float kp_scale, float ki_scale, float kd_scale) {
    fp->Kp *= kp_scale;
    fp->Ki *= ki_scale;
    fp->Kd *= kd_scale;
}

// Fungsi untuk set deadband
void fuzzy_pid_set_deadband(fuzzy_pid_t *fp, float percent) {
    fp->deadband = fmaxf(0.01f, fminf(5.0f, percent));
}