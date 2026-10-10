"""Replay the actual SDIO receive loop across its 16-bit HAL boundary."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c
SOURCE=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c'
class HostedRxTransfer(unittest.TestCase):
 def test_stream_batches_preserve_every_byte_with_bounded_fifo_requests(self):
  s=SOURCE.read_text()
  body=function(s,'sdio_read_task')
  start=body.index('data_left = len_from_slave;')
  stop=body.index('sdio_rx_batch_t batch =', start)
  loop=body[start:stop]
  constants='\n'.join(re.findall(r'^#define SDIO_RX_TRANSFER_MAX_BYTES .*$',s,re.M))
  for block_mode in (0,1):
   with self.subTest(block_mode=block_mode):
    run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP_BLOCK_SIZE 512U
#define ESP_SLAVE_CMD53_END_ADDR 0x1F800U
#define ACQUIRE_LOCK false
#define ESP_LOGE(...) ((void)0)
#define ESP_RX_BYTE_MAX 1048576U
#define HOSTED_BLOCK_MAX 0
#define unlikely(x) (x)
static bool locked;
#define SDIO_DRV_UNLOCK() (locked=false)
static uint32_t sdio_rx_byte_count;
static int returned,restarted;
static void *sdio_rx_free_buf_queue;
static int queue_item(void*q,void*p,int timeout){(void)q;(void)p;(void)timeout;assert(!locked);returned++;return 0;}
static int restart_host(void){assert(!locked && returned==1);restarted++;return 0;}
static uint32_t expected,done,calls,failed_call;
static bool freed;
static void *sdio_handle;
static int read_block(void*c,uint32_t addr,uint8_t*p,uint16_t size,bool lock){
 (void)c;(void)lock;
 assert(size>0 && size<=65024U);
 uint32_t request=expected-done;
 if(request>65024U)request=65024U;
 assert(addr==ESP_SLAVE_CMD53_END_ADDR-request);
 assert(size>=request && size-request<512U);
 if(++calls==failed_call)return -1;
 for(uint32_t i=0;i<request;i++)p[i]=(uint8_t)((done+i)*17U+3U);
 if(size>request)memset(p+request,0,size-request);
 done+=request;return 0;
}
static struct {struct {int(*_h_sdio_read_block)(void*,uint32_t,uint8_t*,uint16_t,bool);int(*_h_queue_item)(void*,void*,int);int(*_h_restart_host)(void);} *funcs;} g_h;
static void sdio_rx_free_buffer(void*p){(void)p;freed=true;}
static void replay(uint32_t len_from_slave,uint32_t fail,uint32_t initial){
 uint8_t *rxbuff=malloc(len_from_slave+512U),*pos;
 assert(rxbuff);memset(rxbuff,0xcc,len_from_slave+512U);
 uint32_t data_left=0,len_to_read;int ret=0,rx_buffer_index=0;
 expected=len_from_slave;done=calls=0;freed=false;
 failed_call=fail;sdio_rx_byte_count=initial;returned=restarted=0;locked=true;
 bool completed=false;
 do {
''' + '\n#define H_SDIO_RX_BLOCK_ONLY_XFER '+str(block_mode)+'\n'+constants+'\n'+loop+r'''
 completed=true;
 } while(0);
 assert(!locked);
 assert(sdio_rx_byte_count==(initial+done)%ESP_RX_BYTE_MAX);
 if(fail){assert(ret && freed && returned==1 && restarted==1 && !completed);}
 else {assert(!ret && !freed && done==expected && data_left==0);
 assert(completed && !returned && !restarted);
 for(uint32_t i=0;i<len_from_slave;i++)assert(rxbuff[i]==(uint8_t)(i*17U+3U));
 assert(rxbuff[len_from_slave+511U]==0xcc);
 }
 free(rxbuff);
}
int main(void){
 static __typeof__(*g_h.funcs) funcs={read_block,queue_item,restart_host};g_h.funcs=&funcs;
 uint32_t sizes[]={1,160,65536,65024,65025,65535,65537,131072,262144};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++)replay(sizes[i],0,ESP_RX_BYTE_MAX-80U);
 for(unsigned fail=1;fail<=3;fail++)replay(131072,fail,ESP_RX_BYTE_MAX-80U);
}
''')
if __name__=='__main__':unittest.main()
