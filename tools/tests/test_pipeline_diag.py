#!/usr/bin/env python3
"""Production SDK submit and socket boundaries; no timing-only source assertions."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
component=root/'platforms/esp-idf/components/starter_tirtc'
src=(component/'src/starter_tirtc.c').read_text()
a=src.index('int starter_tirtc_send_alaw(');b=src.index('\n}',a)+2
code=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <pthread.h>
#include "starter_pipeline_diag.h"
static atomic_uintptr_t s_connection=1;
typedef void *tirtc_conn_t;
typedef enum {STARTER_TIRTC_H5,STARTER_TIRTC_VOIP,STARTER_TIRTC_CALL,STARTER_TIRTC_ROOM} starter_tirtc_mode_t;
#define TIRTC_E_INVALID_PARAMETER -1
#define TIRTC_AUDIO_ALAW 1
#define TIRTC_AUDIOSAMPLE_8K16B1C 1
static starter_tirtc_mode_t starter_tirtc_mode(void){return STARTER_TIRTC_CALL;}
static int audio_stream_for_mode(int m){(void)m;return 10;}
typedef struct {unsigned stream_id,media,flags,ts,length;} TIRTCFRAMEINFO;
static int sdk_ret;
static int TiRtcSendAudioStream(void *c,TIRTCFRAMEINFO *f,const void *data){assert(c&&data&&f->length==160&&f->stream_id==10);return sdk_ret;}
'''+src[a:b]+r'''
static atomic_ullong clock_us=100;
int64_t esp_timer_get_time(void){return atomic_fetch_add(&clock_us,10);}
ssize_t __real_lwip_send(int s,const void*d,size_t z,int f){assert(s==9&&d&&z==4&&f==123);errno=EINTR;return 3;}
ssize_t __real_lwip_sendto(int s,const void*d,size_t z,int f,const struct sockaddr*t,socklen_t l){(void)s;(void)d;(void)z;(void)f;(void)t;(void)l;errno=ENOMEM;return -1;}
ssize_t __real_lwip_recv(int s,void*d,size_t z,int f){(void)s;(void)f;assert(z==4);memcpy(d,"abc",4);errno=EAGAIN;return 3;}
ssize_t __real_lwip_recvfrom(int s,void*d,size_t z,int f,struct sockaddr*t,socklen_t*l){(void)s;(void)d;(void)z;(void)f;assert(t&&l);*l=2;errno=EWOULDBLOCK;return -1;}
ssize_t __wrap_lwip_send(int,const void*,size_t,int);
ssize_t __wrap_lwip_sendto(int,const void*,size_t,int,const struct sockaddr*,socklen_t);
ssize_t __wrap_lwip_recv(int,void*,size_t,int);
ssize_t __wrap_lwip_recvfrom(int,void*,size_t,int,struct sockaddr*,socklen_t*);
static void *observe(void *arg){(void)arg;for(int i=0;i<1000;i++){
 pipeline_diag_ticket_t t=starter_pipeline_diag_begin();starter_pipeline_diag_end(t,PIPELINE_VIDEO_CALLBACK,1,0,false);
}return NULL;}
int main(void){
 pipeline_diag_metric_t m={0};
 pipeline_diag_accumulate(&m,10,20,10,4,0);
 pipeline_diag_accumulate(&m,20,30,20,9,EAGAIN);
 pipeline_diag_accumulate(&m,100,120,100,6,0);
 assert(m.calls==3&&m.failures==1&&m.bytes==10&&m.gaps==1&&m.gap_max_us==90&&m.max_us==20);
 assert(!starter_pipeline_diag_enabled());
 pipeline_diag_ticket_t off=starter_pipeline_diag_begin();assert(!off.epoch);
 starter_pipeline_diag_set_enabled(true);
 uint8_t data[160]={0};assert(starter_tirtc_send_alaw(20,data,160)==0);
 sdk_ret=-35;assert(starter_tirtc_send_alaw(40,data,160)==-35);
 pipeline_diag_ticket_t old=starter_pipeline_diag_begin();starter_pipeline_diag_reset();
 starter_pipeline_diag_end(old,PIPELINE_AUDIO_CALLBACK,999,0,false);
 sdk_ret=0;assert(starter_tirtc_send_alaw(60,data,160)==0);
 sdk_ret=-35;assert(starter_tirtc_send_alaw(80,data,160)==-35);
 char bytes[4]={0};assert(__wrap_lwip_send(9,bytes,4,123)==3&&errno==EINTR);
 assert(__wrap_lwip_sendto(0,bytes,4,0,NULL,0)==-1&&errno==ENOMEM);
 assert(__wrap_lwip_recv(0,bytes,4,0)==3&&errno==EAGAIN&&!memcmp(bytes,"abc",4));
 struct sockaddr addr={0};socklen_t len=sizeof(addr);
 assert(__wrap_lwip_recvfrom(0,bytes,4,0,&addr,&len)==-1&&errno==EWOULDBLOCK&&len==2);
 starter_pipeline_diag_sample_buffer(100);starter_pipeline_diag_sample_buffer(200);
 pthread_t a,b;assert(!pthread_create(&a,NULL,observe,NULL));assert(!pthread_create(&b,NULL,observe,NULL));pthread_join(a,NULL);pthread_join(b,NULL);
 starter_pipeline_diag_report(false);
 starter_pipeline_diag_set_enabled(false);assert(!starter_pipeline_diag_begin().epoch);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='pipeline-diag-') as d:
 p=Path(d);(p/'freertos').mkdir();(p/'lwip').mkdir()
 (p/'sdkconfig.h').write_text('#ifndef CONFIG_STARTER_PIPELINE_DIAGNOSTICS\n#define CONFIG_STARTER_PIPELINE_DIAGNOSTICS 1\n#endif\n')
 (p/'freertos/FreeRTOS.h').write_text('#include <pthread.h>\ntypedef pthread_mutex_t portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER\n#define taskENTER_CRITICAL(p) pthread_mutex_lock(p)\n#define taskEXIT_CRITICAL(p) pthread_mutex_unlock(p)\n')
 (p/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
 (p/'lwip/sockets.h').write_text('#include <sys/socket.h>\n')
 (p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-DCONFIG_STARTER_PIPELINE_DIAGNOSTICS=1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-pthread','-I',str(p),'-I',str(component/'include'),str(p/'test.c'),str(component/'src/starter_pipeline_diag.c'),'-o',str(p/'test')],check=True)
 output=subprocess.check_output([str(p/'test')],text=True)
 assert 'sdk-audio-submit n=2 bytes=160 fail=1' in output,output
 assert 'audio-callback n=0 bytes=0' in output,output
 assert 'socket-send n=1 bytes=3 fail=0' in output,output
 assert 'socket-sendto n=1 bytes=0 fail=1' in output,output
 assert 'socket-recv n=1 bytes=3 fail=0' in output,output
 assert 'socket-recvfrom n=1 bytes=0 fail=1' in output,output
 assert 'video-callback n=2000 bytes=2000 fail=0' in output,output
 assert 'samples=2 first=100 last=200 peak=200' in output,output
print('PASS: production SDK submissions, timing/cadence, epoch reset and transparent errno-preserving socket wrappers')

with tempfile.TemporaryDirectory(prefix='pipeline-diag-off-') as d:
 p=Path(d);(p/'sdkconfig.h').write_text('#define CONFIG_STARTER_PIPELINE_DIAGNOSTICS 0\n')
 (p/'off.c').write_text('#include <assert.h>\n#include "starter_pipeline_diag.h"\nint main(void){starter_pipeline_diag_set_enabled(true);assert(!starter_pipeline_diag_enabled());assert(!starter_pipeline_diag_begin().epoch);starter_pipeline_diag_report(true);return 0;}\n')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(p),'-I',str(component/'include'),str(p/'off.c'),str(component/'src/starter_pipeline_diag.c'),'-o',str(p/'off')],check=True)
 subprocess.run([str(p/'off')],check=True)
print('PASS: production compile-out needs no timer, sockets, logging or FreeRTOS dependencies')
