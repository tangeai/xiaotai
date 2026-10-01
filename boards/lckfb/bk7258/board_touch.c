#include "board_touch.h"
#include "board_config.h"

#include <common/bk_err.h>
#include <os/os.h>
#include <driver/drv_tp.h>
#include <driver/tp.h>
#include <driver/tp_types.h>
#include <stdint.h>

#include "xiaotai_log.h"

#define TAG "xiaotai_board_touch"

int xiaotai_board_touch_open(void)
{
    int rc = drv_tp_open(TOUCH_NATIVE_WIDTH, TOUCH_NATIVE_HEIGHT,
                         TP_MIRROR_NONE);
    tp_device_t *device = bk_tp_get_device();
    if (rc != kNoErr || device == NULL || device->name == NULL) {
        BK_LOGE(TAG, "FT6336 initialization failed rc=%d bus=GPIO42/43\n",
                rc);
        (void)drv_tp_close();
        return rc == kNoErr ? BK_FAIL : rc;
    }
    BK_LOGI(TAG,
            "touch ready controller=%s I2C=GPIO42/43 INT=GPIO45 RST=GPIO46 landscape\n",
            device->name);
    return BK_OK;
}

int xiaotai_board_touch_read(xiaotai_board_touch_point_t *point)
{
    if (point == NULL) return BK_ERR_PARAM;
    tp_point_infor_t native = {0};
    int rc = drv_tp_read(&native);
    if (rc != kNoErr) return rc;
    point->x = native.m_x;
    point->y = native.m_y;
    point->pressed = native.m_state != 0U;
    point->more = native.m_need_continue != 0U;
    point->cancelled = !point->pressed && native.m_x == UINT16_MAX &&
                       native.m_y == UINT16_MAX;
    return BK_OK;
}

void xiaotai_board_touch_close(void)
{
    (void)drv_tp_close();
}
