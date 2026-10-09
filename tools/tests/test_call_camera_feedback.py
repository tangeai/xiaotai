"""Camera-control acknowledgements must refresh the active call page."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

UI = ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c'

class CallCameraFeedback(unittest.TestCase):
    def test_runtime_camera_changes_refresh_without_changing_call_state(self):
        source = UI.read_text()
        start = source.index('        } else if (runtime_changed', source.index('    if (call_now) {'))
        end = source.index('        /*', start)
        branch = source[start + len('        } else '):end].rstrip()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#define CONFIG_IDF_TARGET_ESP32P4 1
static bool runtime_changed,s_previous_call_camera_enabled=true,s_previous_call_microphone_muted;
static struct {bool call_camera_enabled,call_microphone_muted;} product={true,false};
static int runtime,refreshes;
static bool shown_camera;
static void refresh_call_controls(int r,void *p){refreshes++;shown_camera=product.call_camera_enabled;}
static void tick(void){
''' + branch + r'''
}
int main(void){
 (void)s_previous_call_camera_enabled;
 tick();assert(refreshes==0);
 product.call_camera_enabled=false;tick();assert(refreshes==1 && !shown_camera);
 s_previous_call_camera_enabled=false;tick();assert(refreshes==1);
 product.call_camera_enabled=true;tick();assert(refreshes==2 && shown_camera);
 s_previous_call_camera_enabled=true;tick();assert(refreshes==2);
 product.call_microphone_muted=true;tick();assert(refreshes==3);
 s_previous_call_microphone_muted=true;tick();assert(refreshes==3);
 runtime_changed=true;tick();assert(refreshes==4);
}
''')

    def test_call_buttons_name_the_current_camera_and_microphone_state(self):
        controls = function(UI.read_text(), 'refresh_call_controls')
        controls = controls[controls.index('    if (active && product->call_video)'): -1]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#define CONFIG_IDF_TARGET_ESP32P4 1
static int s_call_camera_button=1,s_call_mute_button=2,s_call_hangup_button=3;
static char camera[64],mic[64];
static struct {bool microphone_muted;} s_preferences;
typedef struct {bool call_video,call_camera_enabled,call_microphone_muted;} snapshot_t;
static void set_button_text(int button,const char *text){
 if(button==1)strcpy(camera,text);
 if(button==2)strcpy(mic,text);
}
static void lv_obj_set_pos(int o,int x,int y){}
static void lv_obj_set_size(int o,int w,int h){}
static void render(snapshot_t *product){bool active=true,incoming=false;
#if CONFIG_IDF_TARGET_ESP32P4
''' + controls + r'''
}
int main(void){
 snapshot_t view={.call_video=true,.call_camera_enabled=true};
 render(&view);assert(strcmp(camera,"摄像头已开")==0 && strcmp(mic,"麦克风已开")==0);
 view.call_camera_enabled=false;view.call_microphone_muted=true;
 render(&view);assert(strcmp(camera,"摄像头已关")==0 && strcmp(mic,"麦克风已关")==0);
 view.call_video=false;view.call_microphone_muted=false;s_preferences.microphone_muted=true;
 render(&view);assert(strcmp(mic,"麦克风已开")==0);
}
''')

    def test_rotation_hint_displays_degrees_without_missing_symbol(self):
        source = UI.read_text()
        start = source.index('action == ACTION_CALL_ROTATE)')
        end = source.index('\n#endif',start)
        branch = source[start:end]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define ACTION_CALL_ROTATE 1
#define STARTER_RUNTIME_CALL_ACTIVE 6
#define PRODUCT_CALL_ROTATION_HINT_MS 1800
static uint16_t s_call_rotation_hint, angle;
static int64_t s_call_rotation_hint_hide_ms;
static int s_call_rotation_hint_label,error;
static char shown[64];
static bool visible;
typedef int esp_err_t;
typedef struct {int state;} starter_runtime_status_t;
typedef struct {bool call_video,call_wechat;} starter_runtime_product_snapshot_t;
static starter_runtime_status_t starter_runtime_status(void){return (starter_runtime_status_t){6};}
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){return (starter_runtime_product_snapshot_t){true,false};}
static int p4_video_rotate_remote_clockwise(uint16_t *rotation){if(error)return error;angle=(angle+90)%360;*rotation=angle;return 0;}
static int64_t monotonic_ms(void){return 100;}
static void label_set_text_if_changed(int label,const char *text){snprintf(shown,sizeof(shown),"%s",text);}
static void set_object_visible(int label,bool value){visible=value;}
static void act(int action){if (
''' + branch + r'''
}}
int main(void){
 const char *expected[]={"已旋转 90°","已旋转 180°","已旋转 270°","已旋转 0°"};
 for(int i=0;i<4;i++){
  act(ACTION_CALL_ROTATE);assert(strcmp(shown,expected[i])==0 && visible);
  assert(s_call_rotation_hint==angle && s_call_rotation_hint_hide_ms==1900);
 }
 error=-1;act(ACTION_CALL_ROTATE);assert(strcmp(shown,expected[3])==0);
}
''')

if __name__ == '__main__':
    unittest.main()
