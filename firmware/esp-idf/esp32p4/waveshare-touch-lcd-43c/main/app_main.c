#include "app.h"

#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "app_log_policy.h"

void app_main(void)
{
    app_log_policy_apply();
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI("xiaotai_boot",
             "XIAOTAI_BOOT project=%s version=%s sdk=%s reset=%d built=%s %s",
             app != NULL ? app->project_name : "unknown",
             app != NULL ? app->version : "unknown",
             esp_get_idf_version(), (int)esp_reset_reason(),
             app != NULL ? app->date : "unknown",
             app != NULL ? app->time : "unknown");
    ESP_ERROR_CHECK(app_init());
    app_run();
}
