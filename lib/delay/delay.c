#include "delay.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_TASK 16
#define INVALID_TASK_ID 0xFFFF

// --- Global Variables ---
static task_t _tasks[MAX_TASK] = {0};
static task_queue_t _task_queues[TASK_PRIORITY_COUNT];
static volatile uint32_t systick_millis = 0;
static bool systick_initialized = false;
static uint16_t next_task_id = 1;

// --- DWT Functions (GD32 compatible) ---
static void dwt_init(void) {
    // Aktifkan trace & DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_get_cycle(void) {
    return DWT->CYCCNT;
}

static void delay_us_dwt(uint32_t us) {
    uint32_t ahb_freq = rcu_clock_freq_get(CK_AHB); // Dapatkan frekuensi AHB
    uint32_t ticks = (ahb_freq / 1000000) * us;

    uint32_t start = dwt_get_cycle();
    while ((dwt_get_cycle() - start) < ticks) {
        __asm__("nop");
    }
}

// --- SysTick Handler ---
void SysTick_Handler(void) {
    systick_millis++;

    uint32_t current_time = systick_millis;
    for (int i = 0; i < MAX_TASK; i++) {
        if (_tasks[i].state == TASK_RUNNING) {
            if (_tasks[i].counter_ms > 0) {
                _tasks[i].counter_ms--;
                if (_tasks[i].counter_ms == 0) {
                    if (task_queue_add(&_tasks[i])) {
                        _tasks[i].last_run_ms = current_time;
                        if (_tasks[i].oneshot) {
                            _tasks[i].state = TASK_STOPPED;
                        } else {
                            _tasks[i].counter_ms = _tasks[i].interval_ms;
                        }
                    }
                }
            }
        }
    }
}

// --- SysTick Init (GD32) ---
static void systick_init(void) {
    uint32_t ahb_freq = rcu_clock_freq_get(CK_AHB);
    // Reload value for 1ms (SysTick uses AHB/8 by default on GD32 unless changed)
    // But GD32F350 SysTick uses AHB clock directly if bit STK_CTL.CLKSOURCE = 1
    SysTick_Config(ahb_freq / 1000); // This sets reload, enables, and sets source = AHB

    systick_millis = 0;
    systick_initialized = true;
}

// --- System Init ---
void delay_init(void) {
    // Pastikan clock system sudah diinisialisasi
    SystemInit();

    // Inisialisasi DWT untuk delay_us
    dwt_init();

    // Inisialisasi SysTick
    systick_init();

    // Inisialisasi antrian tugas
    task_queue_init();

    for (int i = 0; i < MAX_TASK; i++) {
        _tasks[i].state = TASK_STOPPED;
        _tasks[i].cb = NULL;
        _tasks[i].semaphore = NULL;
        _tasks[i].interval_ms = 0;
        _tasks[i].counter_ms = 0;
        _tasks[i].last_run_ms = 0;
        _tasks[i].type = TASK_TYPE_CALLBACK;
        _tasks[i].oneshot = false;
        _tasks[i].priority = TASK_PRIORITY_NORMAL;
        _tasks[i].task_id = INVALID_TASK_ID;
        _tasks[i].next = NULL;
    }

    next_task_id = 1;
}

// --- Delay Functions ---
void delay_us(uint32_t us) {
    if (us == 0) return;
    delay_us_dwt(us);
}

void delay_ms(uint32_t ms) {
    if (ms == 0) return;
    uint32_t start = get_millis();
    while ((get_millis() - start) < ms) {
        __WFI(); // GD32 mendukung __WFI()
    }
}

// --- Time Functions ---
uint32_t get_millis(void) {
    if (!systick_initialized) return 0;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t current = systick_millis;
    __set_PRIMASK(primask);
    return current;
}

bool timeout_expired(uint32_t start_time, uint32_t timeout_ms) {
    uint32_t current = get_millis();
    return ((current - start_time) >= timeout_ms);
}


// --- Queue Management (Tetap) ---
void task_queue_init(void) {
    for (int i = 0; i < TASK_PRIORITY_COUNT; i++) {
        _task_queues[i].head = NULL;
        _task_queues[i].tail = NULL;
        _task_queues[i].count = 0;
        _task_queues[i].priority = (task_priority_t)i;
    }
}

