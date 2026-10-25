// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// parallel.h on the ESP32-P4: part 1 runs on a worker task pinned to core 1, part 0 on the caller (core 0).
// The worker waits on a task notification; the caller waits for the worker's semaphore after its own part (not on a
// task notification of its own: other code may notify the calling task, e.g. a USB driver on a finished transfer, and
// pq_par_run would return before core 1 had finished; the semaphore is given only by the worker).
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "runtime/parallel.h"

static TaskHandle_t worker;
static SemaphoreHandle_t done;
static volatile pq_par_fn job_fn;
static void *volatile job_ctx;
int pq_par_single;
uint32_t pq_par_calls, pq_par_wait_cycles;

static inline uint32_t cyc(void)
{
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

static void worker_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        job_fn(job_ctx, 1, PQ_PAR_PARTS);
        xSemaphoreGive(done);
    }
}

void pq_par_init(void)
{
    if (!worker) {
        done = xSemaphoreCreateBinary();
        xTaskCreatePinnedToCore(worker_task, "par1", 16384, NULL, configMAX_PRIORITIES - 2, &worker, 1);
    }
}

void pq_par_run(pq_par_fn fn, void *ctx)
{
    pq_par_calls++;
    if (pq_par_single || !worker) {   // no worker yet (before pq_par_init): both parts here
        fn(ctx, 0, PQ_PAR_PARTS);
        fn(ctx, 1, PQ_PAR_PARTS);
        return;
    }
    job_fn = fn;
    job_ctx = ctx;
    xTaskNotifyGive(worker);
    fn(ctx, 0, PQ_PAR_PARTS);
    uint32_t c0 = cyc();
    xSemaphoreTake(done, portMAX_DELAY);
    pq_par_wait_cycles += cyc() - c0;   // core 0 waiting for core 1 (imbalance + wake-up latency)
}
