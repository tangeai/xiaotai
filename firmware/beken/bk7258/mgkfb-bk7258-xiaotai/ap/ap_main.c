#include <components/log.h>
#include <components/system.h>
#include <media_service.h>
#include <os/os.h>

#include "bk_private/bk_init.h"
#include "xiaotai_app.h"
#include "board_audio.h"
#include "xiaotai_console.h"
#include "xiaotai_metrics.h"
#include "xiaotai_ui.h"
#include "xiaotai_video.h"
#include "tirtc_bk_probe.h"
#include "xiaotai_build_identity.h"

#define TAG "xiaotai_main"

int main(void)
{
    bk_init();
    BK_LOGI(TAG, "AP boot reached application entry\n");
    bk_printf_raw(BK_LOG_NONE, NULL,
            "XIAOTAI_BOOT project=mgkfb-bk7258-xiaotai version=%s build=%s sdk=bk_avdk_smp/release/v3.1.1.10 reset=%u\n",
            XIAOTAI_BUILD_VERSION, XIAOTAI_BUILD_ID,
            (unsigned)bk_misc_get_reset_reason());
    BK_LOGI(TAG, "media service initialization starting\n");
    media_service_init();
    BK_LOGI(TAG, "media service initialization complete\n");
    (void)xiaotai_board_amplifier_idle();
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
