"""Bounded trace keeps duplicates and refuses incomplete evidence as success."""
import unittest
from test_s3_product_regressions import ROOT, run_c

class AudioPacketTrace(unittest.TestCase):
    def test_callback_trace_runs_before_format_rejection(self):
        from unittest.mock import patch
        import test_release_logging as existing
        captured=[]
        with patch.object(existing,'run_c',captured.append):
            existing.ReleaseLogging().test_receive_callback_preserves_delivery_without_periodic_info_io()
        base=ROOT/'platforms/esp-idf/components/starter_tirtc'
        pre='#define CONFIG_STARTER_AUDIO_PACKET_TRACE 1\n#include "'+str(base/'include/starter_audio_trace.h')+'"\n'
        pre+=r"""
static unsigned traced;
static long long esp_timer_get_time(void){return 0;}
void starter_audio_trace_record(unsigned s,uint32_t g,uint32_t t,uint32_t l,unsigned m,unsigned f,unsigned id,const void*d,uint32_t at,uint32_t detail){traced++;}
"""
        code=captured[0];last=code.rfind('}')
        run_c(pre+code[:last]+'assert(traced==11000);'+code[last:])

    def test_clear_releases_trace_and_allows_rearm(self):
        base=ROOT/'platforms/esp-idf/components/starter_tirtc'
        code = '#include "'+str(base/'src/starter_audio_trace.c')+'"\n'
        code += '#include <assert.h>\nint main(void){assert(starter_audio_trace_arm()); starter_audio_trace_clear(); assert(trace_rows==NULL); size_t n;uint32_t lost;bool armed;starter_audio_trace_status(&n,&lost,&armed);assert(!n&&!lost&&!armed); assert(starter_audio_trace_arm());starter_audio_trace_clear();starter_audio_trace_clear();assert(trace_rows==NULL);}'
        run_c(code, [base/"include"])

    def test_opt_in_bounded_capture_and_stopped_read(self):
        base=ROOT/'platforms/esp-idf/components/starter_tirtc'
        run_c('#include "'+str(base/'src/starter_audio_trace.c')+'"\n'+r'''
#include <assert.h>
int main(void){
 size_t n;uint32_t lost;bool armed;starter_audio_trace_row_t row;
 uint8_t p[]={1,2,3};
 starter_audio_trace_record(1,7,20,3,4,1,1,p,100,0);
 starter_audio_trace_status(&n,&lost,&armed);assert(n==0&&!armed&&!lost);
 assert(starter_audio_trace_arm());assert(!starter_audio_trace_arm());
 for(unsigned s=1;s<=4;s++)starter_audio_trace_record(s,7,20,3,4,1,1,p,100+s,0);
 starter_audio_trace_record(1,7,20,3,4,1,1,p,110,0);
 assert(!starter_audio_trace_read(0,&row));
 starter_audio_trace_stop();assert(starter_audio_trace_read(0,&row));
 uint32_t hash=row.hash;
 for(unsigned i=0;i<5;i++){assert(starter_audio_trace_read(i,&row));assert(row.hash==hash && row.length==3);}
 assert(!starter_audio_trace_read(5,&row));
 assert(starter_audio_trace_arm());
 atomic_flag_test_and_set(&trace_lock);
 starter_audio_trace_record(1,7,20,3,4,1,1,p,100,0);
 atomic_flag_clear(&trace_lock);
 for(unsigned i=0;i<4097;i++)starter_audio_trace_record(1,7,20,3,4,1,1,p,100,0);
 starter_audio_trace_stop();starter_audio_trace_status(&n,&lost,&armed);
 assert(n==4096 && lost==2 && !armed);
 assert(starter_audio_trace_arm());
 starter_audio_trace_record(1,8,20,1501,4,1,1,NULL,120,0);
 starter_audio_trace_stop();assert(starter_audio_trace_read(0,&row) && row.hash==0);
 free(trace_rows);
}
''',[base/'include'])

if __name__=='__main__':unittest.main()
