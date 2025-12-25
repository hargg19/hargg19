#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>
#include <gd32f3x0.h>

// Definisi enum & struct (sama seperti sebelumnya)
typedef enum {
    TASK_STOPPED,
    TASK_RUNNING,
    TASK_SUSPENDED
} task_state_t;

typedef enum {
    TASK_PRIORITY_LOW = 0,
    TASK_PRIORITY_NORMAL,
    TASK_PRIORITY_HIGH,
    TASK_PRIORITY_CRITICAL,
    TASK_PRIORITY_COUNT
} task_priority_t;

typedef enum {
    TASK_TYPE_CALLBACK,
    TASK_TYPE_SEMAPHORE,
    TASK_TYPE_DELAYED_CALLBACK
} task_type_t;

typedef struct task task_t;
struct task {
    void (*cb)(void);
    volatile uint8_t *semaphore;
    uint32_t interval_ms;
    uint32_t counter_ms;
    uint32_t last_run_ms;
    task_state_t state;
    task_type_t type;
    bool oneshot;
    task_priority_t priority;
    uint16_t task_id;
    task_t *next;
};

typedef struct {
    task_t *head;
    task_t *tail;
    uint8_t count;
    task_priority_t priority;
} task_queue_t;

// Deklarasi fungsi
void delay_init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
uint32_t get_millis(void);
bool timeout_expired(uint32_t start, uint32_t timeout_ms);

// Task scheduling
void task_queue_init(void);
bool task_queue_add(task_t *task);
task_t* task_queue_get_next(void);
void task_queue_remove(task_t *task);

uint16_t task_start_ex(void (*cb)(void), uint32_t interval_ms, task_priority_t priority, bool oneshot);
bool task_start_priority(void (*cb)(void), uint32_t interval_ms, task_priority_t priority);
bool task_start_oneshot_priority(void (*cb)(void), uint32_t delay_ms, task_priority_t priority);
bool task_start_semaphore_priority(volatile uint8_t *sem, uint32_t interval_ms, task_priority_t priority);

bool task_suspend_by_callback(void (*cb)(void));
bool task_resume_by_callback(void (*cb)(void));
bool task_stop_by_callback(void (*cb)(void));

bool task_suspend_by_id(uint16_t task_id);
bool task_resume_by_id(uint16_t task_id);
bool task_stop_by_id(uint16_t task_id);

uint8_t get_active_task_count(void);
void task_scheduler_run(void);

#endif