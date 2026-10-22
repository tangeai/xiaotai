"""The playback scope shows signed PCM, expires, and isolates media owners."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c

INCLUDE = ROOT / 'platforms/esp-idf/components/starter_media_common/include'


class PlaybackWaveform(unittest.TestCase):
    def test_pcm_silence_timeout_and_owner_switch(self):
        run_c(f'#include "{INCLUDE / "starter_playback_scope.h"}"\n' + r'''
#include <assert.h>
int main(void) {
    starter_playback_scope_t scope = {.lock = ATOMIC_FLAG_INIT};
    starter_playback_waveform_t view;
    int16_t pcm[320];
    starter_playback_scope_read(&scope, 100, 7, true, &view);
    assert(!view.active);
    for (unsigned i=0;i<320;i++) pcm[i]=i<160 ? 32767 : -32768;
    starter_playback_scope_publish(&scope, pcm, 320, 100, 7);
    starter_playback_scope_read(&scope, 120, 7, true, &view);
    assert(view.active && view.points[0]==16 && view.points[31]==-16);
    starter_playback_scope_read(&scope, 120, 8, true, &view);
    assert(!view.active); /* previous session */
    starter_playback_scope_read(&scope, 120, 7, false, &view);
    assert(!view.active); /* muted, stopped or non-AI owner */
    starter_playback_scope_read(&scope, 251, 7, true, &view);
    assert(!view.active && view.points[0]==0); /* restore normal mouth */
    for (unsigned i=0;i<320;i++) pcm[i]=0;
    starter_playback_scope_publish(&scope, pcm, 320, UINT32_MAX-20, 8);
    starter_playback_scope_read(&scope, 10, 8, true, &view);
    assert(view.active);
    for (unsigned i=0;i<32;i++) assert(view.points[i]==0);
    starter_playback_scope_read(&scope, 200, 8, true, &view);
    assert(!view.active);
    /* Disabling must bypass PCM sampling and hide even a fresh mailbox. */
    atomic_store(&scope.disabled, true);
    starter_playback_scope_publish(&scope, pcm, 320, 20, 8);
    assert(scope.at_ms==UINT32_MAX-20);
    starter_playback_scope_read(&scope, 10, 8, true, &view);
    assert(!view.active);
    atomic_store(&scope.disabled, false);
    atomic_flag_test_and_set(&scope.lock);
    starter_playback_scope_read(&scope, 10, 8, true, &view);
    assert(!view.active); /* UI never waits for the audio worker */
    starter_playback_scope_publish(&scope, pcm, 320, 10, 8);
    atomic_flag_clear(&scope.lock);
}
''')

    def test_dot_mailbox_matches_bk_levels_and_drops_stale_audio(self):
        bk = (ROOT / 'platforms/beken/runtime/src/xiaotai_audio.c').read_text()
        run_c(f'#include "{INCLUDE / "starter_playback_scope.h"}"\n' +
              function(bk, 'playback_columns_pack') + r'''
#include <assert.h>
int main(void) {
    starter_playback_scope_t scope={.lock=ATOMIC_FLAG_INIT};
    starter_playback_waveform_t view;
    int16_t pcm[320];
    for(unsigned level=0;level<8;level++) {
        for(unsigned i=0;i<320;i++) pcm[i]=(int16_t)((i%2?-1:1)*(100U<<level));
        starter_playback_scope_publish_dots(&scope,pcm,320,100,7);
        starter_playback_scope_read(&scope,120,7,true,&view);
        assert(view.active && view.columns==playback_columns_pack(pcm,320));
    }
    for(unsigned i=0;i<320;i++)pcm[i]=-32768;
    starter_playback_scope_publish_dots(&scope,pcm,320,100,7);
    starter_playback_scope_read(&scope,120,7,true,&view);
    assert(view.columns==0x7777777U);
    starter_playback_scope_read(&scope,251,7,true,&view);assert(!view.active && !view.columns);
    starter_playback_scope_read(&scope,120,8,true,&view);assert(!view.active);
    starter_playback_scope_read(&scope,120,7,false,&view);assert(!view.active);
    atomic_store(&scope.disabled,true);
    starter_playback_scope_publish_dots(&scope,pcm,320,200,7);assert(scope.at_ms==100);
    atomic_store(&scope.disabled,false);
    atomic_flag_test_and_set(&scope.lock);
    starter_playback_scope_publish_dots(&scope,pcm,320,200,7);assert(scope.at_ms==100);
    starter_playback_scope_read(&scope,120,7,true,&view);assert(!view.active);
    atomic_flag_clear(&scope.lock);
    memset(pcm,0,sizeof(pcm));
    starter_playback_scope_publish_dots(&scope,pcm,1,UINT32_MAX-20,8);
    starter_playback_scope_read(&scope,10,8,true,&view);assert(view.active && !view.columns);
}
''')

    def test_public_media_snapshot_gates_mute_owner_and_generation(self):
        for board in ('esp32s3/lckfb-esp32s3-xiaotai', 'esp32p4/waveshare-touch-lcd-43c'):
            source = (ROOT / 'firmware/esp-idf' / board / 'components/starter_media/src/starter_media.c').read_text()
            run_c(f'#include "{INCLUDE / "starter_playback_scope.h"}"\n' + r'''
#include <assert.h>
#define STARTER_TIRTC_AI 2
static starter_playback_scope_t s_playback_scope={.lock=ATOMIC_FLAG_INIT};
static atomic_bool s_active,s_speaker_muted;
static atomic_uint s_mode,s_generation,s_speaker_volume;
static int64_t clock_us=100000;
static int64_t esp_timer_get_time(void){return clock_us;}
''' + function(source, 'starter_media_playback_waveform') + r'''
int main(void){
    starter_playback_waveform_t out;
    int16_t pcm[32];for(unsigned i=0;i<32;i++)pcm[i]=16384;
    s_active=true;s_mode=STARTER_TIRTC_AI;s_generation=7;s_speaker_volume=5;
    starter_playback_scope_publish(&s_playback_scope,pcm,32,100,7);
    starter_media_playback_waveform(&out);assert(out.active && out.points[0]==8);
    s_speaker_muted=true;starter_media_playback_waveform(&out);assert(!out.active);
    s_speaker_muted=false;s_speaker_volume=0;
    starter_media_playback_waveform(&out);assert(!out.active);
    s_speaker_volume=5;s_mode=1;
    starter_media_playback_waveform(&out);assert(!out.active);
    s_mode=2;s_generation=8;
    starter_media_playback_waveform(&out);assert(!out.active);
    s_generation=7;s_active=false;
    starter_media_playback_waveform(&out);assert(!out.active);
    s_active=true;clock_us=251000;
    starter_media_playback_waveform(&out);assert(!out.active);
    starter_media_playback_waveform(NULL);
}
''')

    def test_console_switch_uses_public_media_control(self):
        console = (ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
        source = (ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c').read_text()
        run_c(f'#include "{INCLUDE / "starter_playback_scope.h"}"\n' + r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
static starter_playback_scope_t s_playback_scope={.lock=ATOMIC_FLAG_INIT};
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
typedef struct {int state;} starter_runtime_status_t;
typedef struct {int ai_phase;bool emotion_tag_present;char emotion[16];bool valid;} starter_runtime_face_snapshot_t;
static starter_runtime_status_t runtime;
static starter_runtime_face_snapshot_t product={.valid=true};
static unsigned snapshot_reads;
static starter_runtime_status_t starter_runtime_status_snapshot(void){snapshot_reads++;return runtime;}
static starter_runtime_face_snapshot_t starter_runtime_face_snapshot(void){return product;}
static void starter_media_playback_waveform(starter_playback_waveform_t *out){
    starter_playback_scope_read(&s_playback_scope,100,1,runtime.state==STARTER_RUNTIME_AI_ACTIVE,out);
}
static char output[2048];
static int capture(const char *format,...){
    va_list args;va_start(args,format);
    int n=vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,args);
    va_end(args);return n;
}
#define printf capture
''' + function(source, 'starter_media_set_face_animation_enabled')
              + function(source, 'starter_media_face_animation_enabled')
              + function(console, 'command_face_animation') + r'''
int main(void){
    char *off[]={"face-animation","off"};
    char *on[]={"face-animation","on"};
    char *bad[]={"face-animation","invalid"};
    assert(starter_media_face_animation_enabled());
    assert(command_face_animation(2,off)==0);
    assert(!starter_media_face_animation_enabled());
    assert(command_face_animation(2,bad)!=0);
    assert(!starter_media_face_animation_enabled());
    assert(command_face_animation(2,on)==0);
    assert(starter_media_face_animation_enabled());
    assert(command_face_animation(1,on)==0);
    assert(command_face_animation(3,on)!=0);
    // Query is read-only and exposes the actual live inputs, even with a TAG.
    runtime.state=STARTER_RUNTIME_AI_ACTIVE;
    product.ai_phase=3;product.emotion_tag_present=true;strcpy(product.emotion,"happy");
    int16_t pcm[320];for(unsigned i=0;i<320;i++)pcm[i]=32767;
    starter_playback_scope_publish_dots(&s_playback_scope,pcm,320,100,1);
    output[0]=0;snapshot_reads=0;
    assert(command_face_animation(1,on)==0 && starter_media_face_animation_enabled());
    assert(strstr(output,"ai=1 phase=3 tag=1 emotion=happy pcm=1 columns=7777777"));
    assert(snapshot_reads==1 && s_playback_scope.valid && s_playback_scope.at_ms==100);
    product.emotion_tag_present=false;
    output[0]=0;assert(command_face_animation(1,on)==0);
    assert(strstr(output,"tag=0 emotion=happy pcm=1 columns=7777777"));
    runtime.state=0;output[0]=0;
    assert(command_face_animation(1,on)==0);
    assert(strstr(output,"ai=0") && strstr(output,"pcm=0 columns=0000000"));
    runtime.state=STARTER_RUNTIME_AI_ACTIVE;output[0]=0;
    assert(command_face_animation(2,off)==0);
    assert(strstr(output,"Face animation: off") && strstr(output,"pcm=0 columns=0000000"));
    unsigned reads=snapshot_reads;output[0]=0;
    assert(command_face_animation(2,bad)!=0 && snapshot_reads==reads);
    product.valid=false;output[0]=0;
    assert(command_face_animation(1,on)==0);
    assert(strstr(output,"snapshot=unavailable") && !strstr(output,"tag="));
}
''')

    def test_face_snapshot_reports_lock_failure_without_copying_large_product_view(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        header = (ROOT / 'platforms/esp-idf/components/starter_runtime/include/starter_runtime.h').read_text()
        end = header.index('} starter_runtime_face_snapshot_t;') + len('} starter_runtime_face_snapshot_t;')
        start = header.rfind('typedef struct {', 0, end)
        run_c(r'''
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef int starter_ai_ui_phase_t;
''' + header[start:end] + r'''
static struct {int ai_phase;bool emotion_tag_present;char emotion[16];char unused_contacts[8192];} s_product_snapshot;
static void *s_product_mutex;
static bool lock_ok;
static unsigned takes,gives;
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static int xSemaphoreTake(void *mutex,unsigned wait){takes++;return lock_ok;}
static void xSemaphoreGive(void *mutex){gives++;}
''' + function(source, 'starter_runtime_face_snapshot') + r'''
int main(void){
    starter_runtime_face_snapshot_t out=starter_runtime_face_snapshot();
    assert(!out.valid && takes==0 && gives==0);
    s_product_mutex=(void*)1;
    out=starter_runtime_face_snapshot();assert(!out.valid && takes==1 && gives==0);
    lock_ok=true;s_product_snapshot.ai_phase=3;s_product_snapshot.emotion_tag_present=true;
    strcpy(s_product_snapshot.emotion,"happy");
    out=starter_runtime_face_snapshot();
    assert(out.valid && out.ai_phase==3 && out.emotion_tag_present && !strcmp(out.emotion,"happy"));
    assert(gives==1 && sizeof(out)<=32);
    out.emotion[0]='x';assert(s_product_snapshot.emotion[0]=='h');
    lock_ok=false;out=starter_runtime_face_snapshot();
    assert(!out.valid && !out.emotion_tag_present && !out.emotion[0] && gives==1);
}
''')

    def test_ai_tag_wins_over_local_expression_selection(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        start = source.index('    const char *emotion = product.emotion;')
        end = source.index('    if (session_idle && s_ambient_visible)', start)
        run_c(r'''
#include <stdbool.h>
#include <string.h>
#include <assert.h>
static struct {char emotion[16];bool emotion_tag_present;} product;
static bool s_prefer_local_expression;
static struct {unsigned emoji_index;} s_preferences;
static const char *s_emoji_keys[]={"happy"};
static const char *select_expression(bool ai_now){
''' + source[start:end] + r'''
    return emotion;
}
int main(void){
    strcpy(product.emotion,"sad");product.emotion_tag_present=true;s_prefer_local_expression=true;
    assert(!strcmp(select_expression(true),"sad"));
    strcpy(product.emotion,"angry");
    assert(!strcmp(select_expression(true),"angry"));
    assert(!strcmp(select_expression(false),"happy"));
    product.emotion_tag_present=false;
    assert(!strcmp(select_expression(true),"happy"));
    product.emotion[0]=0;
    assert(!strcmp(select_expression(true),"happy"));
    strcpy(product.emotion,"loving");product.emotion_tag_present=true;s_prefer_local_expression=false;
    assert(!strcmp(select_expression(true),"loving"));
}
''')

    def test_animation_budget_drops_frames_and_recovers(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        end = source.index('} expression_animation_budget_t;') + len('} expression_animation_budget_t;')
        start = source.rfind('typedef struct {', 0, end)
        run_c(r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
''' + source[start:end] + function(source, 'expression_animation_due') + function(source, 'expression_animation_record') + r'''
int main(void){
    expression_animation_budget_t b={0};
    assert(expression_animation_due(&b,0,false));
    expression_animation_record(&b,0,1000);
    assert(!expression_animation_due(&b,99,false));
    assert(expression_animation_due(&b,100,false));
    expression_animation_record(&b,100,12000);
    assert(!expression_animation_due(&b,200,false));
    assert(expression_animation_due(&b,200,true)); /* TAG / face / stop never delayed */
    assert(expression_animation_due(&b,300,false));
    expression_animation_record(&b,300,1000);
    assert(!expression_animation_due(&b,400,false));
    assert(expression_animation_due(&b,3100,false));
    expression_animation_record(&b,3100,1000);
    assert(expression_animation_due(&b,3200,false));
    b=(expression_animation_budget_t){0};
    expression_animation_record(&b,UINT32_MAX-50,11000);
    assert(!expression_animation_due(&b,49,false));
    assert(expression_animation_due(&b,149,false));
}
''')

    def test_home_keeps_emotion_eyes_and_restores_mouth(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        end = source.index('} expression_geometry_t;') + len('} expression_geometry_t;')
        start = source.rfind('typedef struct {', 0, end)
        run_c(f'#include "{INCLUDE / "starter_playback_waveform.h"}"\n' + r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef int lv_coord_t;
typedef int starter_ai_ui_phase_t;
#define STARTER_AI_UI_SPEAKING 3
#define LV_RADIUS_CIRCLE 1000
#define LV_OPA_COVER 255
#define HOME_BACKGROUND_COLOR 0
#define FACE_CANVAS_W 220
#define FACE_CANVAS_H 108
#define PAGE_HOME_FACE 0
int s_page=PAGE_HOME_FACE;
void *s_face=(void *)1, *s_face_canvas_buffer=(void *)1;
char s_rendered_expression[16];
starter_playback_waveform_t s_playback_waveform;
static bool s_face_default_ai;
static bool s_face_tag_active;
typedef unsigned lv_color_t;
typedef struct {unsigned bg_color;int bg_opa,radius;} lv_draw_rect_dsc_t;
typedef struct {unsigned color;int width,opa,round_start,round_end;} lv_draw_line_dsc_t;
typedef struct {int x,y;} lv_point_t;
static unsigned rects,lines,points,eye_draws,full_invalidations,area_invalidations;
static bool invalidation_enabled=true;
typedef int lv_disp_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
static lv_area_t last_area;
static unsigned face_zoom=512;
static lv_disp_t *lv_obj_get_disp(void *c){return (lv_disp_t *)1;}
static bool lv_disp_is_invalidation_enabled(lv_disp_t *d){return invalidation_enabled;}
static void lv_disp_enable_invalidation(lv_disp_t *d,bool enabled){invalidation_enabled=enabled;}
static void lv_obj_get_coords(void *c,lv_area_t *a){*a=(lv_area_t){10,20,229,127};}
static unsigned lv_img_get_zoom(void *c){return face_zoom;}
static void lv_obj_invalidate(void *c){if(invalidation_enabled)full_invalidations++;}
static void lv_obj_invalidate_area(void *c,const lv_area_t *a){if(invalidation_enabled){area_invalidations++;last_area=*a;}}
static unsigned eye_color;
static int eye_h, eye_center;
static void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d){memset(d,0,sizeof(*d));}
static void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d){memset(d,0,sizeof(*d));}
static unsigned lv_color_hex(unsigned c){return c;}
static void lv_canvas_draw_rect(void *c,int x,int y,int w,int h,const lv_draw_rect_dsc_t *d){
    assert(x>=0 && y>=0 && x+w<=220 && y+h<=108);rects++;lv_obj_invalidate(c);
    if(y<60 && d->bg_color){eye_draws++;eye_color=d->bg_color;eye_h=h;eye_center=y+h/2;}
}
static void lv_canvas_draw_line(void *c,const lv_point_t *p,unsigned n,const lv_draw_line_dsc_t *d){
    lines++;points=n;lv_obj_invalidate(c);
    for(unsigned i=0;i<n;i++)assert(p[i].x>=70 && p[i].x<=150 && p[i].y>=76 && p[i].y<=108);
}

''' + source[start:end] + function(source, 'clear_expression_part') + function(source, 'apply_expression') + r'''
int main(void){
    apply_expression("sad",0,0);
    assert(rects==3 && lines==0 && eye_color==0x9FB2E8);
    strcpy(s_rendered_expression,"sad");
    int tag_eye_h=eye_h;
    s_face_default_ai=true;
    s_playback_waveform.active=true;
    for(unsigned i=0;i<32;i++)s_playback_waveform.points[i]=i%2 ? 16 : -16;
    apply_expression("sad",3,0);
    assert(lines==1 && points==32 && eye_color==0x9FB2E8);
    assert(eye_h==6); /* fixed squint for untagged AI */
    unsigned eyes_before=eye_draws;
    unsigned full_before=full_invalidations;
    unsigned rects_before=rects;
    s_playback_waveform.points[0]=0;
    apply_expression("sad",3,0);
    assert(eye_draws==eyes_before); /* waveform never redraws eyes */
    assert(rects==rects_before+1); /* only clear old mouth */
    assert(full_invalidations==full_before && area_invalidations>0);
    assert(last_area.y1>=20+76*2 && last_area.y2<=20+108*2+2);
    assert(last_area.x1>=10+66*2 && last_area.x2<=10+154*2+2);
    unsigned areas_before=area_invalidations;
    apply_expression("sad",3,0);
    assert(area_invalidations==areas_before && eye_draws==eyes_before); /* unchanged frame skipped */
    invalidation_enabled=false;s_playback_waveform.points[0]=1;
    apply_expression("sad",3,0);
    assert(!invalidation_enabled && area_invalidations==areas_before); /* preserve outer suppression */
    invalidation_enabled=true;
    unsigned frozen_eyes=eye_draws;
    for(unsigned i=0;i<30;i++){
        s_playback_waveform.points[0]=(int8_t)(i%16);
        apply_expression("sad",3,(uint8_t)(i%4));
        assert(eye_h==6 && eye_draws==frozen_eyes);
    }
    s_face_tag_active=true;
    unsigned lines_before=lines;
    s_playback_waveform.points[0]=0;
    apply_expression("sad",3,0);
    assert(lines==lines_before && eye_h==tag_eye_h); /* explicit TAG owns the whole face */
    s_playback_waveform.points[0]=9;
    apply_expression("sad",3,0);
    assert(lines==lines_before && eye_h==tag_eye_h); /* PCM changes cannot override TAG */
    s_face_tag_active=false;
    s_playback_waveform.active=false;
    apply_expression("sad",0,0);
    assert(eye_h==6); /* no TAG restores fixed eyes, even between replies */
    s_playback_waveform.active=true;
    apply_expression("sad",0,0);
    unsigned restore_before=rects;
    unsigned restore_eyes=eye_draws;
    s_playback_waveform.active=false;
    apply_expression("sad",0,0);
    assert(rects==restore_before+2 && eye_draws==restore_eyes); /* clear scope then restore mouth */
    lines_before=lines;
    s_playback_waveform.active=true;s_page=1;
    apply_expression("happy",0,0);
    assert(lines==lines_before && eye_h==22); /* preview inherits neither playback nor blinking */
}
''')

    def test_s3_dot_mouth_uses_pixels_and_freezes_eyes(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        end = source.index('} expression_geometry_t;') + len('} expression_geometry_t;')
        start = source.rfind('typedef struct {', 0, end)
        input_start = source.index('    starter_media_playback_waveform(&s_playback_waveform);')
        input_end = source.index('    const char *emotion = product.emotion;', input_start)
        run_c(f'#include "{INCLUDE / "starter_playback_waveform.h"}"\n' + r'''
#define CONFIG_IDF_TARGET_ESP32S3 1
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef int lv_coord_t;
typedef int starter_ai_ui_phase_t;
#define STARTER_AI_UI_SPEAKING 3
#define LV_RADIUS_CIRCLE 1000
#define LV_OPA_COVER 255
#define HOME_BACKGROUND_COLOR 0
#define FACE_CANVAS_W 220
#define FACE_CANVAS_H 108
#define PAGE_HOME_FACE 0
int s_page=PAGE_HOME_FACE;
void *s_face=(void *)1;
static unsigned canvas[220*108];
static unsigned *s_face_canvas_buffer=canvas;
char s_rendered_expression[16];
starter_playback_waveform_t s_playback_waveform;
static bool s_face_default_ai;
static bool s_face_tag_active;
static starter_playback_waveform_t live_pcm;
static void starter_media_playback_waveform(starter_playback_waveform_t *out){*out=live_pcm;}
static void tick_inputs(bool ai_now,bool tag){
    struct {bool emotion_tag_present;} product={tag};
''' + source[input_start:input_end] + r'''
}
typedef unsigned lv_color_t;
typedef struct {unsigned bg_color;int bg_opa,radius;} lv_draw_rect_dsc_t;
typedef struct {unsigned color;int width,opa,round_start,round_end;} lv_draw_line_dsc_t;
typedef struct {int x,y;} lv_point_t;
static unsigned rects,lines,points,eye_draws,full_invalidations,area_invalidations;
static bool invalidation_enabled=true;
typedef int lv_disp_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
static lv_area_t last_area;
static unsigned face_zoom=512;
static lv_disp_t *lv_obj_get_disp(void *c){return (lv_disp_t *)1;}
static bool lv_disp_is_invalidation_enabled(lv_disp_t *d){return invalidation_enabled;}
static void lv_disp_enable_invalidation(lv_disp_t *d,bool enabled){invalidation_enabled=enabled;}
static void lv_obj_get_coords(void *c,lv_area_t *a){*a=(lv_area_t){10,20,229,127};}
static unsigned lv_img_get_zoom(void *c){return face_zoom;}
static void lv_obj_invalidate(void *c){if(invalidation_enabled)full_invalidations++;}
static void lv_obj_invalidate_area(void *c,const lv_area_t *a){if(invalidation_enabled){area_invalidations++;last_area=*a;}}
static unsigned eye_color;
static int eye_h, eye_center;
static void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d){memset(d,0,sizeof(*d));}
static void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d){memset(d,0,sizeof(*d));}
static unsigned lv_color_hex(unsigned c){return c;}
static void lv_canvas_draw_rect(void *c,int x,int y,int w,int h,const lv_draw_rect_dsc_t *d){
    assert(x>=0 && y>=0 && x+w<=220 && y+h<=108);rects++;lv_obj_invalidate(c);
    if(y<60 && d->bg_color){eye_draws++;eye_color=d->bg_color;eye_h=h;eye_center=y+h/2;}
}
static void lv_canvas_draw_line(void *c,const lv_point_t *p,unsigned n,const lv_draw_line_dsc_t *d){
    lines++;points=n;lv_obj_invalidate(c);
    for(unsigned i=0;i<n;i++)assert(p[i].x>=70 && p[i].x<=150 && p[i].y>=76 && p[i].y<=108);
}

''' + source[start:end] + function(source, 'clear_expression_part') + (function(source, 'draw_dot_mouth') if 'static void draw_dot_mouth(' in source else '') + function(source, 'apply_expression') + r'''
int main(void){
    s_face_default_ai=true;
    s_playback_waveform.active=true;
    apply_expression("happy",3,3);
    assert(lines==0); /* S3 must not draw a connected waveform. */
    assert(eye_h==6);
    strcpy(s_rendered_expression,"happy");
    unsigned eyes_before=eye_draws,full_before=full_invalidations;
    unsigned rects_before=rects;
    unsigned before[220*108];
    memcpy(before,canvas,sizeof(before));
    /* Seven columns, three amplitude bands, written into the native canvas. */
    s_playback_waveform.columns=0x7654321;
    apply_expression("happy",3,1);
    assert(lines==0 && rects==rects_before && eye_draws==eyes_before);
    assert(full_invalidations==full_before && area_invalidations>0);
    assert(last_area.y1>=20+68*2 && last_area.y2<=20+108*2);
    for(unsigned y=0;y<108;y++)for(unsigned x=0;x<220;x++)
        if(x<77 || x>=143 || y<70 || y>=106)
            assert(canvas[y*220+x]==before[y*220+x]);
    assert(canvas[102*220+83]==0x55B8C4);
    assert(canvas[87*220+137]==0x72C79B);
    assert(canvas[72*220+137]==0xD8B66A);
    assert(canvas[102*220+87]==HOME_BACKGROUND_COLOR); /* dot gap */
    unsigned areas_before=area_invalidations;
    apply_expression("happy",3,3);
    assert(area_invalidations==areas_before && eye_draws==eyes_before);
    /* PCM phase/zero-crossings and mic activity never animate the eyes. */
    s_playback_waveform.points[0]=16;
    apply_expression("happy",1,0);
    assert(area_invalidations==areas_before && eye_draws==eyes_before);
    s_playback_waveform.active=false;
    apply_expression("happy",0,0);
    assert(eye_draws==eyes_before && canvas[72*220+137]==HOME_BACKGROUND_COLOR);
    assert(canvas[102*220+137]==0x55B8C4); /* stopped -> static dots */
    s_face_tag_active=true;
    unsigned lines_before=lines;
    apply_expression("sad",3,2);
    assert(eye_color==0x9FB2E8 && eye_h!=6 && lines==lines_before);
    unsigned tag_eyes=eye_draws;
    apply_expression("sad",3,3);
    assert(eye_draws==tag_eyes); /* TAG face ignores audio level */
    // Explicit happy TAG owns the full face, even while music PCM changes.
    live_pcm=(starter_playback_waveform_t){.active=true,.columns=0x6566566};
    tick_inputs(true,true);
    assert(!s_playback_waveform.active && s_face_tag_active && !s_face_default_ai);
    apply_expression("happy",3,0);
    assert(eye_h==22 && lines==0);
    unsigned happy_eyes=eye_draws,tag_areas=area_invalidations;
    memcpy(before,canvas,sizeof(before));
    live_pcm.columns=0x1234321;tick_inputs(true,true);
    apply_expression("happy",3,0);
    assert(area_invalidations==tag_areas && eye_draws==happy_eyes);
    assert(memcmp(before,canvas,sizeof(before))==0);
    // Same happy fallback without a server TAG: squint and show actual PCM.
    tick_inputs(true,false);
    assert(s_playback_waveform.active && !s_face_tag_active && s_face_default_ai);
    apply_expression("happy",3,0);
    assert(eye_h==6 && lines==0);
    unsigned default_eyes=eye_draws,default_areas=area_invalidations;
    memcpy(before,canvas,sizeof(before));
    live_pcm.columns=0x6566566;tick_inputs(true,false);
    apply_expression("happy",3,0);
    assert(area_invalidations==default_areas+1 && eye_draws==default_eyes);
    assert(memcmp(before,canvas,sizeof(before))!=0);
    for(unsigned y=0;y<108;y++)for(unsigned x=0;x<220;x++)
        if(x<77 || x>=143 || y<70 || y>=106)
            assert(canvas[y*220+x]==before[y*220+x]);
    live_pcm.active=false;tick_inputs(true,false);
    apply_expression("happy",3,0);
    assert(eye_draws==default_eyes && canvas[72*220+137]==HOME_BACKGROUND_COLOR);
    assert(canvas[102*220+137]==0x55B8C4); /* stopped -> static base */
    s_face_tag_active=false;
    s_face_default_ai=true;
    apply_expression("happy",0,0);
    assert(eye_h==6);
    s_page=1;
    apply_expression("happy",0,0);
    assert(eye_h==22 && lines==0); /* preview has no playback mouth */
}
''')


if __name__ == '__main__':
    unittest.main()
