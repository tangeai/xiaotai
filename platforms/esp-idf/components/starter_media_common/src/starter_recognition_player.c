#include "starter_recognition_player.h"
unsigned starter_recognition_player_epoch(starter_recognition_player_t *p)
{ return atomic_load(&p->epoch); }
bool starter_recognition_player_busy(starter_recognition_player_t *p)
{ return atomic_load(&p->busy); }
void starter_recognition_player_cancel(starter_recognition_player_t *p)
{ atomic_fetch_add(&p->epoch,1); }
int starter_recognition_player_play(starter_recognition_player_t *p,
    const starter_recognition_output_t *out, const int16_t *pcm, size_t samples,
    unsigned epoch)
{
    if(!p || !out || !out->begin || !out->write || !out->end || !pcm ||
       !samples || samples>1048576U) return -1;
    bool expected=false;
    if(!atomic_compare_exchange_strong(&p->busy,&expected,true)) return -1;
    int err=-1;
    if(starter_recognition_player_epoch(p)!=epoch) goto done;
    err=out->begin(out->context,epoch);
    if(err) goto done;
    int16_t stereo[640]; /* private 20 ms block; never use live-call buffers */
    for(size_t offset=0;offset<samples;) {
        if(starter_recognition_player_epoch(p)!=epoch) {err=-1;break;}
        size_t n=samples-offset; if(n>320) n=320;
        for(size_t i=0;i<n;i++) stereo[2*i]=stereo[2*i+1]=pcm[offset+i];
        err=out->write(out->context,stereo,n,epoch);
        if(err) break;
        offset+=n;
    }
    if(starter_recognition_player_epoch(p)!=epoch && !err) err=-1;
    int cleanup=out->end(out->context);
    if(!err) err=cleanup;
done:
    atomic_store(&p->busy,false);
    return err;
}
