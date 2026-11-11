"""Bounded opt-in audio evidence: complete packets/PCM, generation and export."""
import unittest
import importlib.util
import struct
from test_s3_product_regressions import MEDIA, function
from test_s3_product_regressions import ROOT, run_c


class S3AudioCapture(unittest.TestCase):
    def test_records_exact_packets_and_pcm_without_crossing_capture_bounds(self):
        run_c(r'''
#include <assert.h>
#include <string.h>
#include "starter_audio_capture.h"
static uint32_t word(const uint8_t *p) {
 return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
int main(void) {
 uint8_t storage[128];memset(storage,0xa5,sizeof(storage));
 uint8_t opus[]={0x48,0x12,0x34},pcm[]={1,2,3,4};
 starter_audio_capture_t c={0};
 starter_audio_capture_meta_t m={.generation=7,.timestamp_ms=123,.sample_rate=16000,
   .channels=1,.bits=16,.consumed=3,.decode_ms=140,.write_ms=160,.played=1};
 assert(!starter_audio_capture_record(&c,&m,opus,3,pcm,4)); /* opt in */
 starter_audio_capture_arm(&c,storage,110);
 assert(starter_audio_capture_record(&c,&m,opus,3,pcm,4));
 assert(c.count==1 && c.used==55 && c.armed);
 assert(word(storage)==0x31445541 && word(storage+4)==7);
 assert(word(storage+8)==123 && word(storage+12)==16000);
 assert(word(storage+24)==3 && word(storage+28)==3 && word(storage+32)==4);
 assert(word(storage+36)==140 && word(storage+40)==160 && word(storage+44)==1);
 assert(!memcmp(storage+48,opus,3) && !memcmp(storage+51,pcm,4));
 assert(starter_audio_capture_record(&c,&m,opus,3,pcm,4));
 assert(!starter_audio_capture_record(&c,&m,opus,3,pcm,4));
 assert(!c.armed && c.count==2 && c.used==110 && c.reason==STARTER_AUDIO_CAPTURE_FULL);
 for(unsigned i=110;i<128;i++)assert(storage[i]==0xa5);
 starter_audio_capture_arm(&c,storage,sizeof(storage));
 assert(starter_audio_capture_record(&c,&m,opus,3,pcm,4));m.generation++;
 assert(!starter_audio_capture_record(&c,&m,opus,3,pcm,4));
 assert(c.count==1 && c.reason==STARTER_AUDIO_CAPTURE_GENERATION);
 starter_audio_capture_arm(&c,storage,sizeof(storage));
 assert(!starter_audio_capture_record(&c,&m,NULL,3,pcm,4));
 assert(c.used==0 && c.count==0);
 assert(!starter_audio_capture_record(&c,&m,opus,3,pcm,SIZE_MAX));
 assert(c.used==0);
 starter_audio_capture_arm(&c,storage,sizeof(storage));
 starter_audio_capture_stop(&c);
 assert(!starter_audio_capture_record(&c,&m,opus,3,pcm,4));
 assert(c.used==0 && c.reason==STARTER_AUDIO_CAPTURE_STOPPED);
}
''', [ROOT / 'platforms/esp-idf/components/starter_media_common/include'])

    def test_serial_export_rejects_missing_chunks_and_incomplete_records(self):
        spec=importlib.util.spec_from_file_location('audio_export', ROOT/'tools/decode_ai_audio_capture.py')
        exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
        packet=b'\xf8\xff\xfe'  # Opus 20 ms silence.
        raw=struct.pack('<12I',0x31445541,7,123,16000,1,16,3,3,640,140,160,1)+packet+bytes(640)
        text=f'AUDIO_CAPTURE_DATA 0 {raw.hex()}\nAUDIO_CAPTURE_END bytes={len(raw)}\n'
        rows=exporter.parse_dump(text)
        self.assertEqual(rows[0]['opus'],packet)
        self.assertEqual(rows[0]['pcm'],bytes(640))
        self.assertEqual(len(exporter.decode_opus(rows)[0]),640)
        for broken in (text.split('AUDIO_CAPTURE_END')[0], text.replace('DATA 0','DATA 1'),
                       text.replace(f'bytes={len(raw)}', 'bytes=1')):
            with self.assertRaises(ValueError): exporter.parse_dump(broken)

    def test_export_keeps_raw_packets_and_reports_pcm_comparison(self):
        import tempfile
        from pathlib import Path
        spec=importlib.util.spec_from_file_location('audio_export', ROOT/'tools/decode_ai_audio_capture.py')
        exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
        packet=b'\xf8\xff\xfe'
        record=dict(generation=7,timestamp_ms=123,sample_rate=16000,channels=1,
            bits=16,consumed=3,decode_ms=140,write_ms=160,played=True,
            opus=packet,pcm=bytes(640))
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'evidence'
            report=exporter.export_capture([record],out,'opus')
            self.assertEqual((out/'raw-packets/000000.bin').read_bytes(),packet)
            self.assertTrue(report['pcm_identical'])
            self.assertTrue((out/'comparison.json').is_file())
            with self.assertRaises(FileExistsError):exporter.export_capture([record],out,'opus')

    def test_media_capture_requires_idle_and_stopped_export(self):
        source=MEDIA.read_text()
        names=('starter_media_audio_capture_arm','starter_media_audio_capture_stop',
               'starter_media_audio_capture_clear','starter_media_audio_capture_status',
               'starter_media_audio_capture_read')
        run_c(r'''
#include <assert.h>
#include <stdlib.h>
#include "starter_audio_capture.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_NO_MEM -3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdTRUE 1
#define portMAX_DELAY 999
#define pdMS_TO_TICKS(x) (x)
#define atomic_load(p) (*(p))
typedef int esp_err_t;
static int s_audio_output_mutex=1;
static bool s_active,locked,fail_alloc;
static starter_audio_capture_t s_audio_capture;
static int xSemaphoreTake(int h,int t){assert(!locked);locked=true;return 1;}
static void xSemaphoreGive(int h){assert(locked);locked=false;}
static void *heap_caps_malloc(size_t n,int caps){return fail_alloc?NULL:malloc(n);}
''' + '\n'.join(function(source,n) for n in names) + r'''
int main(void){
 s_active=true;assert(starter_media_audio_capture_arm()==ESP_ERR_INVALID_STATE);
 s_active=false;fail_alloc=true;assert(starter_media_audio_capture_arm()==ESP_ERR_NO_MEM);
 fail_alloc=false;assert(starter_media_audio_capture_arm()==ESP_OK);
 uint8_t raw[]={0x48},pcm[]={1,2},out[100];
 starter_audio_capture_meta_t m={.generation=7};
 assert(starter_audio_capture_record(&s_audio_capture,&m,raw,1,pcm,2));
 assert(starter_media_audio_capture_read(0,out,sizeof(out))==0);
 starter_media_audio_capture_stop();
 size_t size;uint32_t count,reason;bool armed;
 starter_media_audio_capture_status(&size,&count,&armed,&reason);
 assert(size==51 && count==1 && !armed);
 assert(starter_media_audio_capture_read(0,out,sizeof(out))==51);
 assert(out[48]==0x48 && out[49]==1 && out[50]==2);
 assert(starter_media_audio_capture_read(51,out,sizeof(out))==0);
 starter_media_audio_capture_clear();assert(s_audio_capture.storage==NULL);
 assert(starter_media_audio_capture_read(0,out,sizeof(out))==0);
 assert(!locked);
}
''', [ROOT/'platforms/esp-idf/components/starter_media_common/include'])

if __name__ == '__main__':
    unittest.main()
