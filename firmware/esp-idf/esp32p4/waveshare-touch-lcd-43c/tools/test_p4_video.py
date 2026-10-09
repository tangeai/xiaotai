#!/usr/bin/env python3
"""Exercise actual adapter admission and SDK metadata helpers without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
video = (root / "components/p4_hardware/p4_video.c").read_text()
sdk = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c").read_text()
def function(text, signature):
    start = text.index(signature)
    end = text.index("\n}", start) + 2
    return text[start:end]

body = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef enum {STARTER_TIRTC_H5, STARTER_TIRTC_AI, STARTER_TIRTC_CALL, STARTER_TIRTC_VOIP} starter_tirtc_mode_t;
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
static int s_lock;
static uint32_t s_desired_generation;
static starter_tirtc_mode_t s_desired_mode;
static bool s_desired_video;
static atomic_uint s_live_generation;
static atomic_bool s_camera_enabled = true;
static atomic_bool s_tx_enabled;
static bool s_worker_camera;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_FAIL -2
static atomic_uint s_sent;
static bool camera_running;
static unsigned camera_starts, camera_stops, key_frames;
static void camera_pipeline_request_key_frame(void){}
static void p4_video_capture_offer(const uint8_t *data,size_t len,uint16_t w,uint16_t h,bool key,uint32_t generation){
    (void)data;(void)len;(void)w;(void)h;(void)key;(void)generation;
}
static bool camera_pipeline_is_running(void) { return camera_running; }
static int camera_pipeline_set_rtc_video_enabled(bool enabled) {
    if(enabled) ++camera_starts; else ++camera_stops;
    camera_running=enabled; return ESP_OK;
}
static void camera_pipeline_request_stream_start_key_frame(void) { ++key_frames; }
typedef void *tirtc_conn_t;
static atomic_uintptr_t s_connection;
static bool ready;
static starter_tirtc_mode_t mode;
static atomic_int s_mode;
static atomic_bool s_video_subscribed;
#define CONFIG_IDF_TARGET_ESP32P4 1
static bool connection_matches(tirtc_conn_t conn) {return conn == (void*)1;}
static void on_request_key_frame(tirtc_conn_t conn, uint8_t stream) {
    assert(conn == (void*)1 && stream == (mode == STARTER_TIRTC_VOIP ? 1 : 11));
}
static uint32_t current_generation;
static int64_t clock_us;
static uint32_t s_subscribed_generation;
static int64_t s_last_subscribe_us;
static atomic_bool s_need_idr;
static unsigned subscribe_calls;
static int subscribe_result = 8;
static unsigned video_request_calls;
static int video_request_result = 8;
int TiRtcSendCommand(tirtc_conn_t conn, uint32_t command, const void *data, uint32_t length) {
    assert(conn == (void*)1 && (command & 0xffffU) == 0x1105U);
    assert(length == 1 && *(const uint8_t*)data == 1);
    ++video_request_calls;
    return video_request_result;
}
static uint32_t starter_tirtc_generation(void) { return current_generation; }
static int64_t esp_timer_get_time(void) { return clock_us; }
static starter_tirtc_mode_t starter_tirtc_mode(void) { return mode; }
static bool starter_tirtc_video_ready(void) { return ready; }
#define H5_VIDEO_STREAM 11
#define H5_DOWN_VIDEO_STREAM 15
#define VOIP_VIDEO_STREAM 1
#define TIRTC_VIDEO_H264 2
#define TIRTC_FRAME_FLAG_KEY_FRAME 1
#define TIRTC_E_INVALID_PARAMETER -1
typedef struct {uint8_t stream_id, media, flags; uint32_t ts, length;} TIRTCFRAMEINFO;
static TIRTCFRAMEINFO sent;
static unsigned sdk_video_sends;
static int TiRtcSendVideoStream(tirtc_conn_t c, const TIRTCFRAMEINFO *f, const void *data) {
    assert(c && data); sent = *f; ++sdk_video_sends; return 7;
}
static int TiRtcSubscribeVideo(tirtc_conn_t c, uint8_t stream) {
    assert(c && stream == (mode == STARTER_TIRTC_VOIP ? 1 : 11)); ++subscribe_calls; return subscribe_result;
}
'''
body += function(video, "void p4_video_set_session(")
body += function(video, "esp_err_t p4_video_set_camera_enabled(")
body += function(video, "static void maintain_camera(")
body += function(sdk, "static uint8_t video_stream_for_mode(")
body += function(sdk, "static uint8_t down_video_stream_for_mode(")
body += function(sdk, "int starter_tirtc_send_h264(")
body += function(video, "static esp_err_t send_video(")
body += function(sdk, "int starter_tirtc_subscribe_call_video(")
body += function(sdk, "static int on_subscribe_video(")
body += function(video, "static void maintain_subscription(")
body += r'''
int main(void) {
    p4_video_set_session(STARTER_TIRTC_CALL, 7, true);
    assert(s_desired_generation == 7 && s_live_generation == 0);
    atomic_store(&s_live_generation, 7);
    p4_video_set_session(STARTER_TIRTC_CALL, 7, true);
    assert(s_live_generation == 7); /* duplicate start preserves a live owner */
    p4_video_set_session(STARTER_TIRTC_H5, 0, false);
    assert(s_live_generation == 0); /* immediate stop before worker drain */
    p4_video_set_session(STARTER_TIRTC_VOIP, 8, true);
    assert(s_live_generation == 0 && s_desired_generation == 8);
    atomic_store(&s_live_generation, 8);
    p4_video_set_session(STARTER_TIRTC_VOIP, 8, false);
    assert(s_live_generation == 0); /* video -> audio revokes the camera */
    const char data[] = "frame";
    ready = true;
    assert(starter_tirtc_send_h264(40, data, 5, true) == -1);
    atomic_store(&s_connection, 1);
    assert(starter_tirtc_send_h264(40, data, 0, true) == -1);
    ready = false;
    assert(starter_tirtc_send_h264(40, data, 5, true) == -1);
    ready = true;
    assert(starter_tirtc_send_h264(40, data, 5, true) == 7);
    assert(sent.stream_id == 11 && sent.media == TIRTC_VIDEO_H264 && sent.flags == 1);
    assert(sent.ts == 40 && sent.length == 5);
    assert(starter_tirtc_send_h264(41, data, 5, false) == 7 && sent.flags == 0);
    mode = STARTER_TIRTC_AI;
    assert(starter_tirtc_subscribe_call_video() == -1);
    mode = STARTER_TIRTC_CALL;
    assert(starter_tirtc_subscribe_call_video() == 8);
    mode = STARTER_TIRTC_VOIP;
    assert(starter_tirtc_subscribe_call_video() == 8);
    assert(video_request_calls == 1); /* VoIP also requires remote-video enable. */
    video_request_result = -1;
    assert(starter_tirtc_subscribe_call_video() == -1);
    video_request_result = 8;
    /* Transient subscription failure is retried by the owner, at most once/s. */
    subscribe_calls = 0;
    mode = STARTER_TIRTC_CALL;
    current_generation = 9;
    atomic_store(&s_live_generation, 9);
    clock_us = 100;
    subscribe_result = -1;
    maintain_subscription();
    assert(subscribe_calls == 1 && s_subscribed_generation == 0);
    clock_us = 999999;
    maintain_subscription();
    assert(subscribe_calls == 1);
    clock_us = 1000100;
    subscribe_result = 8;
    maintain_subscription();
    assert(subscribe_calls == 2 && s_subscribed_generation == 9 && s_need_idr);
    clock_us += 2000000;
    maintain_subscription();
    assert(subscribe_calls == 2); /* successful generation is subscribed once */
    atomic_store(&s_live_generation, 10);
    maintain_subscription();
    assert(subscribe_calls == 2); /* stale SDK generation cannot be subscribed */
    current_generation = 10;
    mode = STARTER_TIRTC_VOIP;
    maintain_subscription();
    assert(subscribe_calls == 3 && s_subscribed_generation == 10 && !s_need_idr);
    atomic_store(&s_live_generation, 0);
    maintain_subscription();
    assert(subscribe_calls == 3);
    /* Camera off preserves the live receive session; reopen requests IDR. */
    p4_video_set_session(STARTER_TIRTC_CALL, 20, true);
    atomic_store(&s_live_generation, 20);
    atomic_store(&s_tx_enabled, true);
    s_worker_camera = camera_running = true;
    assert(p4_video_set_camera_enabled(19, false) == ESP_ERR_INVALID_STATE);
    assert(s_camera_enabled);
    current_generation=20;
    assert(send_video((const uint8_t*)data,5,640,480,1000,TIRTC_VIDEO_H264,true,NULL)==ESP_OK);
    unsigned before_off=sdk_video_sends;
    assert(p4_video_set_camera_enabled(20, false) == ESP_OK);
    for(int i=0;i<10;i++) {
        assert(send_video((const uint8_t*)data,5,640,480,2000,TIRTC_VIDEO_H264,false,NULL)==ESP_ERR_INVALID_STATE);
    }
    assert(sdk_video_sends==before_off);
    assert(!s_tx_enabled && s_live_generation == 20);
    maintain_camera();
    assert(!camera_running && camera_stops == 1 && s_live_generation == 20);
    assert(p4_video_set_camera_enabled(20, true) == ESP_OK);
    maintain_camera();
    assert(camera_running && s_tx_enabled && key_frames == 1 && camera_starts == 1);
    assert(send_video((const uint8_t*)data,5,640,480,3000,TIRTC_VIDEO_H264,true,NULL)==ESP_OK);
    assert(sdk_video_sends==before_off+1);
    /* Quick off/on still drains the old worker, rather than leaving TX stuck. */
    p4_video_set_camera_enabled(20, false);
    p4_video_set_camera_enabled(20, true);
    maintain_camera();
    assert(s_tx_enabled && camera_stops == 2 && camera_starts == 2);
    p4_video_set_session(STARTER_TIRTC_H5, 0, false);
    assert(!s_live_generation && !s_tx_enabled);
    assert(p4_video_set_camera_enabled(20, true) == ESP_ERR_INVALID_STATE);
    mode=STARTER_TIRTC_CALL; s_mode=mode;
    assert(on_subscribe_video((void*)1,11)==0);
    mode=STARTER_TIRTC_VOIP; s_mode=mode;
    assert(on_subscribe_video((void*)1,1)==0);
    assert(on_subscribe_video((void*)1,11)==-1);
    mode=STARTER_TIRTC_H5; s_mode=mode;
    assert(on_subscribe_video((void*)1,11)==0);
    assert(on_subscribe_video((void*)2,11)==-1);
    assert(on_subscribe_video((void*)1,10)==-1);
    mode=STARTER_TIRTC_AI; s_mode=mode;
    assert(on_subscribe_video((void*)1,11)==-1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="xiaotai-p4-video-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(body)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
callback = function(video, "void p4_video_submit(")
assert "xQueueSend(s_pending, &slot, 0)" in callback
assert "memcpy(" in callback
assert "call_video_renderer_submit" not in callback
assert "vTaskDelay" not in callback and "TiRtc" not in callback
presentation = function(video, "void p4_video_ui_tick(")
assert "CALL_VIDEO_RENDER_WIDTH * CALL_VIDEO_RENDER_HEIGHT" in presentation
assert ".w = CALL_VIDEO_RENDER_WIDTH" in presentation
assert ".h = CALL_VIDEO_RENDER_HEIGHT" in presentation
assert "lv_obj_center(s_image);" in presentation
assert "480 * 320" not in presentation
assert "memcpy(" not in presentation
assert ".data = (const uint8_t *)pixels" in presentation
assert "s_presented_frame_held = true" in presentation
product = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
product_tick = function(product, "static void product_tick(")
video_tick = function(product, "static void product_video_tick(")
assert "p4_video_ui_tick" not in product_tick
assert "p4_video_ui_tick(lv_scr_act(), s_page == PAGE_CALL);" in video_tick
assert "#define PRODUCT_VIDEO_TICK_MS 30U" in product
assert "lv_timer_create(product_video_tick, PRODUCT_VIDEO_TICK_MS, NULL)" in product
print("PASS: video generation/stop/idempotence, SDK metadata and nonblocking ingress")
