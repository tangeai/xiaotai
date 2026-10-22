#include "bk_private/bk_init.h"
#include <components/log.h>
#include <components/system.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>
#include <os/os.h>

extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern int bk_ipc_init(void);

#define TAG "xiaotai_cp"

static void user_app_main(void)
{
    BK_LOGI(TAG, "CP user entry; starting AP core\n");
    bk_pm_module_vote_boot_cp1_ctrl(PM_BOOT_CP1_MODULE_NAME_APP,
                                    PM_POWER_MODULE_STATE_ON);
    BK_LOGI(TAG, "AP core start requested\n");
}

int main(void)
{
    rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    bk_init();
    BK_LOGI(TAG, "CP boot reached application entry\n");
    int ipc_rc = bk_ipc_init();
    BK_LOGI(TAG, "CP IPC init rc=%d\n", ipc_rc);
    return 0;
}
