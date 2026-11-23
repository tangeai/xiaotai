"""Replay public call actions and video conversion failures at the logging boundary."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c


class BusinessLogging(unittest.TestCase):
    def test_call_actions_report_rejected_submissions_only(self):
        source = (ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
        actions = ["starter_runtime_call_accept", "starter_runtime_call_reject", "starter_runtime_call_hangup"]
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef enum {EVENT_CALL_ACCEPT, EVENT_CALL_REJECT, EVENT_CALL_HANGUP} runtime_event_type_t;
typedef struct {runtime_event_type_t type;} runtime_event_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT -1
#define esp_err_to_name(rc) "timeout"
static int accept, warnings;
static char message[128];
#define ESP_LOGW(tag,...) do {warnings++;snprintf(message,sizeof(message),__VA_ARGS__);} while(0)
static int queue_event(const runtime_event_t *event){return accept;}
''' + function(source, "enqueue_simple") + ''.join(function(source, name) for name in actions) + '''
int main(void) {
''' + ''.join(f"""
    accept=1;warnings=0;assert({name}()==ESP_OK && warnings==0);
    accept=0;assert({name}()==ESP_ERR_TIMEOUT && warnings==1);
    assert(strstr(message,"{name}") && strstr(message,"timeout"));
""" for name in actions) + '}')

    def test_h264_conversion_failures_are_counted_and_rate_limited(self):
        source = (ROOT / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
        task = function(source, 'call_video_converter_task')
        failure = task[task.index('if (convert_ret != ESP_OK && generation_matches)'):]
        failure = failure[:failure.index('call_video_return_decoded_slot(index);')]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#define ESP_OK 0
#define CALL_VIDEO_STALL_LOG_INTERVAL_US 5000000LL
#define taskENTER_CRITICAL(lock) ((void)0)
#define taskEXIT_CRITICAL(lock) ((void)0)
#include <stdio.h>
#define esp_err_to_name(rc) "error"
static char warning[128];
#define ESP_LOGW(tag,...) do {warnings++;snprintf(warning,sizeof(warning),__VA_ARGS__);} while(0)
static struct {uint32_t conversion_failures;} s_renderer;
static int warnings;
int main(void) {
    int64_t next_conversion_warning_us = 0;
    for (int i=0;i<1100;i++) {
        int64_t convert_finished_us = i * 10000LL;
        int convert_ret = -1, generation_matches = i < 1050;
        (void)next_conversion_warning_us;(void)convert_finished_us;
''' + failure + '''
    }
    assert(warnings==3 && s_renderer.conversion_failures==1050);
}
''')


if __name__ == "__main__":
    unittest.main()
