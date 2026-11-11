"""Exercise the production fanout with SDK callbacks, including reentrant close."""
import unittest
from test_s3_product_regressions import ROOT, run_c

class FourViewers(unittest.TestCase):
    def test_watchers_subscriptions_talk_owner_and_lifetime(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
typedef void *tirtc_conn_t;
typedef struct {unsigned stream_id,media,flags,ts,length;} TIRTCFRAMEINFO;
#define TIRTC_E_BUSY -1
#define TIRTC_E_INVALID_HANDLE -2
#define TIRTC_VIDEO_H264 1
#define TIRTC_FRAME_FLAG_KEY_FRAME 1
static atomic_uintptr_t s_connection;
static atomic_bool s_audio_subscribed,s_video_subscribed;
static bool active,accepting=true;
static unsigned starts,ends,audio_rx,commands,subscriptions[9],sent_audio[9],sent_video[9],closed[9],keyframes;
static unsigned lock_depth;
static size_t backlog[9];
static unsigned ident(void*c){return (unsigned)(uintptr_t)c;}
static unsigned h5_fanout_lock(void){assert(!lock_depth);return lock_depth++;}
static void h5_fanout_unlock(unsigned token){assert(lock_depth==1);lock_depth--;}
static void on_conn_accepted(void*c){if(!s_connection && accepting){s_connection=(uintptr_t)c;active=true;starts++;}}
static void on_disconnected(void*c){if(s_connection==(uintptr_t)c){s_connection=0;active=false;ends++;}}
static void on_conn_error(void*c,int e){assert(false);}
static void on_audio(void*c,const TIRTCFRAMEINFO*f,void*d){assert(s_connection==(uintptr_t)c);audio_rx++;}
static void on_command(void*c,uint32_t cmd,const void*d,uint32_t n){commands++;}
static int on_subscribe_audio(void*c,uint8_t id){if(id!=10)return -1;s_audio_subscribed=true;return 0;}
static int on_subscribe_video(void*c,uint8_t id){if(id!=11)return -1;s_video_subscribed=true;keyframes++;return 0;}
static void on_unsubscribe_audio(void*c,uint8_t id){if(id==10)s_audio_subscribed=false;}
static void on_unsubscribe_video(void*c,uint8_t id){if(id==11)s_video_subscribed=false;}
static void on_request_key_frame(void*c,uint8_t id){keyframes++;}
static void h5_fanout_on_disconnected(void*);
static void h5_fanout_on_conn_error(void*,int);
static int TiRtcDisconnect(void*c){assert(!lock_depth);assert(!closed[ident(c)]);closed[ident(c)]++;h5_fanout_on_disconnected(c);return 0;}
static int TiRtcSubscribeAudio(void*c,uint8_t id){assert(!lock_depth && id==14);subscriptions[ident(c)]++;return 0;}
static bool close_during_send;
static unsigned fail_video_id;
static int TiRtcSendAudioStream(void*c,const TIRTCFRAMEINFO*f,const void*d){
 assert(!lock_depth && !closed[ident(c)]);sent_audio[ident(c)]++;
 if(close_during_send && ident(c)==1){close_during_send=false;h5_fanout_on_conn_error(c,-3);assert(!closed[1]);}
 return 0;
}
static int TiRtcSendVideoStream(void*c,const TIRTCFRAMEINFO*f,const void*d){assert(!lock_depth&&!closed[ident(c)]);if(ident(c)==fail_video_id)return -1;sent_video[ident(c)]++;return 0;}
static size_t TiRtcGetSendBufferUsed(void*c){assert(!closed[ident(c)]);return backlog[ident(c)];}
#define H5_FANOUT_ACTIVE() (active)
#define H5_FANOUT_ACCEPTING() (accepting)
#define H5_FANOUT_LAST_CLOSED() (accepting=false)
#define H5_FANOUT_NOW_MS() 1000U
#define H5_FANOUT_AUDIO_ID 10
#define H5_FANOUT_VIDEO_ID 11
#define H5_FANOUT_DOWN_AUDIO_ID 14
#define H5_FANOUT_SEND_LIMIT() 1024U
#define H5_FANOUT_UPLINK_CHANGED() ((void)0)
#include "h5_fanout_api.h"
#include "h5_fanout_impl.h"
int main(void){
 for(uintptr_t i=1;i<=4;i++)h5_fanout_on_conn_accepted((void*)i);
 assert(starts==1 && s_connection==1);
 h5_fanout_on_conn_accepted((void*)5);assert(closed[5] && starts==1);
 for(uintptr_t i=1;i<=4;i++){
  assert(h5_fanout_on_subscribe_audio((void*)i,10)==0);
  assert(h5_fanout_on_subscribe_video((void*)i,11)==0);
 }
 TIRTCFRAMEINFO f={.stream_id=10,.length=160};
 assert(h5_fanout_send_audio((void*)1,&f,"x")==0);
 for(unsigned i=1;i<=4;i++)assert(sent_audio[i]==1);
 for(uintptr_t i=1;i<=4;i++)h5_fanout_on_audio((void*)i,&f,"x");
 assert(audio_rx==1);h5_fanout_on_command((void*)2,0,"x",1);assert(!commands);
 h5_fanout_on_unsubscribe_audio((void*)2,10);assert(s_audio_subscribed);
 h5_fanout_on_subscribe_audio((void*)2,10);
 close_during_send=true;h5_fanout_send_audio((void*)1,&f,"x");
 assert(closed[1] && s_connection==2 && subscriptions[2]==1 && ends==0);
 h5_fanout_on_audio((void*)1,&f,"x");h5_fanout_on_audio((void*)2,&f,"x");assert(audio_rx==2);
 /* One congested viewer must not suppress healthy JPEG viewers. */
 backlog[3]=1024;f.stream_id=11;f.media=0;
 assert(h5_fanout_send_video((void*)2,&f,"x")==0);
 assert(sent_video[2]==1 && sent_video[3]==0 && sent_video[4]==1);
 /* A joining or congested H264 viewer waits for an IDR. */
 f.media=TIRTC_VIDEO_H264;f.flags=0;h5_fanout_send_video((void*)2,&f,"x");
 backlog[3]=0;f.flags=TIRTC_FRAME_FLAG_KEY_FRAME;h5_fanout_send_video((void*)2,&f,"x");
 assert(sent_video[3]==1);
 unsigned before=sent_video[3];fail_video_id=3;f.flags=0;
 h5_fanout_send_video((void*)2,&f,"x");fail_video_id=0;
 h5_fanout_send_video((void*)2,&f,"x");assert(sent_video[3]==before);
 f.flags=TIRTC_FRAME_FLAG_KEY_FRAME;h5_fanout_send_video((void*)2,&f,"x");
 assert(sent_video[3]==before+1); /* resume only at IDR */
 /* Caller may have read the old primary just before promotion. Never send to
  * that freed handle; route to the still-active logical H5 group. */
 f.stream_id=10;assert(h5_fanout_send_audio((void*)1,&f,"x")==0);
 h5_fanout_on_conn_error((void*)4,-3);assert(closed[4] && s_connection==2 && ends==0);
 h5_fanout_on_conn_accepted((void*)6);assert(!closed[6]);
 h5_fanout_on_conn_error((void*)2,-3);assert(s_connection==3 && subscriptions[3]==1);
 /* Explicit session teardown closes everyone, without a temporary transfer. */
 assert(h5_fanout_disconnect((void*)3)==0);
 assert(closed[3] && closed[6] && ends==1);
 h5_fanout_on_disconnected((void*)1);assert(ends==1);
 assert(!active && !s_connection);
 assert(h5_fanout_send_audio((void*)3,&f,"x")==TIRTC_E_INVALID_HANDLE);
 /* Runtime has not consumed the last-close event yet: reject an early
  * replacement instead of letting old cleanup tear down the new group. */
 h5_fanout_on_conn_accepted((void*)8);assert(!s_connection);
 for(unsigned round=0;round<100;round++) {
  accepting=true; /* runtime completed old-group teardown */
  memset(closed,0,sizeof(closed));
  for(uintptr_t i=1;i<=4;i++)h5_fanout_on_conn_accepted((void*)i);
  assert(s_connection==1);
  for(uintptr_t i=1;i<=4;i++) {
   h5_fanout_on_conn_error((void*)i,-3);
   assert(closed[i]);
   assert(s_connection==(i<4?i+1:0));
  }
 }
 assert(starts==101 && ends==101);
 /* Foreground calls retain their single connection and SDK submission path. */
 s_connection=7;f.stream_id=0;
 assert(h5_fanout_send_audio((void*)7,&f,"x")==0);
 assert(sent_audio[7]==1);

}
''', [ROOT/'platforms/common/tirtc'])

if __name__=='__main__':unittest.main()
