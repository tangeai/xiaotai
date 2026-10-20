"""CoreS3-Lite build selection and actual board-boundary regressions."""
import subprocess
import sys
import unittest

from test_s3_product_regressions import ROOT, function, run_c

sys.path.insert(0, str(ROOT / "tools"))
from board_registry import build_command, find_board, load_boards
from firmware_version import read_board_version

BOARD = ROOT / "boards/m5stack/cores3-lite"


class CoreS3BoardTest(unittest.TestCase):
    def test_registered_build_selects_one_panel_and_own_version(self):
        board = find_board(load_boards(ROOT), "m5stack-cores3-lite")
        for variant in ("ili9342c", "ili9342e"):
            command, project = build_command(ROOT, board, variant)
            self.assertIn(f"-DXIAOTAI_BOARD_VARIANT={variant}", command)
            self.assertEqual(project.name, "m5stack-cores3-lite")
        self.assertEqual(str(read_board_version(ROOT, board.id)), "1.0.1")
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/build.py"), "--board", board.id,
             "--variant", "ili9342c", "--dry-run"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("touch-full", result.stdout)

    def test_capture_normalizes_mic1_and_electrical_reference(self):
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "board_audio_slots.h"
int main(void) {
    const int16_t wire[] = {100,200,-300,400, -10,-20,30,-40};
    int16_t out[] = {0,0,0,0,1234};
    cores3_audio_unpack(out, wire, 2);
    assert(out[0]==100 && out[1]==200 && out[2]==-10 && out[3]==-20);
    assert(out[4]==1234);
    cores3_audio_unpack(out, wire, 0);
    assert(out[0]==100);
}
''', [BOARD])

    def test_camera_power_preserves_audio_backlight_and_usb_direction(self):
        source = (BOARD / "board_power.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int i2c_master_dev_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 1000
static bool s_power_ready;
static int s_control=1, s_axp=1, s_aw=2, held, fail, writes;
static uint8_t regs[3][256];
static void xSemaphoreTake(int lock,int timeout){assert(!held);held=1;}
static void xSemaphoreGive(int lock){assert(held);held=0;}
static void vTaskDelay(unsigned ms){assert(held);}
static int write_reg(int dev,uint8_t reg,uint8_t value){
    assert(held); if(fail)return 9; regs[dev][reg]=value;writes++;return 0;
}
static int update_reg(int dev,uint8_t reg,uint8_t mask,uint8_t value){
    return write_reg(dev,reg,(regs[dev][reg]&~mask)|(value&mask));
}
''' + function(source, "cores3_board_camera_power") +
              function(source, "cores3_board_backlight") + r'''
int main(void) {
    assert(cores3_board_camera_power(true)==ESP_ERR_INVALID_STATE);
    s_power_ready=true; regs[1][0x90]=0xab; regs[2][3]=0x82;
    assert(cores3_board_camera_power(true)==0);
    assert(regs[1][0x90]==0xbf && regs[2][3]==0x83);
    assert(regs[1][0x94]==28);
    assert(cores3_board_camera_power(false)==0);
    assert(regs[1][0x90]==0xab && regs[2][3]==0x82);
    assert(cores3_board_backlight(false)==0 && regs[1][0x90]==0x2b);
    assert(cores3_board_backlight(true)==0 && regs[1][0x90]==0xab);
    fail=1; int before=writes;
    assert(cores3_board_camera_power(true)==9 && writes==before && !held);
}
''')

    def test_panel_revision_uses_its_own_extended_commands(self):
        source = (BOARD / "board_display.c").read_text()
        tables = source[source.index("#if defined(XIAOTAI_PANEL_ILI9342C)"):
                        source.index("esp_err_t szpi_board_display_init")]
        for define, unlock, gamma in (("#define XIAOTAI_PANEL_ILI9342C 1", 0xc8, 0xe0),
                                       ("", 0xdd, 0xe4)):
            run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {int cmd; const void *data; size_t data_bytes; unsigned delay_ms;}
    ili9341_lcd_init_cmd_t;
''' + define + "\n" + tables + f'''
int main(void) {{
    assert(s_panel_init[0].cmd == {unlock});
    unsigned found=0;
    for(unsigned i=0; i<sizeof(s_panel_init)/sizeof(s_panel_init[0]); ++i)
        found += s_panel_init[i].cmd == {gamma};
    assert(found==1);
}}
''')

    def test_no_application_key_claims_the_audio_master_clock(self):
        source = (BOARD / "board_button.c").read_text()
        run_c(r'''
#include <assert.h>
#define ESP_OK 0
typedef int esp_err_t;
''' + function(source, "starter_button_start") + r'''
int main(void) {
    assert(starter_button_start()==ESP_OK);
}
''')

    def test_platform_profile_reports_selected_board(self):
        source = (ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#define CONFIG_IDF_TARGET_ESP32P4 0
#define XIAOTAI_BOARD_MODEL "m5stack-cores3-lite"
#define PLATFORM_SERVICE_DEVICE 1
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define VOIP_PROFILE_RETRY_MS 1000
typedef int esp_err_t;
static bool s_voip_profile_inflight;
static unsigned s_voip_profile_retry_at_ms;
static bool platform_client_ready(void){return true;}
static bool platform_client_mqtt_connected(void){return true;}
static unsigned now_ms(void){return 1;}
static void voip_profile_response(void){}
static int platform_client_request_timeout(int service,const char *url,
    const char *body,unsigned timeout,void (*callback)(void),void *context){
    assert(strstr(body,"\"board_model\":\"m5stack-cores3-lite\""));
    assert(strstr(body,"\"up_video_mt\":[\"mjpeg\"]"));
    assert(strstr(body,"\"no_video\":true"));
    return ESP_OK;
}
''' + function(source, "request_device_profile") + r'''
int main(void) {
    request_device_profile();
    assert(s_voip_profile_inflight);
}
''')


if __name__ == "__main__":
    unittest.main()
