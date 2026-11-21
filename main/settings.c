#include "esp_log.h"
#include "nvs.h"
#include "settings.h"

#define NAMESPACE   "elk"

static const char *TAG = "settings";

settings_t settings;

void settings_load(void)
{
    settings.disk_expansion = DISK_NONE;
    settings.fast_tape = 0;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t u8;
    if (nvs_get_u8(h, "disk_expansion", &u8) == ESP_OK && u8 <= DISK_DFS) {
        settings.disk_expansion = u8;
    } else if (nvs_get_u8(h, "plus3", &u8) == ESP_OK && u8 == 1) {
        settings.disk_expansion = DISK_ADFS;
    }
    if (nvs_get_u8(h, "fast_tape", &u8) == ESP_OK && u8 <= 1) {
        settings.fast_tape = u8;
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS");
        return;
    }
    nvs_set_u8(h, "disk_expansion", settings.disk_expansion);
    nvs_set_u8(h, "fast_tape", settings.fast_tape);
    nvs_commit(h);
    nvs_close(h);
}
