"""Replay actual board audio setup with partial hardware acquisition failures."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class BoardAudioRetry(unittest.TestCase):
    def test_partial_hardware_start_rolls_back_and_success_is_idempotent(self):
        for path in ['boards/lckfb/esp32s3/board_adapter.c', 'boards/m5stack/cores3-lite/board_adapter.c', 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c']:
            with self.subTest(path=path):
                source=(ROOT/path).read_text();p4='esp32p4' in path
                fn='p4_audio_start' if p4 else 'szpi_board_audio_init'
                cleanup=function(source,'audio_init_rollback') if 'static void audio_init_rollback(' in source else ''
                pointers=['s_microphone_dev','s_speaker_dev','s_es7210_codec_if','s_es7210_ctrl_if','s_i2s_data_if','s_i2s_rx','s_i2s_tx']
                if 'm5stack' in path:pointers+=['s_amp_codec_if','s_amp_ctrl_if']
                else:pointers+=['s_es8311_codec_if','s_es8311_ctrl_if','s_es8311_gpio_if']
                fixture='''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
static bool s_initialized=true,s_audio_ready,s_capture_probe_ready;
static unsigned live,calls,fail_stage;
static void *alloc(void){live++;return malloc(8);}
static int release(void*p){if(p){assert(live);live--;free(p);}return 0;}
#define esp_codec_dev_close(p) ((void)0)
#define esp_codec_dev_delete(p) release(p)
#define audio_codec_delete_codec_if(p) release((void*)p)
#define audio_codec_delete_ctrl_if(p) release((void*)p)
#define audio_codec_delete_gpio_if(p) release((void*)p)
#define audio_codec_delete_data_if(p) release((void*)p)
#define i2s_channel_disable(p) ((void)0)
#define i2s_del_channel(p) release(p)
'''
                fixture+='static void *'+',*'.join(pointers)+';\n'
                fixture+='static int audio_i2s_init(void){calls++;s_i2s_tx=alloc();if(fail_stage==1)return -1;s_i2s_rx=alloc();return 0;}\n'
                fixture+='static int audio_codecs_init(void){'+''.join(v+'=alloc();' for v in pointers if v not in ['s_i2s_tx','s_i2s_rx'])+'return fail_stage==2?-1:0;}\n'
                fixture+='static int audio_i2s_start(void){return 0;}\n'
                call=fn+'(NULL)' if p4 else fn+'()'
                run_c(fixture+cleanup+function(source,fn)+f'''
int main(void){{
 for(fail_stage=1;fail_stage<=2;fail_stage++){{
  assert({call}!=0);assert(live==0 && !s_audio_ready);
 }}
 fail_stage=0;assert({call}==0);unsigned before=live,count=calls;
 assert({call}==0 && live==before && calls==count);
}}
''', cflags=['-Wno-unused-variable'])
if __name__=='__main__':unittest.main()
