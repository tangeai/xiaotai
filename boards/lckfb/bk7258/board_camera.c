#include "board_camera.h"
#include "board_config.h"

#include <components/bk_camera_ctlr.h>
#include <components/bk_camera_ctlr_types.h>
#include <driver/gpio.h>
#include <os/os.h>
#include <stdbool.h>

#include "gpio_driver.h"
#include "xiaotai_log.h"

#define TAG "xiaotai_board_camera"
#define VIDEO_WIDTH 640U
#define VIDEO_HEIGHT 480U
#define CAMERA_POWER_SETTLE_MS 100U
#define GPIO_INVALID_ID 0xffU

static bk_camera_ctlr_handle_t s_camera;
static bool s_started;

static bk_err_t camera_power(bool enabled)
{
#ifdef CONFIG_DVP_CTRL_POWER_GPIO_ID
    if (CONFIG_DVP_CTRL_POWER_GPIO_ID != GPIO_INVALID_ID) {
        gpio_id_t pin = (gpio_id_t)CONFIG_DVP_CTRL_POWER_GPIO_ID;
        bk_err_t rc = gpio_dev_unmap(pin);
        if (rc == BK_OK) rc = bk_gpio_enable_output(pin);
        if (rc == BK_OK) {
            rc = enabled ? bk_gpio_set_output_high(pin) :
                           bk_gpio_set_output_low(pin);
        }
        if (rc != BK_OK) {
            BK_LOGE(TAG, "camera power GPIO%u enabled=%d failed rc=%d\n",
                    (unsigned)pin, enabled, rc);
            return rc;
        }
        if (enabled) rtos_delay_milliseconds(CAMERA_POWER_SETTLE_MS);
        BK_LOGI(TAG, "camera power GPIO%u enabled=%d readback=%d\n",
                (unsigned)pin, enabled, bk_gpio_get_output(pin));
    }
#else
    (void)enabled;
#endif
    return BK_OK;
}

bk_err_t xiaotai_board_camera_reserve(const bk_dvp_callback_t *callbacks)
{
    if (callbacks == NULL) return BK_ERR_PARAM;
    if (s_camera != NULL) return BK_OK;

    bk_dvp_ctlr_config_t config = {
        .config = BK_DVP_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = callbacks,
    };
    config.config.img_format = IMAGE_H264;
    config.config.width = VIDEO_WIDTH;
    config.config.height = VIDEO_HEIGHT;
    config.config.fps = FPS20;
    config.config.pwdn_pin = CAMERA_PWDN_GPIO;
    config.config.reset_pin = CAMERA_RESET_GPIO;
    config.config.i2c_config.scl_pin = CAMERA_SCCB_SCL_GPIO;
    config.config.i2c_config.sda_pin = CAMERA_SCCB_SDA_GPIO;

    avdk_err_t rc = bk_camera_dvp_ctlr_new(&s_camera, &config);
    if (rc != AVDK_ERR_OK) {
        BK_LOGE(TAG, "DVP controller reservation failed rc=%d bytes=%u\n",
                rc, (unsigned)(VIDEO_WIDTH * 32U * 2U));
        s_camera = NULL;
        return BK_ERR_NO_MEM;
    }
    BK_LOGI(TAG, "DVP H264 controller reserved bytes=%u\n",
            (unsigned)(VIDEO_WIDTH * 32U * 2U));
    return BK_OK;
}

bk_err_t xiaotai_board_camera_start(void)
{
    if (s_camera == NULL) return BK_ERR_NOT_INIT;
    bk_err_t rc = camera_power(true);
    if (rc != BK_OK) return rc;

    avdk_err_t camera_rc = bk_camera_open(s_camera);
    BK_LOGI(TAG, "camera control PWDN GPIO%u=%d RESET GPIO%u=%d\n",
            (unsigned)CAMERA_PWDN_GPIO,
            bk_gpio_get_output(CAMERA_PWDN_GPIO),
            (unsigned)CAMERA_RESET_GPIO,
            bk_gpio_get_output(CAMERA_RESET_GPIO));
    if (camera_rc != AVDK_ERR_OK) {
        (void)camera_power(false);
        BK_LOGE(TAG, "DVP camera open failed rc=%d\n", camera_rc);
        return BK_FAIL;
    }
    s_started = true;
    return BK_OK;
}

void xiaotai_board_camera_stop(void)
{
    if (s_started) bk_camera_close(s_camera);
    s_started = false;
    (void)camera_power(false);
}
