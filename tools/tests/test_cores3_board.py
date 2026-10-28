"""CoreS3-Lite build selection and actual board-boundary regressions."""
import subprocess
import sys
import unittest

from test_s3_product_regressions import ROOT, function, run_c
from test_camera_rotation_report import runtime_config

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

    def test_codec_initialization_uses_official_amplifier_gain_compensation(self):
        source = (BOARD / "board_adapter.c").read_text()
        # Execute the production initialization at the codec creation boundary.
        # The 15 dB contract comes from the pinned Espressif CoreS3 BSP, not
        # from this project's initializer; a zero-filled hw_gain is the defect.
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_CODEC_DEV_OK 0
#define I2S_AUDIO_PORT 0
#define BOARD_I2C_PORT 0
#define BOARD_I2C_HZ 400000
#define AW88298_CODEC_DEFAULT_ADDR 0x6c
#define ES7210_CODEC_DEFAULT_ADDR 0x80
#define ESP_CODEC_DEV_TYPE_OUT 1
#define ESP_CODEC_DEV_TYPE_IN 2
#define AUDIO_HW_SAMPLE_RATE_HZ 16000
#define AUDIO_TDM_SLOTS 4
#define AUDIO_MCLK_MULTIPLE 256
#define ES7210_SEL_MIC1 1
#define ES7210_SEL_MIC2 2
#define ES7210_SEL_MIC3 4
#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(c) (1U << (c))
typedef struct {int port; void *rx_handle,*tx_handle;} audio_codec_i2s_cfg_t;
typedef struct {int port,addr,clock_speed_hz;void *bus_handle;} audio_codec_i2c_cfg_t;
typedef struct {float pa_gain,pa_voltage,codec_dac_voltage;} hw_gain_t;
typedef struct {void *ctrl_if;int reset_pin;hw_gain_t hw_gain;} aw88298_codec_cfg_t;
typedef struct {int dev_type;void *codec_if,*data_if;} esp_codec_dev_cfg_t;
typedef struct {int sample_rate,bits_per_sample,channel,channel_mask;} esp_codec_dev_sample_info_t;
typedef struct {void *ctrl_if;bool master_mode;int mic_selected,mclk_div;} es7210_codec_cfg_t;
static void *s_i2s_rx,*s_i2s_tx,*s_i2c_bus,*s_i2s_data_if,*s_amp_ctrl_if;
static void *s_amp_codec_if,*s_speaker_dev,*s_es7210_ctrl_if,*s_es7210_codec_if,*s_microphone_dev;
static int amp_created,clock_checked,mic_gain,reference_gain;
static void *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *c){return (void *)1;}
static void *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *c){return (void *)1;}
static void *aw88298_codec_new(const aw88298_codec_cfg_t *c){
    assert(c->reset_pin == -1);
    assert(c->hw_gain.pa_gain == 15.0f);
    assert(c->hw_gain.pa_voltage == 0 && c->hw_gain.codec_dac_voltage == 0);
    amp_created++;
    return (void *)1;
}
static void *esp_codec_dev_new(const esp_codec_dev_cfg_t *c){return (void *)(intptr_t)c->dev_type;}
static int esp_codec_dev_open(void *d,const esp_codec_dev_sample_info_t *c){return 0;}
static int esp_codec_dev_set_out_vol(void *d,int vol){assert(vol==70);return 0;}
static int esp_codec_dev_set_out_mute(void *d,bool mute){assert(mute);return 0;}
static void *es7210_codec_new(const es7210_codec_cfg_t *c){assert(c->mic_selected==7);return (void *)1;}
static int esp_codec_dev_set_in_channel_gain(void *d,unsigned mask,float gain){
    if(mask==1)mic_gain=(int)gain;
    if(mask==4)reference_gain=(int)gain;
    return 0;
}
static int audio_amplifier_clock_init(void){clock_checked++;return 0;}
''' + function(source, "audio_codecs_init") + r'''
int main(void){
    reference_gain=-1;
    assert(audio_codecs_init()==0);
    assert(amp_created==1 && clock_checked==1);
    assert(mic_gain==30 && reference_gain==0);
}
''')

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
#define XIAOTAI_S3_PROFILE_VERSION "1.0.1"
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
    assert(strstr(body,"\"firmware_version\":\"1.0.1\""));
    assert(strstr(body,"\"up_video_mt\":[\"mjpeg\"]"));
    assert(strstr(body,"\"no_video\":true"));
    return ESP_OK;
}
''' + runtime_config(source) + function(source, "request_device_profile") + r'''
int main(void) {
    request_device_profile();
    assert(s_voip_profile_inflight);
}
''')


if __name__ == "__main__":
    unittest.main()