bool task_queue_add(task_t *task) {
    if (task == NULL || task->priority >= TASK_PRIORITY_COUNT) {
        return false;
    }

    task_queue_t *queue = &_task_queues[task->priority];

    task_t *current = queue->head;
    while (current != NULL) {
        if (current == task) {
            return true;
        }
        current = current->next;
    }

    task->next = NULL;

    if (queue->tail == NULL) {
        queue->head = task;
        queue->tail = task;
    } else {
        queue->tail->next = task;
        queue->tail = task;
    }

    queue->count++;
    return true;
}

task_t* task_queue_get_next(void) {
    for (int priority = TASK_PRIORITY_CRITICAL; priority >= TASK_PRIORITY_LOW; priority--) {
        task_queue_t *queue = &_task_queues[priority];
        if (queue->head != NULL) {
            task_t *task = queue->head;
            queue->head = task->next;
            if (queue->head == NULL) {
                queue->tail = NULL;
            }
            queue->count--;
            task->next = NULL;
            return task;
        }
    }
    return NULL;
}

void task_queue_remove(task_t *task) {
    if (task == NULL) return;

    task_queue_t *queue = &_task_queues[task->priority];

    if (queue->head == task) {
        queue->head = task->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        queue->count--;
        task->next = NULL;
        return;
    }

    task_t *current = queue->head;
    while (current != NULL && current->next != task) {
        current = current->next;
    }

    if (current != NULL && current->next == task) {
        current->next = task->next;
        if (queue->tail == task) {
            queue->tail = current;
        }
        queue->count--;
        task->next = NULL;
    }
}

// --- Task Management (Diperbarui untuk uint16_t task_id dan tanpa use_systick) ---
static task_t* find_free_task_slot(void) {
    for (int i = 0; i < MAX_TASK; i++) {
        if (_tasks[i].state == TASK_STOPPED) {
            return &_tasks[i];
        }
    }
    return NULL;
}

static task_t* find_task_by_callback(void (*cb)(void)) {
    for (int i = 0; i < MAX_TASK; i++) {
        if (_tasks[i].cb == cb && _tasks[i].state != TASK_STOPPED) {
            return &_tasks[i];
        }
    }
    return NULL;
}

static task_t* find_task_by_id(uint16_t task_id) { // <-- Diperbarui parameter
    for (int i = 0; i < MAX_TASK; i++) {
        // Pastikan tugas tidak dalam keadaan STOPPED saat membandingkan ID
        if (_tasks[i].task_id == task_id && _tasks[i].state != TASK_STOPPED) {
            return &_tasks[i];
        }
    }
    return NULL;
}

// Diperbarui: Hapus 'use_systick', gunakan uint16_t untuk task_id
uint16_t task_start_ex(void (*cb)(void), uint32_t interval_ms, task_priority_t priority, bool oneshot) { // <-- Diperbarui return type
    task_t *task = find_free_task_slot();
    if (task == NULL) {
        return INVALID_TASK_ID;
    }

    task->cb = cb;
    task->semaphore = NULL;
    task->interval_ms = interval_ms;
    task->counter_ms = interval_ms;
    task->last_run_ms = get_millis();
    task->state = TASK_RUNNING;
    task->type = TASK_TYPE_CALLBACK;
    task->oneshot = oneshot;
    // task->use_systick = true; // <-- Dihapus
    task->priority = priority;
    task->task_id = next_task_id++;
    task->next = NULL;

    // Tangani overflow next_task_id
    if (next_task_id == 0) next_task_id = 1; // Hindari ID 0

    return task->task_id; // <-- Diperbarui
}

// Diperbarui: Hapus 'use_systick', gunakan uint16_t untuk task_id
bool task_start_priority(void (*cb)(void), uint32_t interval_ms, task_priority_t priority) {
    uint16_t id = task_start_ex(cb, interval_ms, priority, false); // <-- Diperbarui
    return (id != INVALID_TASK_ID);
}

// Diperbarui: Hapus 'use_systick', gunakan uint16_t untuk task_id
bool task_start_oneshot_priority(void (*cb)(void), uint32_t delay_ms, task_priority_t priority) {
    uint16_t id = task_start_ex(cb, delay_ms, priority, true); // <-- Diperbarui
    return (id != INVALID_TASK_ID);
}

