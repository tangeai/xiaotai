#!/usr/bin/env python3
"""Exercise deferred room cleanup across failed HTTP and stale callbacks."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[5]
source = (repo / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start)+2]

# An abnormal termination must preserve the exact room before local teardown.
finish = function("static void finish_call_session(")
assert "schedule_call_cleanup(s_call_room_id)" in finish
assert finish.index("schedule_call_cleanup") < finish.index("finish_session(error)")
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define PLATFORM_SERVICE_CALL 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct { int valueint; } cJSON;
static cJSON json;
static cJSON *cJSON_Parse(const char *body) { if(!body) return NULL; json.valueint=atoi(body); return &json; }
static cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *root,const char *key) { (void)key; return (cJSON *)root; }
static bool cJSON_IsNumber(const cJSON *item) { return item!=NULL; }
static void cJSON_Delete(cJSON *root) { (void)root; }
static char s_call_cleanup_room[129];
static int64_t s_call_cleanup_due_ms;
static unsigned s_call_cleanup_sequence;
static atomic_uint s_call_cleanup_result;
static bool s_call_wechat;
static char s_call_room_id[129];
static void finish_session(int error) { (void)error; s_call_room_id[0]=0; }
static void product_set_call_result(const char *result) { (void)result; }
static int64_t clock_ms;
static int64_t now_ms(void) {return clock_ms;}
static bool platform_client_ready(void) {return true;}
static int requests, submit_result;
static void (*callback)(const char *,void *);
static void *tag;
static char sent[256];
static int platform_client_request_timeout(int service,const char *path,const char *body,
 unsigned timeout,void (*cb)(const char *,void *),void *ctx) {
 (void)service;(void)timeout;assert(strcmp(path,"/v1/call/hangup")==0);
 ++requests;snprintf(sent,sizeof(sent),"%s",body);callback=cb;tag=ctx;return submit_result;
}
'''.replace('#include <stdio.h>', '#include <stdio.h>\n#include <stdlib.h>')
code += function("static void call_cleanup_response(")
code += function("static void schedule_call_cleanup(")
code += function("static void service_call_cleanup(")
code += finish
code += r'''
int main(void) {
 strcpy(s_call_room_id,"disconnected-room");finish_call_session(-40007,"network");
 assert(!s_call_room_id[0]);assert(strcmp(s_call_cleanup_room,"disconnected-room")==0);
 service_call_cleanup();callback("200",tag);service_call_cleanup();requests=0;
 s_call_wechat=true;strcpy(s_call_room_id,"wechat-room");finish_call_session(-1,"network");
 assert(!s_call_cleanup_room[0]);s_call_wechat=false;
 schedule_call_cleanup("");service_call_cleanup();assert(requests==0);
 schedule_call_cleanup("old-room");service_call_cleanup();assert(requests==1);
 assert(strstr(sent,"old-room"));void *old_tag=tag;
 callback(NULL,tag);service_call_cleanup();assert(s_call_cleanup_room[0]);
 clock_ms+=5000;service_call_cleanup();assert(requests==2);
 callback("200",old_tag);service_call_cleanup();assert(s_call_cleanup_room[0]);
 callback("200",tag);service_call_cleanup();assert(!s_call_cleanup_room[0]);
 schedule_call_cleanup("next-room");submit_result=-1;service_call_cleanup();
 assert(s_call_cleanup_room[0]);submit_result=0;clock_ms+=5000;service_call_cleanup();
 old_tag=tag;clock_ms+=16000;service_call_cleanup();assert(tag!=old_tag);
 callback("200",old_tag);service_call_cleanup();assert(s_call_cleanup_room[0]);
 callback("40400",tag);service_call_cleanup();assert(!s_call_cleanup_room[0]);
}
'''
with tempfile.TemporaryDirectory(prefix="call-cleanup-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path/"test.c"), "-o", str(path/"test")], check=True)
    subprocess.run([str(path/"test")], check=True)
print("PASS: room cleanup retries failures/timeouts and ignores stale acknowledgements")
