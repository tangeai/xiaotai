#!/usr/bin/env python3
"""Replay pending-call media selection and explicit-answer activation.

Mini-program incoming notifications may omit the outbound-only wx_room_type.
The harness replaces RTOS locking/media hardware, not runtime selection logic.
Notification may update the pending-call UI, but only explicit acceptance may
switch the hardware media mode.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
start = source.index("    s_call_wechat = wechat;", source.index("static void handle_platform_signal("))
selection = source[start:source.index("    (void)snprintf(s_call_room_id", start)]
start = source.index("static void product_set_call(")
publisher = source[start:source.index("\n}", start) + 2]
start = source.index("static void reset_call_media_state(")
resetter = source[start:source.index("\n}", start) + 2]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static void *s_product_mutex = (void *)1;
static int xSemaphoreTake(void *m, int ms) { (void)m; (void)ms; return 1; }
static void xSemaphoreGive(void *m) { (void)m; }
static bool s_call_wechat, s_call_outgoing, s_call_video, s_call_camera_enabled;
static bool media_video;
static struct {
    bool call_incoming, call_wechat, call_microphone_muted;
    bool call_video, call_camera_enabled;
    char call_peer[65];
} s_product_snapshot;
void starter_media_set_call_video(bool video) { media_video = video; }
''' + resetter + "\n" + publisher + r'''
static void incoming(bool wechat, const char *room_type) {
    (void)room_type;
''' + selection + r'''
    product_set_call(!s_call_outgoing, s_call_wechat, "test caller", false);
}
static void accept_pending(void) {
    /* Match accept_call(): release the current foreground owner first, then
     * activate the media choice stored by the pending incoming call. */
    reset_call_media_state(true);
    starter_media_set_call_video(s_call_video);
}
static int check(bool wechat, const char *type, bool outgoing, bool chosen, bool expected) {
    s_call_outgoing = outgoing;
    s_call_video = s_call_camera_enabled = media_video = chosen;
    incoming(wechat, type);
    if (s_product_snapshot.call_video != expected || media_video != chosen ||
        s_product_snapshot.call_camera_enabled != expected) {
        fprintf(stderr, "FAIL pending: wx=%d type='%s' outgoing=%d chosen=%d expected_video=%d actual_ui_video=%d media_video=%d\n",
                wechat, type, outgoing, chosen, expected,
                s_product_snapshot.call_video, media_video);
        return 1;
    }
    if (!outgoing) {
        accept_pending();
        if (media_video != expected) {
            fprintf(stderr, "FAIL accept: wx=%d type='%s' chosen=%d expected_video=%d media_video=%d\n",
                    wechat, type, chosen, expected, media_video);
            return 1;
        }
    }
    return 0;
}
int main(void) {
    int failures = 0;
    const char *types[] = {"", "video", "voice", "audio"};
    for (unsigned i = 0; i < 4; ++i) {
        bool video = CONFIG_IDF_TARGET_ESP32P4 && i < 2;
        for (int chosen = 0; chosen <= CONFIG_IDF_TARGET_ESP32P4; ++chosen) {
            /* Incoming calls must replace the previous call's media state. */
            failures += check(true, types[i], false, chosen, video);
            failures += check(false, types[i], false, chosen,
                              CONFIG_IDF_TARGET_ESP32P4 && i == 1);
            failures += check(true, types[i], true, chosen, chosen);
        }
    }
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="incoming-call-media-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(code)
    for p4 in (1, 0):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        f"-DCONFIG_IDF_TARGET_ESP32P4={p4}", str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
print("PASS: P4 pending-call UI and explicit-answer media switch; outbound/S3 baseline")
