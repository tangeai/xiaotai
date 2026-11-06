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
bool xiaotai_recognition_wav_parse(const uint8_t *wav,size_t bytes,xiaotai_recognition_wav_t *out)
{
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!wav || bytes<44 || bytes>2097152U || memcmp(wav,"RIFF",4) || memcmp(wav+8,"WAVE",4)) return false;
    uint32_t riff=le32(wav+4);
    bool placeholder=riff==0xffffffffU || riff==2147483583U;
    if(!placeholder && riff!=bytes-8) return false;
    unsigned rate=0,channels=0,align=0;
    const uint8_t *pcm=NULL;size_t frames=0,offset=12;
    bool fmt=false,stream_size=false;
    while(offset<bytes) {
        if(bytes-offset<8) return false;
        uint32_t size=le32(wav+offset+4);size_t start=offset+8;
        bool data=!memcmp(wav+offset,"data",4);
        if(data && placeholder && ((riff==0xffffffffU && size==0xffffffffU) ||
           (riff==2147483583U && size==2147483547U))) {
            size=(uint32_t)(bytes-start);stream_size=true;
        }
        if(size>bytes-start) return false;
        if(!memcmp(wav+offset,"fmt ",4)) {
            if(fmt || pcm || size<16 || le16(wav+start)!=1 || le16(wav+start+14)!=16) return false;
            channels=le16(wav+start+2);rate=le32(wav+start+4);align=le16(wav+start+12);
            if((channels!=1 && channels!=2) || rate<8000 || rate>48000 ||
               align!=channels*2 || le32(wav+start+8)!=rate*align) return false;
            fmt=true;
        } else if(data) {
            if(!fmt || pcm || !size || size%align) return false;
            pcm=wav+start;frames=size/align;
        }
        offset=start+size;
        if(size&1U) {if(offset==bytes) return false;offset++;}
    }
    size_t samples=(size_t)((uint64_t)frames*16000U/(rate?rate:1));
    if(!pcm || !samples || samples>1048576U || frames>(size_t)rate*180U ||
       (placeholder && !stream_size)) return false;
    *out=(xiaotai_recognition_wav_t){.pcm=pcm,.frames=frames,.samples_16k=samples,
        .sample_rate=rate,.channels=channels};
    return true;
}
static int32_t mono_at(const xiaotai_recognition_wav_t *a,size_t frame)
{
    int32_t value=(int16_t)le16(a->pcm+frame*a->channels*2);
    if(a->channels==2) value=(value+(int16_t)le16(a->pcm+frame*4+2))/2;
    return value;
}
bool xiaotai_recognition_wav16(const xiaotai_recognition_wav_t *a,int16_t *out,size_t capacity)
{
    if(!a || !a->pcm || !out || !a->frames || !a->samples_16k || a->samples_16k>capacity ||
       a->sample_rate<8000 || a->sample_rate>48000 || (a->channels!=1 && a->channels!=2)) return false;
    for(size_t i=0;i<a->samples_16k;i++) {
        uint64_t start=(uint64_t)i*a->sample_rate;
        size_t index=(size_t)(start/16000U);
        if(index>=a->frames) return false;
        if(a->sample_rate<16000) {
            /* Interpolate once for lower-rate WAV; keep the final sample. */
            unsigned fraction=(unsigned)(start%16000U);
            int32_t first=mono_at(a,index),next=mono_at(a,index+1<a->frames?index+1:index);
            out[i]=(int16_t)(((int64_t)first*(16000-fraction)+(int64_t)next*fraction)/16000);
        } else {
            /* Integrate source intervals once. Exact identity at 16 kHz; simple
             * low-pass averaging for higher rates, including fractional ratios. */
            uint64_t end=start+a->sample_rate;int64_t sum=0;
            while((uint64_t)index*16000U<end) {
                if(index>=a->frames) return false;
                uint64_t left=(uint64_t)index*16000U,right=left+16000U;
                if(left<start) left=start;
                if(right>end) right=end;
                sum+=(int64_t)mono_at(a,index)*(int64_t)(right-left);index++;
            }
            out[i]=(int16_t)(sum/a->sample_rate);
        }
    }
    return true;
}
