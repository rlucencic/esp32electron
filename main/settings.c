#include "esp_log.h"
#include "nvs.h"
#include "settings.h"

#define NAMESPACE   "elk"

static const char *TAG = "settings";

settings_t settings;

void settings_load(void)
{
    settings.plus3 = 1;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t u8;
    if (nvs_get_u8(h, "plus3", &u8) == ESP_OK && u8 <= 1) {
        settings.plus3 = u8;
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
    nvs_set_u8(h, "plus3", settings.plus3);
    nvs_commit(h);
    nvs_close(h);
}
