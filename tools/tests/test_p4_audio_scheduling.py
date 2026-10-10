"""Execute production P4 worker creation against the SDIO scheduling contract."""
import re
import unittest
from test_s3_product_regressions import ROOT, run_c

PROJECT = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'


class P4AudioScheduling(unittest.TestCase):
    def test_audio_workers_preempt_sustained_packet_forwarding(self):
        source = (PROJECT / 'components/starter_media/src/starter_media.c').read_text()
        start = source.index('    if (xTaskCreateWithCaps(audio_sink_task,')
        end = source.index('    atomic_store_explicit(&s_ready', start)
        creation = source[start:end]
        policy = (PROJECT / 'main/platform/app_task_affinity.h').read_text()
        priorities = '\n'.join(re.findall(r'^#define APP_TASK_PRIORITY_AUDIO_.*$', policy, re.M))
        sdio = (PROJECT / 'components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c').read_text()
        forwarding = re.search(r'^#define SDIO_DATA_PATH_TASK_PRIORITY\s+(\d+)U', sdio, re.M).group(1)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define pdPASS 1
#define ESP_ERR_NO_MEM 257
#define OPUS_WORKER_TASK_STACK_BYTES 40960
#define MEDIA_REALTIME_CORE 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
''' + priorities + '\n#define FORWARDING_PRIORITY ' + forwarding + r'''
static unsigned created, fail_at, playback, uplink, capture, camera;
static void audio_sink_task(void *p) {}
static void audio_uplink_task(void *p) {}
static void audio_capture_task(void *p) {}
static void camera_task(void *p) {}
static int create(void (*entry)(void *), const char *name, unsigned stack,
                  void *arg, unsigned priority, void *handle, int core, unsigned caps) {
    ++created;
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (entry == audio_sink_task) {assert(strcmp(name,"board_audio_rx")==0);playback=priority;}
    else if (entry == audio_uplink_task) {assert(strcmp(name,"rtc_audio_tx")==0);uplink=priority;}
    else if (entry == audio_capture_task) {assert(core==MEDIA_REALTIME_CORE);capture=priority;}
    else {assert(entry==camera_task);camera=priority;}
    return created == fail_at ? 0 : pdPASS;
}
#define xTaskCreateWithCaps(a,b,c,d,e,f,g) create(a,b,c,d,e,f,-1,g)
#define xTaskCreatePinnedToCoreWithCaps(a,b,c,d,e,f,g,h) create(a,b,c,d,e,f,g,h)
static int start_workers(void) {
''' + creation + r'''
    return 0;
}
int main(void) {
    assert(start_workers()==0 && created==4);
    /* Continuous video forwarding must not outrank ready audio workers. */
    assert(playback > FORWARDING_PRIORITY);
    assert(uplink > FORWARDING_PRIORITY);
    assert(capture > FORWARDING_PRIORITY);
    assert(capture >= playback && playback > camera && uplink > camera);
    /* Interrupt-facing RX/batch handoff retain priority 23/22. */
    assert(capture < 22 && playback < 22 && uplink < 22);
    for (unsigned failure=1;failure<=4;++failure) {
        created=0;fail_at=failure;
        assert(start_workers()==ESP_ERR_NO_MEM && created==failure);
    }
}
''')


if __name__ == '__main__':
    unittest.main()
