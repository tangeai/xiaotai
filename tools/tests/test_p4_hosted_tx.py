"""Exercise ESP-Hosted TX ownership and empty-wakeup recovery at its SDK seam."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
SOURCE = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c'
COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_SERIAL_IF 0
#define ESP_HCI_IF 1
#define ESP_STA_IF 2
#define PRIO_Q_SERIAL 0
#define PRIO_Q_BT 1
#define PRIO_Q_OTHERS 2
#define HOSTED_BLOCK_MAX -1
#define MAX_PAYLOAD_SIZE 128
#define MAX_SDIO_BUFFER_SIZE 256
#define ESP_RX_BUFFER_SIZE 256
#define ESP_TX_BUFFER_MAX 4096
#define MAX_SDIO_WRITE_RETRY 2
#define ESP_SLAVE_CMD53_END_ADDR 4096
#define BUFFER_AVAILABLE 0
#define ACQUIRE_LOCK 1
#define MEMSET_REQUIRED 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_HEXLOGV(...) ((void)0)
#define htole16(x) (x)
#define SDIO_DRV_LOCK() ((void)0)
#define SDIO_DRV_UNLOCK() ((void)0)
#define H_FREE_PTR_WITH_FUNC(fn,p) do{if((fn)&&(p)){(fn)(p);(p)=NULL;}}while(0)
typedef struct {uint16_t payload_len,seq_num;uint8_t payload_zcopy,if_type,if_num,flag;uint8_t *payload;void *priv_buffer_handle;void(*free_buf_handle)(void*);} interface_buffer_handle_t;
struct esp_payload_header {uint16_t len,offset,seq_num;uint8_t if_type,if_num,flags,hci_pkt_type;};
static void *to_slave_queue[3]={(void*)1,(void*)2,(void*)3};
static void *sem_to_slave_queue=(void*)4,*sdio_handle=(void*)5;
static bool sdio_start_write_thread=true;
static uint32_t sdio_tx_buf_count;
static unsigned step,writes,posts,freed[2],empty_step=2,failed_wait_step;
static int queue_result;
static jmp_buf finished;
static uint8_t packets[2][256];
static void release(void *p){for(unsigned i=0;i<2;i++)if(p==packets[i]){assert(!freed[i]);freed[i]++;return;}assert(0);}
static void msleep(int n){(void)n;}
static int take(void *s,int t){(void)s;(void)t;if(++step==4)longjmp(finished,1);return step==failed_wait_step?-1:0;}
static int dequeue(void *q,void *out,int timeout){(void)timeout;
 if(step==empty_step || q!=to_slave_queue[2])return -1;
 unsigned i=step==3?1:0;
 *(interface_buffer_handle_t*)out=(interface_buffer_handle_t){.payload_len=16,.payload_zcopy=1,.if_type=ESP_STA_IF,.payload=packets[i],.priv_buffer_handle=packets[i],.free_buf_handle=release};return 0;}
static int enqueue(void*q,void*i,int t){(void)q;(void)i;(void)t;return queue_result;}
static int post(void*s){(void)s;posts++;return 0;}
static int write_block(void*h,uint32_t a,void*p,uint32_t n,int l){(void)h;(void)a;(void)n;(void)l;assert(p==packets[0]||p==packets[1]);unsigned i=p==packets[0]?0:1;assert(!freed[i]);writes++;return 0;}
static void restart_host(void){assert(0);}
static void *sdio_buffer_alloc(int n){(void)n;return calloc(1,256);}
static void sdio_buffer_free(void*p){free(p);}
static int sdio_is_write_buffer_available(uint32_t n){(void)n;return BUFFER_AVAILABLE;}
static bool is_transport_tx_ready(void){return true;}
static struct {struct {void(*_h_msleep)(int);int(*_h_get_semaphore)(void*,int);int(*_h_dequeue_item)(void*,void*,int);void*(*_h_memcpy)(void*,const void*,size_t);int(*_h_sdio_write_block)(void*,uint32_t,void*,uint32_t,int);void(*_h_restart_host)(void);int(*_h_queue_item)(void*,void*,int);int(*_h_post_semaphore)(void*);} *funcs;} g_h;
'''
class HostedTx(unittest.TestCase):
 def test_first_empty_wakeup_does_not_disable_future_transmission(self):
  s=SOURCE.read_text()
  run_c(COMMON+function(s,'sdio_write_task')+r'''
int main(void){
 static __typeof__(*g_h.funcs) funcs={msleep,take,dequeue,memcpy,write_block,restart_host,enqueue,post};g_h.funcs=&funcs;
 empty_step=1;
 if(!setjmp(finished))sdio_write_task(NULL);
 assert(writes==2 && freed[0]==1 && freed[1]==1);return 0;}
''')
 def test_failed_semaphore_wait_cannot_consume_another_packet(self):
  s=SOURCE.read_text()
  run_c(COMMON+function(s,'sdio_write_task')+r'''
int main(void){
 static __typeof__(*g_h.funcs) funcs={msleep,take,dequeue,memcpy,write_block,restart_host,enqueue,post};g_h.funcs=&funcs;
 failed_wait_step=2;empty_step=0;
 if(!setjmp(finished))sdio_write_task(NULL);
 assert(writes==2 && freed[0]==1 && freed[1]==1);return 0;}
''')
 def test_empty_wakeup_never_reuses_a_freed_packet_and_next_packet_sends(self):
  s=SOURCE.read_text()
  run_c(COMMON+function(s,'sdio_write_task')+r'''
int main(void){
 static __typeof__(*g_h.funcs) funcs={msleep,take,dequeue,memcpy,write_block,restart_host,enqueue,post};g_h.funcs=&funcs;
 if(!setjmp(finished))sdio_write_task(NULL);
 assert(writes==2 && freed[0]==1 && freed[1]==1);return 0;}
''')
 def test_enqueue_failure_returns_ownership_and_does_not_signal_phantom_packet(self):
  s=SOURCE.read_text()
  run_c(COMMON+r'''
#define ESP_PKT_STATS 1
static struct {atomic_uint sta_tx_in_drop,sta_tx_in_pass;} pkt_stats;
'''+function(s,'esp_hosted_tx')+r'''
int main(void){(void)sdio_tx_buf_count;(void)sdio_start_write_thread;(void)sdio_handle;static __typeof__(*g_h.funcs) funcs={msleep,take,dequeue,memcpy,write_block,restart_host,enqueue,post};g_h.funcs=&funcs;
 queue_result=-1;
 assert(esp_hosted_tx(ESP_STA_IF,0,packets[0],16,1,release)==ESP_FAIL);
 assert(freed[0]==1 && posts==0);
 assert(pkt_stats.sta_tx_in_drop==1 && pkt_stats.sta_tx_in_pass==0);
 queue_result=0;
 assert(esp_hosted_tx(ESP_STA_IF,0,packets[1],16,1,release)==ESP_OK);
 assert(freed[1]==0 && posts==1);
 assert(pkt_stats.sta_tx_in_drop==1 && pkt_stats.sta_tx_in_pass==1);
}
''')
if __name__=='__main__':unittest.main()
