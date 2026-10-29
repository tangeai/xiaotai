"""Execute the portable production recorder through its public PCM interface."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class RecognitionTest(unittest.TestCase):
    def test_recorder(self):
        source = r'''
#include "xiaotai_recognition.h"
#include <assert.h>
#include <stdlib.h>
int main(void) {
    xiaotai_recognition_t r = {0};
    int16_t *buffer = calloc(XIAOTAI_RECOGNITION_SAMPLES, 2);
    int16_t frame[320];
    for (int i=0;i<320;i++) frame[i]=123;
    xiaotai_recognition_init(&r, buffer);
    for(int i=0;i<250;i++) xiaotai_recognition_feed(&r,frame,320,false,true,1000+i*20);
    assert(r.pre_count==80000);
    xiaotai_recognition_feed(&r,frame,320,true,true,6000);
    assert(r.recording && r.count==80320);
    for(int i=1;i<500;i++) xiaotai_recognition_feed(&r,frame,320,false,true,6000+i*20);
    assert(r.ready && r.count==240000);
    for(size_t i=0;i<r.count;i++) assert(buffer[i]==123);
    xiaotai_recognition_release(&r,16000);
    xiaotai_recognition_feed(&r,frame,320,true,true,75999);
    assert(!r.recording);
    xiaotai_recognition_feed(&r,frame,320,true,true,76000);
    assert(r.recording);
    xiaotai_recognition_feed(&r,frame,320,true,false,76020);
    assert(!r.ready && !r.recording && !r.pre_count);
    xiaotai_recognition_feed(&r,frame,320,true,true,77000);
    assert(r.recording);
    xiaotai_recognition_feed(&r,frame,320,true,true,78000); /* lost PCM */
    assert(!r.recording && !r.ready);
    xiaotai_recognition_init(&r, buffer);
    for(int i=0;i<400;i++) {
        for(int j=0;j<320;j++) frame[j]=(int16_t)(i+1);
        xiaotai_recognition_feed(&r,frame,320,false,true,1000+i*20);
    }
    for(int j=0;j<320;j++) frame[j]=777;
    xiaotai_recognition_feed(&r,frame,320,true,true,9000);
    assert(r.recording && r.count==80320);
    for(int i=0;i<250;i++) assert(buffer[i*320]==151+i);
    assert(buffer[80000]==777);
    free(buffer);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)
            (path/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'product/include'),str(path/'test.c'),
                str(ROOT/'product/src/xiaotai_recognition.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

    def test_home_result_has_two_nonoverlapping_rows(self):
        import re
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        rows={}
        for name,x,y,width in re.findall(r'(s_recognition_\w+) = make_label\(screen, "", (\d+), (\d+), (\d+),',source):
            rows[name]=(int(x),int(y),int(width))
        status=rows['s_recognition_label']
        layout=re.search(r'lv_obj_set_pos\(result_viewport, (\d+), (\d+)\);\s*lv_obj_set_size\(result_viewport, (\d+), (\d+)\);',source)
        self.assertIsNotNone(layout)
        result=tuple(map(int,layout.groups()))
        self.assertGreaterEqual(status[1],168)
        self.assertGreaterEqual(result[1]-status[1],18)
        self.assertLessEqual(result[1]+18,208)  # page indicator begins at y=208
        self.assertLessEqual(result[0]+result[2],252)  # clear of call shortcut

    def test_result_scrolls_twice_then_hides(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        self.assertIn('start_recognition_result_scroll(result == NULL ? \"\" : result)', source)
        self.assertIn('lv_anim_set_values(&anim, 236, -width)', source)
        self.assertIn('lv_anim_set_repeat_count(&anim, 1)', source)
        self.assertIn('lv_anim_set_ready_cb(&anim, recognition_result_scroll_done)', source)
        self.assertNotIn('lv_label_set_long_mode(s_recognition_result, LV_LABEL_LONG_SCROLL_CIRCULAR)', source)

    def test_home_switch_updates_status_and_icon(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        self.assertIn('starter_recognition_set_local_enabled(!starter_recognition_local_enabled())', source)
        self.assertIn('lv_obj_add_event_cb(s_recognition_icon, on_recognition_switch, LV_EVENT_CLICKED, NULL)', source)
        self.assertIn('starter_recognition_effective_enabled()', source)
        lckfb=(ROOT/'firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/components/starter_product/CMakeLists.txt').read_text()
        m5=(ROOT/'firmware/esp-idf/esp32s3/m5stack-cores3-lite/components/starter_product/CMakeLists.txt').read_text()
        self.assertIn('XIAOTAI_RECOGNITION_HOME_SWITCH=1',lckfb)
        self.assertNotIn('XIAOTAI_RECOGNITION_HOME_SWITCH=1',m5)

    def test_home_icon_uses_ai_and_cooldown_background(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        self.assertIn('lv_label_set_text(s_recognition_ai_text, "AI")', source)
        self.assertIn('starter_recognition_cooldown_remaining_ms()', source)
        self.assertIn('lv_obj_set_size(s_recognition_cooldown_fill, 16, height)', source)
        self.assertIn('lv_obj_set_pos(s_recognition_cooldown_fill, 6, 23 - height)', source)
        self.assertNotIn('Four-point AI sparkle and two scanner dots', source)

    def test_home_status_is_centered_and_idle_notice_expires(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        self.assertIn('lv_obj_set_style_text_align(s_recognition_label, LV_TEXT_ALIGN_CENTER, 0)',source)
        self.assertIn('starter_recognition_status_visible(s_previous_recognition_status,',source)

    def test_wav(self):
        import ctypes
        import struct
        import wave
        import io
        with tempfile.TemporaryDirectory() as temp:
            libpath = pathlib.Path(temp)/'recognition.so'
            subprocess.run(['cc','-std=c11','-shared','-fPIC','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'product/include'),str(ROOT/'product/src/xiaotai_recognition.c'),
                '-o',str(libpath)],check=True)
            lib=ctypes.CDLL(str(libpath))
            decode=lib.xiaotai_recognition_wav
            decode.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
            decode.restype=ctypes.c_bool
            output=(ctypes.c_int16*8000)()
            count=ctypes.c_size_t()
            for rate in (8000,16000,24000,44100,48000):
                for channels in (1,2):
                    stream=io.BytesIO()
                    with wave.open(stream,'wb') as audio:
                        audio.setparams((channels,2,rate,0,'NONE',''))
                        audio.writeframes(struct.pack('<h',1234)*rate*channels)
                    data=stream.getvalue()
                    self.assertTrue(decode(data,len(data),output,8000,ctypes.byref(count)))
                    self.assertEqual(count.value,8000)
                    self.assertTrue(all(v==1234 for v in output))
                    self.assertFalse(decode(data,len(data)-1,output,8000,ctypes.byref(count)))
                    self.assertFalse(decode(data,len(data),output,7999,ctypes.byref(count)))
            placeholder=bytearray(data)
            struct.pack_into('<I',placeholder,4,0xffffffff)
            struct.pack_into('<I',placeholder,40,0xffffffff)
            raw=bytes(placeholder)
            self.assertTrue(decode(raw,len(raw),output,8000,ctypes.byref(count)))
            for size in range(44):
                self.assertFalse(decode(raw,size,output,8000,ctypes.byref(count)))

    def test_worker_http_quota_and_playback(self):
        stubs = {
            'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n',
            'esp_heap_caps.h': '#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t,unsigned);void heap_caps_free(void *);\n',
            'esp_random.h': '#include <stdint.h>\nuint32_t esp_random(void);\n',
            'esp_timer.h': '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
            'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(x) (x)\n#define pdTRUE 1\n#define pdPASS 1\n#define portMAX_DELAY 0xffffffffU\n',
            'freertos/semphr.h': '#pragma once\ntypedef void *SemaphoreHandle_t;\nSemaphoreHandle_t xSemaphoreCreateMutex(void);int xSemaphoreTake(SemaphoreHandle_t,unsigned);void xSemaphoreGive(SemaphoreHandle_t);SemaphoreHandle_t xSemaphoreCreateBinary(void);void vSemaphoreDelete(SemaphoreHandle_t);\n',
            'freertos/task.h': '#pragma once\nvoid vTaskDelay(unsigned);void vTaskDelete(void *);int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);int xTaskCreateWithCaps(void (*)(void *),const char *,unsigned,void *,unsigned,void *,unsigned);\n',
            'nvs.h': '#pragma once\n#include <stdint.h>\ntypedef unsigned nvs_handle_t;\n#define NVS_READWRITE 1\n#define NVS_READONLY 0\nint nvs_open(const char *,int,nvs_handle_t *);int nvs_get_i64(nvs_handle_t,const char *,int64_t *);int nvs_set_i64(nvs_handle_t,const char *,int64_t);int nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);\n',
            'starter_media.h': '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include "esp_err.h"\ntypedef void (*starter_pcm_observer_t)(const int16_t *,size_t,bool,bool,int64_t);\nvoid starter_media_set_pcm_observer(starter_pcm_observer_t);void starter_media_cancel_pcm8k_playback(void);uint32_t starter_media_playback_epoch(void);esp_err_t starter_media_play_pcm8k_at_epoch(const int16_t *,size_t,uint32_t);\n',
        }
        cjson=pathlib.Path('/usr/include/cjson')
        with tempfile.TemporaryDirectory() as temp:
            temp=pathlib.Path(temp)
            for name,body in stubs.items():
                target=temp/name
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_text(body)
            includes=[temp,ROOT/'platforms/esp-idf/components/starter_runtime/include',
                ROOT/'platforms/esp-idf/components/starter_runtime/src',
                ROOT/'platforms/esp-idf/components/platform_client/include',ROOT/'product/include',cjson]
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                *['-I'+str(p) for p in includes],str(ROOT/'tools/tests/fixtures/recognition_worker.c'),
                str(ROOT/'product/src/xiaotai_recognition.c'),'-lcjson','-lm','-o',str(temp/'worker')],check=True)
            for scenario in range(15):
                subprocess.run([str(temp/'worker'),str(scenario)],check=True)

if __name__ == '__main__':
    unittest.main()