// Diperbarui: Hapus 'use_systick', gunakan uint16_t untuk task_id
bool task_start_semaphore_priority(volatile uint8_t *sem, uint32_t interval_ms, task_priority_t priority) {
    task_t *task = find_free_task_slot();
    if (task == NULL) {
        return false;
    }

    task->cb = NULL;
    task->semaphore = sem;
    task->interval_ms = interval_ms;
    task->counter_ms = interval_ms;
    task->last_run_ms = get_millis();
    task->state = TASK_RUNNING;
    task->type = TASK_TYPE_SEMAPHORE;
    task->oneshot = false;
    // task->use_systick = true; // <-- Dihapus
    task->priority = priority;
    task->task_id = next_task_id++;
    task->next = NULL;

    // Tangani overflow next_task_id
    if (next_task_id == 0) next_task_id = 1; // Hindari ID 0

    return true;
}

// ... (Fungsi kontrol task lainnya seperti suspend/resume/stop) ...

bool task_suspend_by_callback(void (*cb)(void)) {
    task_t *task = find_task_by_callback(cb);
    if (task != NULL && task->state == TASK_RUNNING) {
        task->state = TASK_SUSPENDED;
        task_queue_remove(task);
        return true;
    }
    return false;
}

bool task_resume_by_callback(void (*cb)(void)) {
    task_t *task = find_task_by_callback(cb);
    if (task != NULL && task->state == TASK_SUSPENDED) {
        task->state = TASK_RUNNING;
        task->last_run_ms = get_millis();
        task->counter_ms = task->interval_ms;
        return false;
    }
    return false;
}

bool task_stop_by_callback(void (*cb)(void)) {
    task_t *task = find_task_by_callback(cb);
    if (task != NULL) {
        task->state = TASK_STOPPED;
        task->cb = NULL;
        task->semaphore = NULL;
        task->interval_ms = 0;
        task->counter_ms = 0;
        task->oneshot = false;
        // task->use_systick = false; // <-- Dihapus
        task->task_id = INVALID_TASK_ID; // <-- Diperbarui
        task_queue_remove(task);
        return true;
    }
    return false;
}

// Diperbarui: Gunakan uint16_t untuk task_id
bool task_suspend_by_id(uint16_t task_id) { // <-- Diperbarui parameter
    task_t *task = find_task_by_id(task_id);
    if (task != NULL && task->state == TASK_RUNNING) {
        task->state = TASK_SUSPENDED;
        task_queue_remove(task);
        return true;
    }
    return false;
}

// Diperbarui: Gunakan uint16_t untuk task_id
bool task_resume_by_id(uint16_t task_id) { // <-- Diperbarui parameter
    task_t *task = find_task_by_id(task_id);
    if (task != NULL && task->state == TASK_SUSPENDED) {
        task->state = TASK_RUNNING;
        task->last_run_ms = get_millis();
        task->counter_ms = task->interval_ms;
        return true;
    }
    return false;
}

// Diperbarui: Gunakan uint16_t untuk task_id
bool task_stop_by_id(uint16_t task_id) { // <-- Diperbarui parameter
    task_t *task = find_task_by_id(task_id);
    if (task != NULL) {
        task->state = TASK_STOPPED;
        task->cb = NULL;
        task->semaphore = NULL;
        task->interval_ms = 0;
        task->counter_ms = 0;
        task->oneshot = false;
        // task->use_systick = false; // <-- Dihapus
        task->task_id = INVALID_TASK_ID; // <-- Diperbarui
        task_queue_remove(task);
        return true;
    }
    return false;
}

uint8_t get_active_task_count(void) {
    uint8_t count = 0;
    for (int i = 0; i < MAX_TASK; i++) {
        if (_tasks[i].state != TASK_STOPPED) {
            count++;
        }
    }
    return count;
}

void task_scheduler_run(void) {
    task_t *task = task_queue_get_next();
    if (task != NULL) {
        switch (task->type) {
            case TASK_TYPE_SEMAPHORE:
                if (task->semaphore != NULL) {
                    *task->semaphore = 1;
                }
                break;

            case TASK_TYPE_CALLBACK:
            case TASK_TYPE_DELAYED_CALLBACK:
                if (task->cb != NULL) {
                    task->cb();
                }
                break;
        }
    }
}