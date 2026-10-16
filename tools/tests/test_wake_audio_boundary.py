"""Replay captured wake/body markers through the public preroll queue."""
import unittest
from test_s3_product_regressions import ROOT, run_c

class WakeAudioBoundary(unittest.TestCase):
    def test_detection_boundary_preserves_inference_time_speech_only(self):
        common = ROOT / 'platforms/esp-idf/components/starter_media_common'
        run_c(r'''
#include <assert.h>
#include "starter_preroll.h"
''' + (common / 'src/starter_preroll.c').read_text() + r'''
static int16_t memory[PREROLL_CAPACITY_SAMPLES],frame[160],out[160];
static void append(starter_preroll_t *q,int value,int from,int to){
 for(int i=0;i<160;i++)frame[i]=value;
 for(int ms=from;ms<=to;ms+=20)starter_preroll_append(q,frame,160,ms);
}
int main(void){
 starter_preroll_t q;starter_preroll_init(&q,memory,PREROLL_CAPACITY_SAMPLES);
 append(&q,11,20,1500); /* wake detector input */
 append(&q,22,1520,1740); /* user speaks while inference finishes */
 unsigned token=starter_preroll_prepare(&q,1500,1740);
 assert(token && q.timestamp_ms==1500 && q.count==1920);
 append(&q,33,1760,1900); /* speech during connection */
 assert(starter_preroll_bind(&q,token,1,1900));
 unsigned timestamp=0,count=0;
 while(starter_preroll_peek(&q,1,out,160,&timestamp)){
  assert(timestamp==1500+count/8);
  for(int i=0;i<160;i++)assert(out[i]==(count<1920?22:33));
  count+=160;starter_preroll_consume(&q,160);
 }
 assert(count==3200);
 starter_preroll_clear(&q);
 append(&q,11,1920,2000);
 token=starter_preroll_prepare(&q,2000,2000);
 assert(token && q.count==0); /* no new speech yet is a valid wake */
 assert(starter_preroll_bind(&q,token,2,2000));
 append(&q,44,2020,2040);
 assert(starter_preroll_peek(&q,2,out,160,&timestamp) && timestamp==2000 && out[0]==44);
 starter_preroll_clear(&q);
 append(&q,11,2060,2100);
 assert(starter_preroll_prepare(&q,2000,3101)==0); /* stale detection */
 assert(starter_preroll_prepare(&q,2120,2120)==0); /* capture has not reached boundary */
 return 0;
}
''', [common / 'include'])

if __name__=='__main__': unittest.main()
