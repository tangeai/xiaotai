/* Run the production ESP worker with deterministic clock, HTTP and audio ports. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "starter_recognition.c"
static jmp_buf finish;
static int scenario, uploads, plays, config_calls, queries;
static bool saw_recording_complete;
static int64_t ticks, upload_at[4], resume_at;
static starter_pcm_observer_t observer;
static bool internal_stack;
int64_t esp_timer_get_time(void) { return ticks*1000; }
uint32_t esp_random(void) { static unsigned v=1;return v++; }
void *heap_caps_malloc(size_t n,unsigned caps) { (void)caps;return malloc(n); }
void heap_caps_free(void *p) { free(p); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait) { (void)s;(void)wait;return 1; }
void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned caps)
{ (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)caps;return 1; }
void vTaskDelete(void *p) { (void)p; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) {return (void *)1;}
void vSemaphoreDelete(SemaphoreHandle_t s) {(void)s;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle)
{(void)name;(void)stack;(void)priority;(void)handle;internal_stack=true;fn(arg);internal_stack=false;return 1;}
void starter_media_set_pcm_observer(starter_pcm_observer_t fn) { observer=fn; }
void starter_media_cancel_pcm8k_playback(void) {}
uint32_t starter_media_playback_epoch(void) { return 0; }
esp_err_t starter_media_play_pcm8k_at_epoch(const int16_t *pcm,size_t count,uint32_t epoch)
{ (void)epoch;assert(!cancelled());assert(count==800);assert(pcm[0]==1234);plays++;
  if(scenario==4) { starter_recognition_allow(false);resume_at=ticks+2000;return -1; }
  return 0; }
bool platform_client_ready(void) { return true; }
bool platform_client_binding_required(void) { return false; }
int nvs_open(const char *name,int mode,nvs_handle_t *h) { (void)name;(void)mode;*h=1;return scenario==6?0:-1; }
int nvs_get_i64(nvs_handle_t h,const char *key,int64_t *v) { (void)h;(void)key;*v=(int64_t)time(NULL);return 0; }
int nvs_set_i64(nvs_handle_t h,const char *key,int64_t v) { (void)h;(void)key;(void)v;assert(internal_stack);return 0; }
int nvs_commit(nvs_handle_t h) { (void)h;return 0; }
void nvs_close(nvs_handle_t h) { (void)h; }
void vTaskDelay(unsigned ms)
{
    ticks+=ms;
    if(scenario==0 && (ticks==5000 || ticks==10000 || ticks==20000)) {
        char status[1728];starter_recognition_snapshot(status,sizeof(status));
        if(ticks==5000) assert(strstr(status,"等待声音"));
        if(ticks==10000) assert(strstr(status,"录制中") && strstr(status,"/10 秒"));
        if(ticks==20000) {
            assert(strstr(status,"声音分析完成") && !strstr(status,"冷却中"));
            assert(starter_recognition_cooldown_remaining_ms()>0);
        }
    }
    if(scenario==14 && ticks==80000) {
        char status[1728];starter_recognition_snapshot(status,sizeof(status));
        assert(strstr(status,"等待声音"));
    }
    if(resume_at && ticks>=resume_at) {resume_at=0;starter_recognition_allow(true);}
    if(ticks>190000) longjmp(finish,1);
    if(scenario==3 && ticks==12000) starter_recognition_allow(false);
    if(scenario==3 && ticks==14000) starter_recognition_allow(true);
    if(scenario==11 && ticks==12000) {
        starter_recognition_set_local_enabled(false);
        char status[1728];starter_recognition_snapshot(status,sizeof(status));
        assert(strstr(status,"已暂停") && !starter_recognition_effective_enabled());
    }
    if(scenario==11 && ticks==14000) starter_recognition_set_local_enabled(true);
    int16_t pcm[320];for(unsigned i=0;i<320;i++) pcm[i]=1234;
    if(observer) for(int i=4;i>=0;i--)
        observer(pcm,320,ticks>=6000 && !(scenario==14 && ticks>=17000),true,ticks-i*20);
    if(scenario==0 && s_recorder.ready && !saw_recording_complete) {
        char status[1728];starter_recognition_snapshot(status,sizeof(status));
        assert(strstr(status,"录音完成，准备上传"));
        saw_recording_complete=true;
    }
}
esp_err_t platform_client_recognition_transfer(const char *path,const char *prefix,
    const void *pcm,size_t pcm_bytes,const char *suffix,void *response,size_t capacity,
    size_t *length,int *status,bool (*stop)(void))
{
    assert(!stop());*status=200;
    const char *json=NULL;
    if(strstr(path,"/config")) {
        config_calls++;
        if(scenario==7) {*status=410;json="{\"code\":6006}";}
        else json=scenario==12?
          "{\"code\":200,\"data\":{\"effective_enabled\":false,\"policy\":{\"enabled\":false,\"allowed\":true,\"revision\":3,\"monthly_limit\":100,\"schemes\":[{}]},\"usage\":{\"used\":0},\"upload_limits\":{\"max_audio_seconds\":15,\"max_file_bytes\":480044}}}":
          "{\"code\":200,\"data\":{\"effective_enabled\":true,\"policy\":{\"enabled\":true,\"allowed\":true,\"revision\":3,\"monthly_limit\":100,\"schemes\":[{}]},\"usage\":{\"used\":0},\"upload_limits\":{\"max_audio_seconds\":15,\"max_file_bytes\":480044}}}";
    } else if(prefix) {
        if(scenario==0) {char status[1728];starter_recognition_snapshot(status,sizeof(status));
            assert(strstr(status,"正在上传声音"));}
        assert(pcm && pcm_bytes>=320000 && pcm_bytes<=480000);
        assert(strstr(prefix,"pcm_s16le") && strstr(prefix,"16000") && strstr(suffix,"--\r\n"));
        assert(uploads<4);upload_at[uploads++]=ticks;
        if(uploads>1) assert(upload_at[uploads-1]-upload_at[uploads-2]>=60000);
        if(scenario==8) {*status=410;json="{\"code\":6006}";}
        else if(scenario==1) {*status=429;json="{}";}
        else if(scenario==13) {*status=400;json="{}";}
        else if(scenario==2) {*length=0;return -1;} /* Accepted but response lost. */
        else json="{\"code\":200}";
    } else if(strstr(path,"/audio/")) {
        if(scenario==10) {*status=410;const char *b="{\"code\":6006}";*length=strlen(b);memcpy(response,b,*length);return 0;}
        unsigned char wav[1644]={0};
        memcpy(wav,"RIFF",4);unsigned riff=1636;memcpy(wav+4,&riff,4);memcpy(wav+8,"WAVEfmt ",8);
        wav[16]=16;wav[20]=1;wav[22]=1;unsigned rate=8000;memcpy(wav+24,&rate,4);
        wav[32]=2;wav[34]=16;memcpy(wav+36,"data",4);unsigned bytes=1600;memcpy(wav+40,&bytes,4);
        for(unsigned i=44;i<sizeof(wav);i+=2) {wav[i]=0xd2;wav[i+1]=4;}
        assert(capacity>=sizeof(wav));memcpy(response,wav,sizeof(wav));*length=sizeof(wav);return 0;
    } else {
        if(scenario==0 && queries==0) {char status[1728];starter_recognition_snapshot(status,sizeof(status));
            assert(strstr(status,"已上传，等待分析"));}
        queries++;
        if(scenario==9) {*status=410;json="{\"code\":6006}";}
        else json="{\"code\":200,\"data\":{\"status_revision\":2,\"status\":\"succeeded\",\"results\":[{\"item_id\":\"item_1\",\"result\":{\"summary\":\"test sound\"},\"speech\":{\"status\":\"ready\"}}]}}";
    }
    if(scenario==5 && !prefix && !strstr(path,"/config") && queries%2) {
        json="{\"code\":200,\"data\":{\"status_revision\":2,\"status\":\"processing\",\"results\":[{\"item_id\":\"item_1\",\"result\":{\"summary\":\"test sound\"},\"speech\":{\"status\":\"ready\"}}]}}";
    }
    *length=strlen(json);assert(*length<capacity);memcpy(response,json,*length);return 0;
}
int main(int argc,char **argv)
{
    assert(!starter_recognition_status_is_active("声音识别未开启或不可用"));
    assert(!starter_recognition_status_is_active("等待声音"));
    assert(starter_recognition_status_is_active("检测到声音，录制中 4/10 秒"));
    assert(starter_recognition_status_is_active("正在上传声音"));
    assert(starter_recognition_status_is_active("已上传，等待分析"));
    assert(!starter_recognition_status_is_active("声音分析完成"));
    assert(starter_recognition_status_visible("声音识别未开启或不可用",5000,4999));
    assert(!starter_recognition_status_visible("声音识别未开启或不可用",5000,5000));
    assert(starter_recognition_status_visible("检测到声音，录制中 4/10 秒",5000,9000));
    assert(!starter_recognition_status_visible("",5000,0));
    assert(argc==2);scenario=atoi(argv[1]);starter_recognition_init();starter_recognition_allow(true);
    assert(starter_recognition_local_enabled());
    int64_t original_cooldown=s_recorder.next_ms;
    assert(starter_recognition_cooldown_remaining_ms()==0); /* server not yet ready */
    atomic_store(&s_effective_enabled,true);
    s_recorder.next_ms=clock_ms()+30000;
    assert(starter_recognition_cooldown_remaining_ms()==30000);
    s_recorder.next_ms=clock_ms()+70000;
    assert(starter_recognition_cooldown_remaining_ms()==60000); /* clamped */
    s_recorder.next_ms=original_cooldown;
    atomic_store(&s_effective_enabled,false);
    s_recorder.recording=true;
    s_recorder.post_count=16000;
    s_recorder.count=16000;
    starter_recognition_set_local_enabled(false);
    assert(!starter_recognition_local_enabled() && cancelled());
    assert(!s_recorder.recording && s_recorder.post_count==0 && s_recorder.count==0);
    char disabled[1728];starter_recognition_snapshot(disabled,sizeof(disabled));
    assert(strstr(disabled,"已暂停"));
    starter_recognition_set_local_enabled(true);
    assert(starter_recognition_local_enabled());
    if(!setjmp(finish)) worker(NULL);
    assert(config_calls>=1);
    if(scenario==12) {assert(!starter_recognition_effective_enabled() && uploads==0);return 0;}
    if(scenario==13) {char status[1728];starter_recognition_snapshot(status,sizeof(status));
        assert(uploads>=1 && plays==0 && queries==0 &&
            strstr(status,"声音上传未受理") && !strstr(status,"冷却中") &&
            starter_recognition_cooldown_remaining_ms()>0);return 0;}
    if(scenario==0) assert(saw_recording_complete);
    if(scenario==7) {assert(uploads==0 && plays==0 && starter_recognition_binding_revoked());return 0;}
    assert(uploads>=1);
    if(scenario==8) {assert(uploads==1 && plays==0 && queries==0 && starter_recognition_binding_revoked());return 0;}
    if(scenario==9 || scenario==10) {assert(uploads==1 && plays==0 && starter_recognition_binding_revoked());return 0;}
    if(scenario==1) {assert(uploads==1 && plays==0);}
    else {assert(plays==uploads && queries>=uploads);}
    if(scenario!=1) { char snapshot[1728];starter_recognition_snapshot(snapshot,sizeof(snapshot));assert(strstr(snapshot,"test sound"));assert(strchr(snapshot,'\n')); }
    if(scenario==6) assert(upload_at[0]>=70000);
    if(scenario==3) assert(upload_at[0]>=24000); /* Original recording was cancelled. */
    if(scenario==11) assert(upload_at[0]>=24000); /* Local switch cancelled recording. */
    return 0;
}
