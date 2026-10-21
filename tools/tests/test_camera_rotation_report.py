"""Replay selected-board capability and both call HTTP submission boundaries."""
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import ROOT, function

RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"


def runtime_config(source):
    return source[source.index("#ifndef XIAOTAI_BOARD_MODEL"):
                  source.index("#define RUNTIME_QUEUE_DEPTH")]


def selected_defines(board):
    if board != "m5stack":
        return "#define CONFIG_IDF_TARGET_ESP32P4 %d\n" % (board == "p4")
    cmake = (ROOT / "firmware/esp-idf/esp32s3/m5stack-cores3-lite/components/starter_runtime/CMakeLists.txt").read_text()
    definitions = re.findall(r'(XIAOTAI_BOARD_\w+)=("[^"\n]+"|\d+)', cmake)
    return "#define CONFIG_IDF_TARGET_ESP32P4 0\n" + "".join(
        f"#define {name} {value}\n" for name, value in definitions)


FAKES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define PLATFORM_SERVICE_DEVICE 0
#define PLATFORM_SERVICE_CALL 1
#define PLATFORM_SERVICE_VOIP 2
#define VOIP_PROFILE_RETRY_MS 1000
#define CALL_CONNECT_TIMEOUT_MS 30000
#define CALL_HTTP_DEVICE_DIAL 1
#define CALL_HTTP_VOIP_DIAL 2
#define CALL_HTTP_DEVICE_INFO 3
#define XIAOTAI_OWNER_NONE 0
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
typedef int esp_err_t;
typedef int platform_service_t;
typedef int call_http_stage_t;
typedef int xiaotai_session_owner_t;
typedef struct {char id[16],wx_app_id[16],wx_model_id[16];} starter_product_contact_t;
static struct {unsigned contact_count;starter_product_contact_t contacts[1];} s_product_snapshot = {1};
static void *s_product_mutex = (void *)1;
static bool s_voip_profile_inflight, s_call_wechat, s_call_video, s_call_camera_enabled;
static unsigned s_voip_profile_retry_at_ms;
static int s_session;
static atomic_int s_last_error;
static char s_device_id[]="test",s_call_wx_app_id[]="test",s_call_peer_id[]="peer";
static char s_call_wx_model_id[]="test",s_call_peer_name[]="peer",s_call_room_id[]="room";
static bool platform_client_ready(void){return true;}
static bool platform_client_mqtt_connected(void){return true;}
static unsigned now_ms(void){return 1;}
static unsigned session_generation(void){return 1;}
static void voip_profile_response(void){}
static void call_http_response(void){}
static void *call_http_tag(unsigned g,int stage){return NULL;}
static int xSemaphoreTake(void *s,int ticks){return 1;}
static void xSemaphoreGive(void *s){}
static bool begin_call_common(const starter_product_contact_t *c){return true;}
static void starter_media_set_call_video(bool v){}
static void configure_remote_video_presentation(void *p,bool w){}
static void product_set_call(bool i,bool w,const char *name,bool a){}
static void starter_tirtc_expect_call(unsigned g){}
static void finish_call_session(int err,const char *text){}
static bool session_incoming_pending(void){return true;}
static bool preempt_for_call(bool incoming){return true;}
static bool xiaotai_runtime_accept_incoming(int *s,int *o,uint32_t *g){*g=1;return true;}
static void starter_tirtc_accept_h5(bool b){}
static void publish_state(void){}
static void arm_session_timeout(unsigned t){}
static void connect_voip(void){}
static int platform_client_request_timeout(int service,const char *url,
    const char *body,unsigned timeout,void (*callback)(void),void *context){
    printf("%s\t%s\n",url,body);
    return ESP_OK;
}
'''


def requests(board):
    source = RUNTIME.read_text()
    code = selected_defines(board) + runtime_config(source) + FAKES
    code += "\n".join(function(source, n) for n in
                      ("request_device_profile", "dial_contact", "accept_call"))
    code += r'''
int main(void){
    request_device_profile();
    assert(s_voip_profile_inflight);
    request_device_profile(); /* No duplicate while submission is pending. */
    dial_contact(0, true);
    accept_call();
}
'''
    with tempfile.TemporaryDirectory(prefix="camera-report-") as tmp:
        path = Path(tmp)
        (path / "main.c").write_text(code)
        subprocess.run(["cc", "-std=c11", "-Werror", "-Wno-unused-function",
                        str(path / "main.c"), "-o", str(path / "main")], check=True)
        output = subprocess.check_output([str(path / "main")], text=True)
    lines = [line.split("\t", 1) for line in output.splitlines()]
    assert len(lines) == 3, lines
    return {url: json.loads(body) for url, body in lines}


class CameraRotationReportTest(unittest.TestCase):
    def test_cores3_stream_and_capabilities_report_numeric_180(self):
        result = requests("m5stack")
        profile = result["/v1/device/profile"]
        self.assertEqual(profile["hardware"]["board_model"], "m5stack-cores3-lite")
        for scene in ("stream", "call", "voip"):
            self.assertEqual(profile["profiles"][scene].get("camera_rotation"), 180)
        self.assertEqual(profile["profiles"]["stream"]["up_video_mt"], ["mjpeg"])
        self.assertEqual(profile["profiles"]["stream"]["up_video_streamid"], 11)
        self.assertTrue(profile["profiles"]["call"]["no_video"])
        for board in ("m5stack", "lckfb"):
            voip = requests(board)["/v1/device/profile"]["profiles"]["voip"]
            self.assertTrue(voip["no_video"])
            self.assertEqual(voip["up_video_mt"], "none")
            self.assertEqual(voip["down_video_mt"], "none")

    def test_cores3_outgoing_and_incoming_call_metadata_agree(self):
        result = requests("m5stack")
        for url in ("/v1/call/request", "/v1/call/device/info"):
            self.assertEqual(result[url]["camera_rotation"], 180)
        self.assertEqual(result["/v1/call/request"]["call_type"], "audio")

    def test_existing_boards_keep_their_presentation_contract(self):
        for board in ("lckfb", "p4"):
            result = requests(board)
            profiles = result["/v1/device/profile"]["profiles"]
            for scene, angle in (("stream", 270), ("call", 270), ("voip", 180)):
                if board == "p4":
                    self.assertEqual(profiles[scene]["camera_rotation"], angle)
                else:
                    self.assertNotIn("camera_rotation", profiles[scene])
            for url in ("/v1/call/request", "/v1/call/device/info"):
                self.assertEqual(result[url]["camera_rotation"], 270)


if __name__ == "__main__":
    unittest.main()
