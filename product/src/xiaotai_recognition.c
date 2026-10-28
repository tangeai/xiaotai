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
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static unsigned le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
bool xiaotai_recognition_wav(const uint8_t *w, size_t bytes,
    int16_t *out, size_t capacity, size_t *samples)
{
    if(!w || !out || !samples || bytes<44 || memcmp(w,"RIFF",4) || memcmp(w+8,"WAVE",4)) return false;
    uint32_t riff=le32(w+4);
    bool placeholder=riff==0xffffffffU || riff==2147483583U;
    if(!placeholder && riff!=bytes-8) return false;
    unsigned channels=0, rate=0, align=0;
    for(size_t offset=12;offset+8<=bytes;) {
        uint32_t size=le32(w+offset+4); size_t start=offset+8;
        if(!memcmp(w+offset,"fmt ",4)) {
            if(size<16 || size>bytes-start || le16(w+start)!=1 || le16(w+start+14)!=16) return false;
            channels=le16(w+start+2); rate=le32(w+start+4); align=le16(w+start+12);
            if((channels!=1 && channels!=2) || rate<8000 || rate>48000 || align!=channels*2) return false;
        }
        if(!memcmp(w+offset,"data",4)) {
            if(!align) return false;
            if(placeholder && ((riff==0xffffffffU && size==0xffffffffU) ||
               (riff==2147483583U && size==2147483547U))) size=bytes-start;
            if(size>bytes-start || size%align) return false;
            size_t frames=size/align, n=frames*8000/rate;
            if(!n || n>capacity || frames>(size_t)rate*180) return false;
            /* Average source frames in each output interval; preserve little endian. */
            for(size_t i=0;i<n;i++) {
                size_t a=i*rate/8000,b=(i+1)*rate/8000;
                int32_t total=0;
                for(size_t j=a;j<b;j++) for(unsigned c=0;c<channels;c++)
                    total+=(int16_t)le16(w+start+j*align+c*2);
                out[i]=(int16_t)(total/(int32_t)((b-a)*channels));
            }
            *samples=n; return true;
        }
        if(size>bytes-start) return false;
        offset=start+size+(size&1);
    }
    return false;
}
