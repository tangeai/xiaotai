#include <components/log.h>
#include <media_service.h>
#include <os/os.h>

#include "bk_private/bk_init.h"
#include "xiaotai_app.h"
#include "xiaotai_console.h"
#include "xiaotai_metrics.h"
#include "xiaotai_ui.h"
#include "xiaotai_video.h"
#include "tirtc_bk_probe.h"

#define TAG "xiaotai_main"

int main(void)
{
    bk_init();
    BK_LOGI(TAG, "AP boot reached application entry\n");
    BK_LOGI(TAG, "media service initialization starting\n");
    media_service_init();
    BK_LOGI(TAG, "media service initialization complete\n");
    (void)xiaotai_ui_init();
    if (xiaotai_video_init() != 0) {
        BK_LOGE(TAG, "camera SRAM reservation failed\n");
    }

    /* Read-only board snapshot.  Active audio/touch tests remain explicit
     * operator actions and are never started during unattended boot. */
    tirtc_bk_probe_emit();
    if (xiaotai_metrics_start() != 0) {
        BK_LOGW(TAG, "resource metrics task unavailable\n");
    }
    if (xiaotai_console_start() != 0) {
        BK_LOGW(TAG, "XiaoTai console command registration failed\n");
    }

    int rc = xiaotai_app_start();
    if (rc != 0) {
        BK_LOGE(TAG, "application start failed rc=%d\n", rc);
    }
    return rc;
}
