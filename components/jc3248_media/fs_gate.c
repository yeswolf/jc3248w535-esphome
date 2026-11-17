#include "fs_gate.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static volatile bool s_suspend;

void fs_gate_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void fs_gate_set_suspend(bool suspended)
{
    s_suspend = suspended;
}

bool fs_gate_suspended(void)
{
    return s_suspend;
}

bool fs_gate_lock(uint32_t timeout_ms)
{
    if (!s_mutex) {
        return false;
    }
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void fs_gate_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}
