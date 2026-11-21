"""Replay pinned SDK close ownership; no firmware/toolchain build is invoked."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_s3_product_regressions import ROOT, function, run_c

PATCHES = ROOT / 'platforms/beken/tools/patches'
FIXTURE = ROOT / 'tools/tests/fixtures/bk_audio_lifecycle'

class SDKAudioLifecycle(unittest.TestCase):
    def patched(self, directory):
        shutil.copytree(FIXTURE, directory, dirs_exist_ok=True)
        subprocess.run(['patch', '--batch', '-p1', '-i', str(PATCHES/'bk-avdk-audio-lifecycle.patch')],
                       cwd=directory, check=True, capture_output=True)

    def test_close_retains_resources_until_exit_and_detaches_interrupts(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.patched(temporary)
            for kind, stem, part, direction, worker, buf in [
                ('record','mic','mic_record','adc','mic_data_write','write'),
                ('play','speaker','speaker_play','dac','spk_data_read','read')]:
                with self.subTest(kind=kind):
                    source=(Path(temporary)/f'ap/components/audio_{kind}/src/onboard_{part}.c').read_text()
                    generic=(Path(temporary)/f'ap/components/audio_{kind}/src/audio_{kind}.c').read_text()
                    # Common fake names preserve execution of both production variants.
                    for old,new in [(f'onboard_{part}_priv_t','priv_t'),(f'audio_{kind}_t','audio_t'),
                                    (f'gl_onboard_{part}','current'),(f'{direction}_dma_id','dma_id'),
                                    (f'{worker}_msg_que','queue'),(f'{buf}_buff','buffer')]:
                        source=source.replace(old,new);generic=generic.replace(old,new)
                    code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int bk_err_t;
#define BK_OK 0
#define BK_FAIL -1
#define LOGE(...) ((void)0)
#define LOGD(...) ((void)0)
#define ONBOARD_MIC_RECORD_CHECK_NULL(p) do{if(!(p))return -1;}while(0)
#define ONBOARD_SPK_PLAY_CHECK_NULL(p) ONBOARD_MIC_RECORD_CHECK_NULL(p)
#define MIC_DATA_WRITE_EXIT 1
#define SPK_DATA_READ_EXIT 1
#define PM_DEV_ID_AUDIO 0
#define PM_CPU_FRQ_DEFAULT 0
#define AUD_MIC_DATA_DUMP_CLOSE() ((void)0)
typedef struct {int port,nChans,sampRate,bitsPerSample,adc_gain,mic_mode,volume,play_mode,frame_size,pool_size;} cfg_t;
typedef struct {cfg_t config;int frame_size,frame_num,rb_size;bool dma_ready,codec_ready,worker_ready,exit_requested;int dma_id;void *queue,*sem,*buffer,*rb,*can_process;} priv_t;
typedef struct audio {void *record_ctx,*play_ctx;struct ops {int (*close)(struct audio*);} *ops;} audio_t;
static priv_t *current;
static unsigned freed,sent,aborted,gives;
static bool locked,timeout,stop_fail;
static uint32_t rtos_enter_critical(void){assert(!locked);locked=true;return 0;}
static void rtos_exit_critical(uint32_t flags){assert(locked);locked=false;}
static int rtos_set_semaphore(void **p){assert(locked && *p);gives++;return 0;}
static void *os_malloc(size_t n){void *p=malloc(n);if(p)memset(p,0xa5,n);return p;}
#define os_memset memset
static void os_free(void *p){assert(!locked);if(p){freed++;free(p);}}
static void rb_abort(void *p){assert(p);aborted++;}
static void rb_destroy(void *p){os_free(p);}
static int rtos_get_semaphore(void **p,unsigned ms){assert(!locked && current==NULL && *p && ms==1000);return timeout?-1:0;}
static void rtos_deinit_semaphore(void **p){os_free(*p);*p=NULL;}
static int send_exit(void *q,int op,void *p){assert(q);sent++;return 0;}
static int dma_stop(int id){return stop_fail?-1:0;}
static void disable_irq(int id){}
static int codec_stop(void){return 0;}
static int codec_deconfig(void){return 0;}
static void dma_deconfig(priv_t *p){p->dma_ready=false;}
static void bk_pm_module_vote_cpu_freq(int a,int b){}
'''
                    code+=f'\n#define {worker}_send_msg send_exit\n#define bk_dma_stop dma_stop\n#define bk_dma_disable_finish_interrupt disable_irq\n#define bk_aud_{direction}_stop codec_stop\n#define {"mic_adc" if kind=="record" else "spk_dac"}_deconfig codec_deconfig\n#define aud_{direction}_dma_deconfig dma_deconfig\n'
                    for name in [f'aud_{direction}_dma_finish_isr',f'{worker}_task_deinit',f'onboard_{stem}_close',f'onboard_{part}_close']:
                        code+=function(source,name)
                    code+=f'''
#define audio_{kind}_cfg_t cfg_t
#define DMA_ID_MAX 16
#define DMA_CARRY_MIC_FRAME_NUM 2
#define DMA_CARRY_SPK_FRAME_NUM 2
#define PM_CPU_FRQ_480M 1
#define AUDIO_RECORD_STA_RUNNING 1
#define AUDIO_PLAY_STA_RUNNING 1
#define AUD_MIC_DATA_DUMP_OPEN() ((void)0)
static int onboard_{stem}_open(priv_t *p){{
 assert(!p->dma_ready && !p->codec_ready && !p->worker_ready && !p->exit_requested);
 assert(p->dma_id==DMA_ID_MAX && !p->sem && !p->can_process && !p->rb && !p->queue && !p->buffer);
 return -7;
}}
static int onboard_{stem}_start(priv_t *p){{assert(0);return -1;}}
static void onboard_{stem}_state_set(priv_t *p,int state){{assert(0);}}
'''+function(source,f'onboard_{part}_open')
                    code+=function(generic,f'audio_{kind}_close')+function(generic,f'audio_{kind}_destroy')
                    code+=f'''
int main(void){{
 struct ops ops={{onboard_{part}_close}};
 for(unsigned i=0;i<100;i++){{
  audio_t *a=calloc(1,sizeof(*a));priv_t *p=calloc(1,sizeof(*p));
  a->{kind}_ctx=p;a->ops=&ops;current=p;
  p->dma_ready=p->codec_ready=p->worker_ready=true;
  p->queue=(void*)1;p->can_process=(void*)2;
  p->sem=malloc(1);p->buffer=malloc(32);p->rb=malloc(64);
  aud_{direction}_dma_finish_isr();assert(gives==i+1);
  unsigned before=freed, sends=sent;
  stop_fail=true;assert(audio_{kind}_destroy(a)==BK_FAIL);
  assert(freed==before && current==p && a->{kind}_ctx==p);
  stop_fail=false;timeout=true;assert(audio_{kind}_destroy(a)==BK_FAIL);
  assert(current==NULL && freed==before && p->exit_requested && sent==sends+1);
  aud_{direction}_dma_finish_isr();assert(gives==i+1);
  assert(audio_{kind}_destroy(a)==BK_FAIL && sent==sends+1 && freed==before);
  timeout=false;assert(audio_{kind}_destroy(a)==BK_OK);
  assert(freed==before+5 && sent==sends+1);
  /* Zero-initialized context from an early open failure owns no worker. */
  a=calloc(1,sizeof(*a));p=calloc(1,sizeof(*p));a->{kind}_ctx=p;a->ops=&ops;current=p;
  assert(audio_{kind}_destroy(a)==BK_OK && current==NULL);
  a=calloc(1,sizeof(*a));a->ops=&ops;cfg_t config={{0}};
  assert(onboard_{part}_open(a,&config)==-7);
  assert(a->{kind}_ctx && current==a->{kind}_ctx);
  assert(audio_{kind}_destroy(a)==BK_OK && current==NULL);
 }}
}}
'''
                    run_c(code)

    def test_followup_patch_can_be_normalized_and_reapplied_twice(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk=Path(temporary);self.patched(sdk)
            # This is the actual build normalization order for overlapping audio patches.
            build=(ROOT/'platforms/beken/tools/build.sh').read_text()
            order=re.search(r'for followup in (.*); do',build).group(1).split()
            relevant=['bk-avdk-speaker-close-diagnostics.patch','bk-avdk-speaker-close-debug-level.patch','bk-avdk-audio-lifecycle.patch']
            for _ in range(2):
                for name in order:
                    if name not in relevant:continue
                    subprocess.run(['git','apply','--reverse','--ignore-whitespace',str(PATCHES/name)],cwd=sdk,check=True,capture_output=True)
                for name in relevant:
                    args=['git','apply','--ignore-whitespace']
                    probe=subprocess.run(args+['--check',str(PATCHES/name)],cwd=sdk,capture_output=True)
                    if probe.returncode==0:subprocess.run(args+[str(PATCHES/name)],cwd=sdk,check=True,capture_output=True)
                    else:subprocess.run(args+['--reverse','--check',str(PATCHES/name)],cwd=sdk,check=True,capture_output=True)

if __name__=='__main__':unittest.main()
