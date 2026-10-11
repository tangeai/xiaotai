#!/usr/bin/env python3
"""Replay the SDK thread-create call through the actual P4 linker wrapper."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=root/"firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/tirtc_sdk/src/tirtc_task_stack_policy.c"
with tempfile.TemporaryDirectory(prefix="p4-rtc-stack-") as directory:
    p=Path(directory)
    (p/"esp_log.h").write_text('#define ESP_LOGI(...) ((void)0)\n')
    (p/"caller.c").write_text(r'''#include <assert.h>
#include <stddef.h>
typedef void (*routine_t)(void *);
int freertos_ThreadCreateWithStackSize(void *,routine_t,void *,int,const char *);
extern int requested;
static void routine(void *a){(void)a;}
int main(void){
 assert(freertos_ThreadCreateWithStackSize((void *)1,routine,(void *)2,8192,"rtc_thread")==-37);
 assert(requested==24576);
 freertos_ThreadCreateWithStackSize((void *)1,routine,(void *)2,32768,"rtc_thread");assert(requested==32768);
 freertos_ThreadCreateWithStackSize((void *)1,routine,(void *)2,4096,"other");assert(requested==4096);
 freertos_ThreadCreateWithStackSize((void *)1,routine,(void *)2,4096,NULL);assert(requested==4096);
 return 0;
}
''')
    (p/"sdk.c").write_text(r'''#include <assert.h>
#include <stddef.h>
typedef void (*routine_t)(void *);
int requested;
int freertos_ThreadCreateWithStackSize(void *h,routine_t f,void *a,int s,const char *n){
 assert(h==(void *)1 && f!=NULL && a==(void *)2);(void)n;requested=s;return -37;
}
''')
    subprocess.run(["cc","-Wall","-Wextra","-Werror","-I",str(p),str(p/"caller.c"),str(p/"sdk.c"),str(source),"-Wl,--wrap=freertos_ThreadCreateWithStackSize","-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
print("PASS: actual P4 SDK thread-create boundary raises rtc_thread 8 KiB to 24 KiB, preserves larger stacks/other threads/return code")
