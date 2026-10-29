#include "xiaotai_recognition.h"
#include <string.h>
static void clear(xiaotai_recognition_t *r)
{
    r->pre_count = r->head = r->count = r->post_count = 0;
    r->recording = r->ready = false;
    r->last_ms = 0;
}
void xiaotai_recognition_init(xiaotai_recognition_t *r, int16_t *pcm)
{
    *r = (xiaotai_recognition_t){.pcm = pcm};
}
void xiaotai_recognition_release(xiaotai_recognition_t *r, int64_t accepted_ms)
{
    clear(r);
    r->next_ms = accepted_ms + 60000;
}
void xiaotai_recognition_feed(xiaotai_recognition_t *r, const int16_t *pcm,
    size_t count, bool sound, bool allowed, int64_t now_ms)
{
    if (r->ready) return; /* Buffer is immutable until worker releases it. */
    if (!allowed || now_ms < r->next_ms) { clear(r); return; }
    if (!pcm || !count || count > 1600) { clear(r); return; }
    if (r->last_ms && (now_ms <= r->last_ms || now_ms-r->last_ms > 150)) {
        clear(r); return;
    }
    r->last_ms = now_ms;
    if (!r->recording && sound) {
        /* Rotate the bounded pre-roll into chronological order in place. */
        if (r->pre_count == XIAOTAI_RECOGNITION_PRE_SAMPLES && r->head) {
            size_t a=0, b=r->head-1;
            while(a<b) { int16_t v=r->pcm[a]; r->pcm[a++]=r->pcm[b]; r->pcm[b--]=v; }
            a=r->head; b=r->pre_count-1;
            while(a<b) { int16_t v=r->pcm[a]; r->pcm[a++]=r->pcm[b]; r->pcm[b--]=v; }
            a=0; b=r->pre_count-1;
            while(a<b) { int16_t v=r->pcm[a]; r->pcm[a++]=r->pcm[b]; r->pcm[b--]=v; }
        }
        r->count = r->pre_count;
        r->recording = true;
    }
    if (r->recording) {
        size_t n = XIAOTAI_RECOGNITION_POST_SAMPLES-r->post_count;
        if (n>count) n=count;
        memcpy(r->pcm+r->count,pcm,n*sizeof(*pcm));
        r->count+=n; r->post_count+=n;
        if(r->post_count==XIAOTAI_RECOGNITION_POST_SAMPLES) {
            r->ready=true; r->recording=false;
        }
    } else {
        for(size_t i=0;i<count;i++) {
            r->pcm[r->head]=pcm[i];
            r->head=(r->head+1)%XIAOTAI_RECOGNITION_PRE_SAMPLES;
            if(r->pre_count<XIAOTAI_RECOGNITION_PRE_SAMPLES) r->pre_count++;
        }
    }
}

bool xiaotai_recognition_pcm16(const uint8_t *data, size_t bytes,
    int16_t *out, size_t capacity, size_t *samples)
{
    if(!data || !out || !samples || !bytes || bytes>2097152U ||
       bytes%2 || bytes/2>capacity) return false;
    if(bytes>=12 && !memcmp(data,"RIFF",4) && !memcmp(data+8,"WAVE",4)) return false;
    for(size_t i=0;i<bytes/2;i++)
        out[i]=(int16_t)((uint16_t)data[2*i]|((uint16_t)data[2*i+1]<<8));
    *samples=bytes/2;
    return true;
}
