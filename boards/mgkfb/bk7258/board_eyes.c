#include "board_eyes.h"
#include "board_config.h"
#include <driver/gpio.h>
#include <driver/lcd_spi.h>
#include <driver/lcd.h>
#include <driver/qspi.h>
#include <os/os.h>
#include "gpio_driver.h"

static bool ready;
static int output(gpio_id_t pin, bool value)
{
    int rc=gpio_dev_unmap(pin);
    if (rc==BK_OK) rc=bk_gpio_disable_input(pin);
    if (rc==BK_OK) rc=bk_gpio_set_output_value(pin,value);
    return rc;
}
int xiaotai_board_eyes_init(void)
{
    if (ready) return BK_OK;
    int rc=output(PERIPHERAL_POWER_GPIO,true);
    if (rc==BK_OK) rc=output(EYES_BACKLIGHT_GPIO,false);
    if (rc!=BK_OK) return rc;
    rtos_delay_milliseconds(20);
    rc=bk_qspi_driver_init();
    if (rc!=BK_OK) return rc;
    const lcd_device_t *device=get_lcd_device_by_name("gc9d01");
    if (!device || !device->spi || device->width!=160 || device->height!=160) return BK_ERR_NOT_INIT;
    /* Shared init bytes, independent controllers and frame buffers. */
    lcd_spi_t spi=*device->spi;
    spi.clk=LCD_QSPI_40M;
    lcd_device_t panel=*device;
    panel.spi=&spi;
    bk_lcd_spi_init(EYES_LEFT_BUS,&panel,EYES_LEFT_RESET_GPIO,EYES_LEFT_DC_GPIO);
    bk_lcd_spi_init(EYES_RIGHT_BUS,&panel,EYES_RIGHT_RESET_GPIO,EYES_RIGHT_DC_GPIO);
    rc=output(EYES_BACKLIGHT_GPIO,true);
    if (rc==BK_OK) ready=true;
    return rc;
}
int xiaotai_board_eyes_present(uint16_t *left, uint16_t *right)
{
    if (!ready || !left || !right) return BK_ERR_NOT_INIT;
    int l=bk_lcd_spi_frame_display(EYES_LEFT_BUS,(uint8_t *)left,160U*160U*2U);
    int r=bk_lcd_spi_frame_display(EYES_RIGHT_BUS,(uint8_t *)right,160U*160U*2U);
    /* Always drain both accepted transfers, including one-panel failures. */
    int lw=l==BK_OK?bk_lcd_spi_wait_display_complete(EYES_LEFT_BUS):BK_OK;
    int rw=r==BK_OK?bk_lcd_spi_wait_display_complete(EYES_RIGHT_BUS):BK_OK;
    int rc=l!=BK_OK?l:r!=BK_OK?r:lw!=BK_OK?lw:rw;
    if (rc!=BK_OK) ready=false;
    return rc;
}
