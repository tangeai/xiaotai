"""Replay room join callbacks and disconnects against the ESP SDK boundary."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class RoomLeaseLifecycle(unittest.TestCase):
    def test_rejoining_starts_a_fresh_lease_before_presence_returns(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        join = function(source, 'handle_room_command')
        join = join[:join.index('    const cJSON *method =')] + '    cJSON_Delete(root);\n}'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { int valueint; char *valuestring; } cJSON;
typedef struct { uint32_t command, generation; const char *text; size_t length; } runtime_event_t;
typedef int starter_runtime_state_t;
#define ROOM_COMMAND 0x2200U
#define ESP_OK 0
#define ESP_ERR_INVALID_RESPONSE -1
#define STARTER_TIRTC_ROOM 5
#define STARTER_RUNTIME_ROOM_CONNECTING 8
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define STARTER_ROOM_ASSIGNED 1
#define STARTER_ROOM_NONE 0
#define STARTER_ROOM_JOINED 2
#define ESP_LOGW(...) ((void)0)
static uint32_t s_connection_generation=1;
static int64_t clock_ms, s_room_lease_deadline_ms, s_room_next_heartbeat_ms;
static int s_room_lease_seconds=45, s_room_heartbeat_seconds=15, s_session;
static bool s_room_joined,s_room_ptt,s_room_desired=true,s_room_resume_pending;
static char s_room_session_id[65]="session";
static int current_state=STARTER_RUNTIME_ROOM_CONNECTING, presence_requests, starts;
static bool valid=true;
static cJSON object, number={1,NULL}, rate={8000,NULL}, codec={0,"g711a"};
static cJSON *cJSON_ParseWithLength(const char *text,size_t len) { return &object; }
static bool cJSON_IsObject(const cJSON *p) { return p==&object; }
static bool cJSON_IsNumber(const cJSON *p) { return p==&number || p==&rate; }
static bool cJSON_IsString(const cJSON *p) { return p==&codec; }
static cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *p,const char *key) {
    return strcmp(key,"id")==0 ? &number : &object;
}
static cJSON *cJSON_GetObjectItem(const cJSON *p,const char *key) {
    return strcmp(key,"codec")==0 ? &codec : strcmp(key,"sample_rate")==0 ? &rate : &number;
}
static void cJSON_Delete(cJSON *p) {}
static bool copy_json_string(const cJSON *p,const char *key,char *out,size_t n,bool required) {
    snprintf(out,n,"session"); return valid;
}
static int64_t now_ms(void) { return clock_ms; }
static int session_generation(void) { return s_connection_generation; }
static int session_state(void) { return current_state; }
static bool starter_tirtc_connected(void) { return true; }
static int starter_tirtc_send_command(unsigned cmd,const void *data,size_t len) { return 0; }
static void starter_media_set_uplink_enabled(bool enabled) {}
static int starter_media_start(int mode,unsigned generation) { starts++; return 0; }
static void publish_state(void) {}
static void product_set_room(int phase,const char *text) {}
static int xiaotai_runtime_media_started(int *session,int generation) {
    current_state=STARTER_RUNTIME_ROOM_ACTIVE; return 0;
}
static void finish_session(int error) { current_state=0; }
static void room_request_presence(const char *state) {
    /* HTTP completion can be delayed or not admitted: join must set timers. */
    if(strcmp(state,"joined")==0) {
        assert(s_room_joined && s_room_lease_deadline_ms==clock_ms+s_room_lease_seconds*1000);
        assert(s_room_next_heartbeat_ms==clock_ms+s_room_heartbeat_seconds*1000);
        presence_requests++;
    }
}
''' + function(source, 'room_stop_connection') + join + r'''
int main(void) {
    runtime_event_t e={ROOM_COMMAND,1,"join response",13};
    for(int cycle=0;cycle<5;cycle++) {
        clock_ms=100000+cycle*60000;
        current_state=STARTER_RUNTIME_ROOM_CONNECTING;
        s_room_session_id[0]='s';
        handle_room_command(&e);
        assert(s_room_joined && s_room_lease_deadline_ms>clock_ms);
        int64_t deadline=s_room_lease_deadline_ms;
        clock_ms+=1000; handle_room_command(&e);
        assert(s_room_lease_deadline_ms==deadline); /* duplicate join isn't renewal */
        room_stop_connection("left",0);
        assert(!s_room_joined && s_room_lease_deadline_ms==0 && s_room_next_heartbeat_ms==0);
        s_connection_generation++; e.generation=s_connection_generation;
    }
    assert(starts==5 && presence_requests==5);
    current_state=STARTER_RUNTIME_ROOM_CONNECTING;
    e.generation--; handle_room_command(&e); assert(starts==5 && !s_room_joined);
    e.generation++; valid=false; handle_room_command(&e);
    assert(starts==5 && !s_room_joined && s_room_lease_deadline_ms==0);
}
''')


if __name__ == '__main__':
    unittest.main()
