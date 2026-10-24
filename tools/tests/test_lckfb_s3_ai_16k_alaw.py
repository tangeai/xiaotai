"""LCKFB S3 AI trial: 16 kHz PCM goes straight to A-law upstream."""
import unittest

from test_s3_product_regressions import MEDIA, ROOT, function, run_c
import test_s3_voip_audio as routing
import test_s3_opus_media as media_test


RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
DEFAULTS = ROOT / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/sdkconfig.defaults"


class LckfbS3Ai16kAlaw(unittest.TestCase):
    def test_board_selects_16k_ai_profile(self):
        self.assertIn("CONFIG_STARTER_AI_UPLINK_G711A_16K=y", DEFAULTS.read_text())
        m5_defaults = ROOT / "firmware/esp-idf/esp32s3/m5stack-cores3-lite/sdkconfig.defaults"
        self.assertIn("CONFIG_STARTER_AI_UPLINK_G711A_16K=y", m5_defaults.read_text())

    def test_ai_session_requests_16k_alaw_and_accepts_matching_response(self):
        source = RUNTIME.read_text()
        code=r'''
#define CONFIG_STARTER_AI_UPLINK_G711A 1
#define CONFIG_STARTER_AI_UPLINK_G711A_16K 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cjson/cJSON.h>
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_NO_MEM -2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define AI_COMMAND 0x2000
#define AI_RESPONSE_TIMEOUT_MS 10000
static int64_t s_ai_start_at_ms;
static uint32_t s_connection_generation=1;
static char s_ai_request_id[32],s_device_id[]="test-device",s_ai_role_id[]="test-role";
static char request[1024];
static bool starter_tirtc_connected(void){return true;}
static void finish_session(int error){assert(0);}
static unsigned esp_random(void){return 42;}
static void arm_session_timeout(int ms){assert(ms==10000);}
static int starter_tirtc_send_command(unsigned cmd,const void *data,unsigned len){
 assert(cmd==AI_COMMAND && len<sizeof request);memcpy(request,data,len);request[len]=0;return 0;
}
''' + function(source, "send_ai_start") + function(source, "ai_audio_profile_valid") + r'''
int main(void){
 send_ai_start();cJSON *root=cJSON_Parse(request);assert(root);
 cJSON *params=cJSON_GetObjectItemCaseSensitive(root,"params");
 cJSON *input=cJSON_GetObjectItemCaseSensitive(params,"input_audio");
 cJSON *output=cJSON_GetObjectItemCaseSensitive(params,"output_audio");
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(input,"codec")->valuestring,"alaw"));
 assert(cJSON_GetObjectItemCaseSensitive(input,"sample_rate")->valueint==16000);
 assert(ai_audio_profile_valid(input,true) && ai_audio_profile_valid(output,false));
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"sample_rate"),8000);
 assert(!ai_audio_profile_valid(input,true));
 cJSON_Delete(root);
}
'''
        import subprocess
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory(prefix="s3-ai16-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(path / "test.c"),
                            "-lcjson", "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)

    def test_ai_transport_sends_320_byte_16k_alaw_without_changing_calls(self):
        source = routing.TIRTC.read_text()
        harness = routing.S3VoipAudio().code().replace(
            "#define TIRTC_AUDIOSAMPLE_8K16B1C 0",
            "#define TIRTC_AUDIOSAMPLE_8K16B1C 0\n#define TIRTC_AUDIOSAMPLE_16K16B1C 1",
        ).replace(
            "f->length==160);",
            "f->length==(current_mode==STARTER_TIRTC_AI?320:160) && "
            "f->flags==(current_mode==STARTER_TIRTC_AI?1:0));",
        )
        run_c("#define CONFIG_STARTER_AI_UPLINK_G711A 1\n"
              "#define CONFIG_STARTER_AI_UPLINK_G711A_16K 1\n" + harness +
              function(source, "starter_tirtc_send_alaw") + r'''
int main(void){
 current_mode=STARTER_TIRTC_AI;expected=1;
 assert(starter_tirtc_send_alaw(20,"x",320)==0);
 current_mode=STARTER_TIRTC_H5;expected=10;
 assert(starter_tirtc_send_alaw(20,"x",160)==0);
}
''')

    def test_uplink_worker_encodes_16k_ai_and_8k_h5(self):
        media_test.S3OpusMedia().check_uplink(g711=True, alaw16=True)

    def test_ai_has_separate_16k_encoder_and_preroll_joins_live_pcm(self):
        source = MEDIA.read_text()
        run_c(r'''
#define CONFIG_STARTER_AI_UPLINK_G711A 1
#define CONFIG_STARTER_AI_UPLINK_G711A_16K 1
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_AUDIO_ERR_OK 0
typedef int esp_err_t;
#define ESP_AUDIO_SAMPLE_RATE_8K 8000
#define ESP_AUDIO_SAMPLE_RATE_16K 16000
#define ESP_AUDIO_MONO 1
#define ESP_AUDIO_BIT16 16
#define AUDIO_PACKET_MS 20
typedef struct {int sample_rate,channel,bits_per_sample,frame_duration;} esp_g711_enc_config_t;
#define ESP_G711_ENC_CONFIG_DEFAULT() {0}
static void *s_g711_encoder,*s_ai_g711_encoder,*s_g711_decoder;
static int opens;
static int esp_g711a_enc_open(void *cfg,size_t size,void **handle){
 esp_g711_enc_config_t *c=cfg;assert(size==sizeof(*c));
 assert(c->sample_rate==(opens?16000:8000));assert(c->channel==1);
 assert(c->bits_per_sample==16 && c->frame_duration==20);
 *handle=(void *)(uintptr_t)(++opens);return 0;
}
static int esp_g711_dec_open(void *cfg,size_t size,void **handle){*handle=(void *)3;return 0;}
static void esp_g711_enc_close(void *handle){}
''' + function(source, "g711_init") + function(source, "prepare_ai_preroll_pcm") + r'''
int main(void){
 assert(g711_init()==0 && opens==2 && s_g711_encoder && s_ai_g711_encoder);
 int16_t history[320],joined[320];
 for(int i=0;i<320;i++)history[i]=(int16_t)i;
 assert(prepare_ai_preroll_pcm(history,320,joined)==320);
 assert(joined[0]==0 && joined[1]==1 && joined[318]==318 && joined[319]==319);
}
''')

    def test_preroll_preserves_16k_timestamps_and_samples(self):
        source = ROOT / "platforms/esp-idf/components/starter_media_common/src/starter_preroll.c"
        header = ROOT / "platforms/esp-idf/components/starter_media_common/include"
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "starter_preroll.h"
int main(void){
 int16_t *storage=calloc(16000,sizeof(*storage));assert(storage);
 starter_preroll_t q;starter_preroll_init_rate(&q,storage,16000,16000);
 int16_t frame[320],out[320];for(int i=0;i<320;i++)frame[i]=(int16_t)i;
 starter_preroll_append(&q,frame,320,1020);
 assert(q.count==320);
 uint32_t token=starter_preroll_prepare(&q,1000,1020);assert(token);
 assert(q.count==320 && q.timestamp_ms==1000);
 assert(starter_preroll_bind(&q,token,7,1020));
 uint32_t ts=0;assert(starter_preroll_peek(&q,7,out,320,&ts));
 assert(ts==1000 && out[0]==0 && out[319]==319);
 starter_preroll_consume(&q,320);assert(q.timestamp_ms==1020);
 free(storage);
}
'''
        import subprocess
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory(prefix="s3-preroll16-") as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-I", str(header),
                            str(path / "test.c"), str(source), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
