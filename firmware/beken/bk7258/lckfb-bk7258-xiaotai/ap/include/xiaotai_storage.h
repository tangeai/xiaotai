#ifndef XIAOTAI_STORAGE_H
#define XIAOTAI_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "xiaotai_platform.h"

#define XIAOTAI_WIFI_SSID_MAX 32
#define XIAOTAI_WIFI_PASSWORD_MAX 64

typedef struct {
    char ssid[XIAOTAI_WIFI_SSID_MAX + 1];
    char password[XIAOTAI_WIFI_PASSWORD_MAX + 1];
} xiaotai_wifi_credentials_t;

typedef struct {
    uint8_t volume;
    bool speaker_muted;
    bool microphone_muted;
    uint8_t screen_timeout_index;
    uint8_t home_face;
    uint8_t microphone_sensitivity;
} xiaotai_product_settings_t;

bool xiaotai_wifi_credentials_valid(const xiaotai_wifi_credentials_t *value);
int xiaotai_storage_load_wifi(xiaotai_wifi_credentials_t *out);
int xiaotai_storage_save_wifi(const xiaotai_wifi_credentials_t *value);
/** Delete only the saved station credentials; binding/settings are retained. */
int xiaotai_storage_clear_wifi(void);
int xiaotai_storage_load_device(xiaotai_device_credentials_t *out);
int xiaotai_storage_save_device(const xiaotai_device_credentials_t *value);
int xiaotai_storage_clear_device(void);
/** Clear only user keys; on success reject later saves until reboot. */
int xiaotai_storage_reset_user_data(void);
void xiaotai_product_settings_default(xiaotai_product_settings_t *out);
int xiaotai_storage_load_settings(xiaotai_product_settings_t *out);
int xiaotai_storage_save_settings(const xiaotai_product_settings_t *value);

#endif
