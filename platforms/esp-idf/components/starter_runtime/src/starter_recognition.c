#include "starter_recognition.h"
#include "xiaotai_recognition.h"
#include "starter_media.h"
#include "platform_client.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define JSON_BYTES 32768U
#define WAV_BYTES (2U*1024U*1024U)
static SemaphoreHandle_t s_lock;
static atomic_bool s_allowed, s_refresh, s_capture, s_shutdown, s_revoked;
static atomic_bool s_local_enabled = true, s_effective_enabled;
static atomic_bool s_resume_pending;
static atomic_bool s_request_active;
static atomic_uint s_admission_epoch;
static unsigned s_io_epoch;
static xiaotai_recognition_t s_recorder;
static char s_text[128];
static char s_summary[1536];
static int64_t s_guard_ms;
static bool s_policy_open, s_config_enabled;
static int64_t clock_ms(void) { return esp_timer_get_time()/1000; }
typedef struct {
    int64_t when;
    SemaphoreHandle_t done;
} cooldown_write_t;
static void cooldown_write_task(void *argument)
{
    /* Copy before NVS can disable the PSRAM cache. Ordinary xTaskCreate uses
     * internal stack/TCB; never commit flash on the network worker's PSRAM stack. */
    cooldown_write_t *request=argument;
    int64_t when=request->when;
    SemaphoreHandle_t done=request->done;
    nvs_handle_t handle;
    if(nvs_open("recognition",NVS_READWRITE,&handle)==ESP_OK) {
        (void)nvs_set_i64(handle,"last_upload",when);
        (void)nvs_commit(handle); nvs_close(handle);
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}
static void save_cooldown(void)
{
    cooldown_write_t request={.when=(int64_t)time(NULL),.done=xSemaphoreCreateBinary()};
    if(!request.done) return;
    if(xTaskCreate(cooldown_write_task,"sound_cooldown",6144,&request,2,NULL)==pdPASS)
        xSemaphoreTake(request.done,portMAX_DELAY);
    vSemaphoreDelete(request.done);
}
static void load_cooldown(void)
{
    nvs_handle_t handle;int64_t last=0;
    if(nvs_open("recognition",NVS_READONLY,&handle)!=ESP_OK) return;
    esp_err_t err=nvs_get_i64(handle,"last_upload",&last); nvs_close(handle);
    if(err==ESP_OK) {
        int64_t now=(int64_t)time(NULL), remaining=60;
        if(now>1700000000 && last>1700000000 && now>=last) remaining=last+60-now;
        if(remaining>60) remaining=60;
        if(remaining>0) s_recorder.next_ms=clock_ms()+remaining*1000;
    }
}
static bool cancelled(void)
{
    return atomic_load(&s_revoked) || !atomic_load(&s_local_enabled) || !atomic_load(&s_allowed) || s_io_epoch!=atomic_load(&s_admission_epoch) ||
        !platform_client_ready() || platform_client_binding_required();
}
static void message(const char *text)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    snprintf(s_text,sizeof(s_text),"%s",text);
    xSemaphoreGive(s_lock);
}
void starter_recognition_snapshot(char *text,size_t capacity)
{
    if(!text || !capacity) return;
    text[0]=0;
    if(s_lock && xSemaphoreTake(s_lock,0)==pdTRUE) {
        char live[128];
        const char *status=s_text;
        if(atomic_load(&s_local_enabled) && atomic_load(&s_effective_enabled) &&
           atomic_load(&s_allowed) && !atomic_load(&s_request_active)) {
            if(s_recorder.recording) {
                unsigned seconds=(unsigned)(s_recorder.post_count/16000U);
                snprintf(live,sizeof(live),"检测到声音，录制中 %u/10 秒",seconds);
                status=live;
            } else if(s_recorder.ready) status="录音完成，准备上传";
            else if(s_recorder.next_ms>clock_ms()) {
                /* The icon owns cooldown progress; retain short outcome notices. */
                bool outcome=!strcmp(s_text,"声音分析完成") ||
                    !strcmp(s_text,"声音分析失败或结果待确认") ||
                    !strcmp(s_text,"声音分析等待超时") ||
                    !strcmp(s_text,"声音上传未受理");
                status=outcome?s_text:"";
            } else if(!strcmp(s_text,"声音分析完成") || !strcmp(s_text,"声音分析失败或结果待确认") ||
                    !strcmp(s_text,"声音分析等待超时") || !strcmp(s_text,"声音上传未受理")) status="等待声音";
        }
        snprintf(text,capacity,"%s%s%s",status,s_summary[0]?"\n最近结果：":"",s_summary);
        xSemaphoreGive(s_lock);
    }
}
bool starter_recognition_status_is_active(const char *status)
{
    if(!status) return false;
    return strstr(status,"录制中") || strstr(status,"准备上传") ||
        strstr(status,"正在上传") || strstr(status,"等待分析") ||
        strstr(status,"正在查询") ||
        strstr(status,"正在获取识别配置");
}
bool starter_recognition_status_visible(const char *status,int64_t until_ms,int64_t now_ms)
{
    return status && status[0] &&
        (starter_recognition_status_is_active(status) || now_ms<until_ms);
}
bool starter_recognition_local_enabled(void) { return atomic_load(&s_local_enabled); }
bool starter_recognition_effective_enabled(void)
{
    return atomic_load(&s_local_enabled) && atomic_load(&s_effective_enabled) && !atomic_load(&s_revoked);
}
int64_t starter_recognition_cooldown_remaining_ms(void)
{
    if(!s_lock || !starter_recognition_effective_enabled() || !atomic_load(&s_allowed)) return 0;
    if(xSemaphoreTake(s_lock,0)!=pdTRUE) return -1;
    int64_t remaining=s_recorder.next_ms-clock_ms();
    xSemaphoreGive(s_lock);
    if(remaining<=0) return 0;
    return remaining>60000?60000:remaining;
}
void starter_recognition_set_local_enabled(bool enabled)
{
    if(!s_lock || atomic_exchange(&s_local_enabled,enabled)==enabled) return;
    atomic_fetch_add(&s_admission_epoch,1);
    atomic_store(&s_capture,false);
    if(!enabled) {
        xSemaphoreTake(s_lock,portMAX_DELAY);
        int64_t next=s_recorder.next_ms;
        xiaotai_recognition_release(&s_recorder,0);
        s_recorder.next_ms=next;
        xSemaphoreGive(s_lock);
        starter_media_cancel_pcm8k_playback();
        message("声音识别已暂停");
    } else {
        atomic_store(&s_refresh,true);
        atomic_store(&s_resume_pending,true);
        message("正在获取识别配置");
    }
}
void starter_recognition_allow(bool allowed)
{
    if(!s_lock) return;
    if(atomic_load(&s_shutdown)) allowed=false;
    if(atomic_exchange(&s_allowed,allowed) && !allowed) {
        atomic_fetch_add(&s_admission_epoch,1);
        starter_media_cancel_pcm8k_playback();
    }
}
void starter_recognition_shutdown(void)
{
    atomic_store(&s_shutdown,true);
    atomic_store(&s_effective_enabled,false);
    atomic_store(&s_request_active,false);
    starter_recognition_allow(false);
    if(s_lock) {
        xSemaphoreTake(s_lock,portMAX_DELAY);
        s_text[0]=s_summary[0]=0;
        xSemaphoreGive(s_lock);
    }
}
bool starter_recognition_binding_revoked(void) { return atomic_load(&s_revoked); }
static void mark_revoked(void)
{
    atomic_store(&s_revoked, true);
    atomic_store(&s_effective_enabled,false);
    atomic_store(&s_request_active,false);
    atomic_store(&s_capture, false);
    message("设备已解绑或重置，正在重新绑定");
}
void starter_recognition_refresh(void) { atomic_store(&s_refresh,true); }
static void feed(const int16_t *pcm,size_t samples,bool sound,bool safe,int64_t now)
{
    if(!s_lock || xSemaphoreTake(s_lock,0)!=pdTRUE) return;
    bool allowed=atomic_load(&s_allowed) && atomic_load(&s_capture) && safe && now>=s_guard_ms;
    xiaotai_recognition_feed(&s_recorder,pcm,samples,sound,allowed,now);
    xSemaphoreGive(s_lock);
}
static cJSON *get_json(const char *path,char *buffer,int *status)
{
    size_t length=0;
    if(platform_client_recognition_transfer(path,NULL,NULL,0,NULL,buffer,JSON_BYTES-1,
        &length,status,cancelled)!=ESP_OK) return NULL;
    if(*status==410) {mark_revoked();return NULL;}
    buffer[length]=0;
    cJSON *root=cJSON_Parse(buffer);
    cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
    if(*status!=200 || !cJSON_IsNumber(code) || code->valueint!=200) {
        cJSON_Delete(root);return NULL;
    }
    return root;
}
static const char *str(const cJSON *obj,const char *key)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    return cJSON_IsString(v)?v->valuestring:"";
}
static bool terminal(const char *state)
{
    return !strcmp(state,"succeeded") || !strcmp(state,"partial_failed") ||
        !strcmp(state,"failed") || !strcmp(state,"cancelled") || !strcmp(state,"unknown");
}
static bool valid_id(const char *id)
{
    size_t n=strlen(id); if(!n || n>64) return false;
    for(size_t i=0;i<n;i++) if(!((id[i]>='a'&&id[i]<='z') ||
        (id[i]>='A'&&id[i]<='Z') || (id[i]>='0'&&id[i]<='9') || id[i]=='_' || id[i]=='-')) return false;
    return true;
}
static bool config(char *buffer,unsigned *revision)
{
    int status=0; cJSON *root=get_json("/v1/recognition/config",buffer,&status);
    if(!root) {atomic_store(&s_effective_enabled,false);if(!atomic_load(&s_revoked)) message("声音识别服务暂不可用");return false;}
    cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    cJSON *policy=cJSON_GetObjectItemCaseSensitive(data,"policy");
    cJSON *usage=cJSON_GetObjectItemCaseSensitive(data,"usage");
    cJSON *limits=cJSON_GetObjectItemCaseSensitive(data,"upload_limits");
    cJSON *rev=cJSON_GetObjectItemCaseSensitive(policy,"revision");
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(data,"effective_enabled");
    cJSON *limit=cJSON_GetObjectItemCaseSensitive(policy,"monthly_limit");
    cJSON *used=cJSON_GetObjectItemCaseSensitive(usage,"used");
    int schemes=cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(policy,"schemes"));
    s_policy_open=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(policy,"enabled")) &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(policy,"allowed"));
    bool exhausted=cJSON_IsNumber(limit)&&cJSON_IsNumber(used)&&used->valuedouble+schemes>limit->valuedouble;
    cJSON *seconds=cJSON_GetObjectItemCaseSensitive(limits,"max_audio_seconds");
    cJSON *bytes=cJSON_GetObjectItemCaseSensitive(limits,"max_file_bytes");
    bool fits=cJSON_IsNumber(seconds)&&seconds->valuedouble>=15 &&
        cJSON_IsNumber(bytes)&&bytes->valuedouble>=480044;
    bool ok=cJSON_IsTrue(enabled)&&cJSON_IsNumber(rev)&&rev->valuedouble>=1&&fits&&!exhausted;
    atomic_store(&s_effective_enabled,ok);
    if(ok) {
        *revision=(unsigned)rev->valuedouble;
        if(atomic_load(&s_local_enabled) &&
           (!s_config_enabled || atomic_exchange(&s_resume_pending,false)))
            message("等待声音");
    }
    else if(atomic_load(&s_local_enabled))
        message(exhausted?"本月识别额度已用完":!cJSON_IsTrue(enabled)?"声音识别未开启或不可用":"识别类型需支持15秒音频");
    s_config_enabled=ok;
    cJSON_Delete(root);return ok;
}
static bool play_item(const cJSON *item,const char *request)
{
    cJSON *speech=cJSON_GetObjectItemCaseSensitive(item,"speech");
    if(strcmp(str(speech,"status"),"ready")) return false;
    const char *id=str(item,"item_id");
    if(!valid_id(id)) return false;
    /* Construct a same-service path; never trust arbitrary notification URLs. */
    char path[224];snprintf(path,sizeof(path),"/v1/recognition/requests/%s/audio/%s",request,id);
    uint8_t *wav=heap_caps_malloc(WAV_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    /* Downsampling in place is safe: output never overtakes unread source PCM. */
    int16_t *pcm=(int16_t *)wav;
    size_t length=0,samples=0;int status=0;
    bool ok=wav && pcm && !cancelled() &&
        platform_client_recognition_transfer(path,NULL,NULL,0,NULL,wav,WAV_BYTES,&length,&status,cancelled)==ESP_OK &&
        status==200 && xiaotai_recognition_wav(wav,length,pcm,WAV_BYTES/2,&samples);
    bool attempted=false;
    if(ok && !cancelled()) {
        uint32_t epoch=starter_media_playback_epoch();
        if(!cancelled()) {
            attempted=true;
            ok=starter_media_play_pcm8k_at_epoch(pcm,samples,epoch)==ESP_OK;
        }
    }
    heap_caps_free(wav);
    if(status==410) mark_revoked();
    if(!ok && !cancelled()) message("识别语音下载或播放失败");
    return attempted || !cancelled(); /* Never restart an interrupted playback. */
}
static void worker(void *arg)
{
    (void)arg;
    char *buffer=heap_caps_malloc(JSON_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer) {message("声音识别内存不足");vTaskDelete(NULL);return;}
    unsigned revision=0,highest_revision=0;
    int64_t next_config=0,next_poll=0,accepted=0,expires=0;
    bool enabled=false;
    char request[65]="",played[16][65]={{0}};
    unsigned played_count=0;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_io_epoch=atomic_load(&s_admission_epoch);
        if(platform_client_binding_required()) {
            request[0]=0; enabled=false; atomic_store(&s_capture,false);
            atomic_store(&s_request_active,false);
            xSemaphoreTake(s_lock,portMAX_DELAY);
            xiaotai_recognition_release(&s_recorder,clock_ms());
            xSemaphoreGive(s_lock);
            xSemaphoreTake(s_lock,portMAX_DELAY);
            s_summary[0]=0;s_text[0]=0;
            xSemaphoreGive(s_lock);continue;
        }
        if(cancelled()) {
            atomic_store(&s_capture,false);
            xSemaphoreTake(s_lock,portMAX_DELAY);
            int64_t next=s_recorder.next_ms;
            xiaotai_recognition_release(&s_recorder,0);
            s_recorder.next_ms=next;
            xSemaphoreGive(s_lock);
            continue;
        }
        int64_t now=clock_ms();
        bool refresh=atomic_exchange(&s_refresh,false);
        if(refresh || (!request[0] && now>=next_config)) {
            enabled=config(buffer,&revision);next_config=now+300000;
            if(refresh && !s_policy_open) {
                request[0]=0;
                atomic_store(&s_request_active,false);
            }
        }
        atomic_store(&s_capture,enabled && !request[0]);
        xSemaphoreTake(s_lock,portMAX_DELAY);
        bool ready=s_recorder.ready;
        size_t count=s_recorder.count;
        xSemaphoreGive(s_lock);
        if(ready && !request[0]) {
            atomic_store(&s_capture,false);
            /* Revalidate policy immediately before each submission. */
            enabled=config(buffer,&revision);
            if(enabled && !cancelled()) {
                atomic_store(&s_request_active,true);
                snprintf(request,sizeof(request),"esp_%08lx_%08lx_%08lx",(unsigned long)esp_random(),
                    (unsigned long)esp_random(),(unsigned long)esp_random());
                char prefix[1024];
                snprintf(prefix,sizeof(prefix),
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"request_id\"\r\n\r\n%s\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"config_revision\"\r\n\r\n%u\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"media_type\"\r\n\r\naudio\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"audio_format\"\r\n\r\npcm_s16le\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"sample_rate\"\r\n\r\n16000\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"channels\"\r\n\r\n1\r\n"
                    "--xiaotai-recognition\r\nContent-Disposition: form-data; name=\"files\"; filename=\"audio.pcm\"\r\n"
                    "Content-Type: application/octet-stream\r\n\r\n",request,revision);
                size_t length=0;int status=0;
                message("正在上传声音");
                esp_err_t err=platform_client_recognition_transfer("/v1/recognition/requests",prefix,
                    s_recorder.pcm,count*2,"\r\n--xiaotai-recognition--\r\n",buffer,JSON_BYTES-1,&length,&status,cancelled);
                accepted=clock_ms();save_cooldown();expires=accepted+180000;next_poll=accepted+2000;
                highest_revision=0;played_count=0;
                if(status==410) {mark_revoked();request[0]=0;enabled=false;}
                else if(status==429) {enabled=false;atomic_store(&s_effective_enabled,false);atomic_store(&s_request_active,false);request[0]=0;message("本月识别额度已用完");next_config=accepted+300000;}
                else if(err==ESP_OK && status>=400) {atomic_store(&s_request_active,false);request[0]=0;message("声音上传未受理");next_config=0;}
                else if(err==ESP_OK) message("已上传，等待分析");
                else message("上传状态待确认，正在查询");
                /* Ambiguous transport failure: query this ID, never upload a new ID. */
            } else accepted=clock_ms();
            xSemaphoreTake(s_lock,portMAX_DELAY);
            xiaotai_recognition_release(&s_recorder,accepted);
            xSemaphoreGive(s_lock);
        }
        if(request[0] && now>=next_poll) {
            next_poll=now+3000;
            char path[160];snprintf(path,sizeof(path),"/v1/recognition/requests/%s",request);
            int status=0;cJSON *root=get_json(path,buffer,&status);
            cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
            cJSON *rev=cJSON_GetObjectItemCaseSensitive(data,"status_revision");
            bool done=false;
            if(cJSON_IsNumber(rev) && rev->valueint>0 && (unsigned)rev->valueint>=highest_revision) {
                highest_revision=(unsigned)rev->valueint;
                cJSON *items=cJSON_GetObjectItemCaseSensitive(data,"results");
                char text[1536]="";size_t used=0;
                cJSON *item;
                bool waiting_speech=false;
                cJSON_ArrayForEach(item,items) {
                    cJSON *result=cJSON_GetObjectItemCaseSensitive(item,"result");
                    const char *summary=str(result,"summary");size_t n=strlen(summary);
                    if(n && used+n+2<sizeof(text)) {
                        if(used) text[used++]='\n';
                        memcpy(text+used,summary,n+1);used+=n;
                    }
                    cJSON *speech=cJSON_GetObjectItemCaseSensitive(item,"speech");
                    const char *speech_status=str(speech,"status");
                    if(!strcmp(speech_status,"pending") || !strcmp(speech_status,"synthesizing")) waiting_speech=true;
                }
                if(text[0]) {
                    xSemaphoreTake(s_lock,portMAX_DELAY);
                    snprintf(s_summary,sizeof(s_summary),"%s",text);
                    xSemaphoreGive(s_lock);
                }
                /* Server results are in user order. Earlier pending audio blocks later audio. */
                cJSON_ArrayForEach(item,items) {
                    cJSON *speech=cJSON_GetObjectItemCaseSensitive(item,"speech");
                    const char *st=str(speech,"status");
                    if(!strcmp(st,"pending") || !strcmp(st,"synthesizing")) break;
                    bool seen=false;
                    for(unsigned i=0;i<played_count;i++) if(!strcmp(played[i],str(item,"item_id"))) seen=true;
                    if(!seen && played_count<16 && play_item(item,request)) {
                        snprintf(played[played_count++],65,"%s",str(item,"item_id"));
                    }
                    if(cancelled()) break;
                }
                done=terminal(str(data,"status"))&&!waiting_speech&&!cancelled();
                if(done) message(!strcmp(str(data,"status"),"succeeded")?"声音分析完成":"声音分析失败或结果待确认");
            }
            cJSON_Delete(root);
            if(done || now>expires) {
                if(!done) message("声音分析等待超时");
                request[0]=0;next_config=0;
                atomic_store(&s_request_active,false);
                xSemaphoreTake(s_lock,portMAX_DELAY);
                s_guard_ms=clock_ms()+1000;
                xiaotai_recognition_release(&s_recorder,accepted);
                xSemaphoreGive(s_lock);
            }
        }
    }
}
void starter_recognition_init(void)
{
    if(s_lock) return;
    s_lock=xSemaphoreCreateMutex();
    int16_t *pcm=heap_caps_malloc(XIAOTAI_RECOGNITION_SAMPLES*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_lock || !pcm) {heap_caps_free(pcm);return;}
    xiaotai_recognition_init(&s_recorder,pcm);
    load_cooldown();
    if(xTaskCreateWithCaps(worker,"sound_analysis",12288,NULL,2,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        message("声音识别任务启动失败");return;
    }
    starter_media_set_pcm_observer(feed);
}
