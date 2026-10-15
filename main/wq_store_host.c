// main/wq_store_host.c —— 宿主模拟器的存档桩。
//
// 真机用 NVS（main/wq/wq_store.c）；host 模拟器没有文件系统，这里把同一份
// wq_progress 字节放在进程静态内存里。语义与真机一致：load 读出当前档、
// save 写回当前档 —— 因此一次 sim 运行内「过关 -> 回列表 -> 星级更新」这条链
// 可以被复现。该文件只出现在 game.sim.json（host），不进设备 CMakeLists。
#include "wq/wq_store.h"

#include <string.h>

static wq_progress_t s_memory; // 进程内存档（host 期间的「NVS」）。
static bool s_ready = false;

esp_err_t wq_store_init(void)
{
    if (!s_ready) {
        wq_progress_reset(&s_memory);
        s_ready = true;
    }
    return ESP_OK;
}

esp_err_t wq_store_load(wq_progress_t *progress)
{
    if (!progress) return ESP_FAIL;
    if (!s_ready) {
        wq_progress_reset(&s_memory);
        s_ready = true;
    }
    *progress = s_memory;
    return ESP_OK;
}

esp_err_t wq_store_save(const wq_progress_t *progress)
{
    if (!progress) return ESP_FAIL;
    s_memory = *progress;
    return ESP_OK;
}

esp_err_t wq_store_load_volume(uint8_t *percent)
{
    if (!percent) return ESP_FAIL;
    *percent = 80;
    return ESP_OK;
}

esp_err_t wq_store_save_volume(uint8_t percent)
{
    (void)percent;
    return ESP_OK;
}

esp_err_t wq_store_erase(void)
{
    wq_progress_reset(&s_memory);
    return ESP_OK;
}
