"""Exercise MQTT subscription admission and room closure at SDK boundaries."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_s3_product_regressions import ROOT, function


def replay(code):
    with tempfile.TemporaryDirectory() as tmp:
        source=Path(tmp)/'test.c'; source.write_text(code)
        exe=Path(tmp)/'test'
        subprocess.run(['cc','-std=c11','-Wall','-Werror','-Wno-unused-function',
                        '-Wno-unused-variable','-I/usr/include/cjson',str(source),
                        '-lcjson','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


class EspSignalRecovery(unittest.TestCase):
    def test_subscriptions_require_both_successful_acknowledgements(self):
        source=(ROOT/'platforms/esp-idf/components/platform_client/src/platform_client.c').read_text()
        replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MQTT_EVENT_CONNECTED 1
#define MQTT_EVENT_DISCONNECTED 2
#define MQTT_EVENT_DATA 3
#define MQTT_EVENT_ERROR 4
#define MQTT_EVENT_SUBSCRIBED 5
#define MQTT_ERROR_TYPE_SUBSCRIBE_FAILED 7
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define vTaskDelay(...) ((void)0)
typedef void *esp_event_base_t;
typedef struct { int error_type; } mqtt_error_t;
typedef struct { void *client; int current_data_offset,msg_id,topic_len,data_len,total_data_len;
                 char *topic,*data; mqtt_error_t *error_handle; } mqtt_event_t;
typedef mqtt_event_t *esp_mqtt_event_handle_t;
static char s_device_id[]="test",s_mqtt_message[4096];
static size_t s_mqtt_message_size; static int s_mqtt_message_id;
static bool s_mqtt_connected;
static int s_mqtt_command_subscription=-1,s_mqtt_notify_subscription=-1;
static bool s_mqtt_command_subscribed,s_mqtt_notify_subscribed;
static int online_calls,subscribes,disconnects,fail_index;
static int esp_mqtt_client_subscribe(void*c,const char*t,int qos) {
    ++subscribes; return subscribes==fail_index ? -1 : subscribes;
}
static int esp_mqtt_client_disconnect(void*c) { ++disconnects; return 0; }
static int esp_mqtt_client_publish(void*c,const char*t,const char*p,int l,int q,int r) { return 0; }
static void notify_platform_online(void) { ++online_calls; }
static void dispatch_mqtt_signal(const char*p,size_t l) {}
''' + function(source,'mqtt_event') + r'''
int main(void) {
    mqtt_event_t e={0}; mqtt_error_t error={0}; e.error_handle=&error;
    fail_index=1; mqtt_event(NULL,NULL,MQTT_EVENT_CONNECTED,&e);
    assert(!s_mqtt_connected && online_calls==0 && disconnects==1);
    mqtt_event(NULL,NULL,MQTT_EVENT_DISCONNECTED,&e);
    fail_index=0; mqtt_event(NULL,NULL,MQTT_EVENT_CONNECTED,&e);
    int command=subscribes-1, notify=subscribes;
    assert(!s_mqtt_connected && online_calls==0);
    e.msg_id=command; mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    assert(!s_mqtt_connected && online_calls==0);
    error.error_type=MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
    e.msg_id=notify; mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    assert(!s_mqtt_connected && online_calls==0 && disconnects==2);
    error.error_type=0; mqtt_event(NULL,NULL,MQTT_EVENT_DISCONNECTED,&e);
    mqtt_event(NULL,NULL,MQTT_EVENT_CONNECTED,&e);
    /* An old SUBACK cannot complete this connection's subscriptions. */
    e.msg_id=notify; mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    e.msg_id=subscribes; mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    assert(!s_mqtt_connected);
    e.msg_id=subscribes-1; mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    mqtt_event(NULL,NULL,MQTT_EVENT_SUBSCRIBED,&e);
    assert(s_mqtt_connected && online_calls==1);
    mqtt_event(NULL,NULL,MQTT_EVENT_DISCONNECTED,&e);
    assert(!s_mqtt_connected);
}
''')

    def test_mqtt_closed_invalidates_room_and_inflight_token(self):
        source=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        handler=function(source,'handle_platform_signal')
        handler=handler[:handler.index('    const cJSON *payload =')]+ '    cJSON_Delete(root);\n}'
        replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define vTaskDelay(...) ((void)0)
#define ESP_OK 0
#define STARTER_ROOM_NONE 0
static int stops;
static bool s_room_desired=true,s_room_resume_pending=true;
static char s_room_id[65]="room-1",s_room_code[7]="123456";
static int64_t s_room_assignment_version=20,s_room_sync_due_ms;
static uint32_t s_room_page_epoch=3;
typedef struct { const char *text; size_t length; } runtime_event_t;
static void finish_session(int e) {}
static int runtime_config_clear_tirtc(void) {return 0;}
static void esp_restart(void) {}
static void refresh_contacts(void) {}
static int64_t now_ms(void) {return 1000;}
static void room_stop_connection(const char *presence,int error) {stops++;}
static void product_set_room(int phase,const char *text) {}
''' + handler + r'''
static void send(const char *text) {
    runtime_event_t e={text,strlen(text)}; handle_platform_signal(&e);
}
int main(void) {
    send("{\"type\":\"room_closed\",\"room_id\":\"other\",\"assignment_version\":21}");
    send("{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":19}");
    send("{\"type\":\"room_closed\",\"room_id\":\"room-1\"}");
    send("{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":20.5}");
    assert(stops==0 && s_room_desired && s_room_page_epoch==3);
    const char *closed="{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":21}";
    send(closed); send(closed);
    assert(stops==1 && !s_room_desired && !s_room_resume_pending);
    assert(s_room_id[0]==0 && s_room_code[0]==0 && s_room_page_epoch==4);
    assert(s_room_assignment_version==21 && s_room_sync_due_ms==0);
}
''')

if __name__=='__main__': unittest.main()
