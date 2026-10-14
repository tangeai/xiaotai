"""BK AI face policy and PCM driven dot columns through production interfaces."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c

UI_INCLUDE = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/include'
AUDIO = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_audio.c'
BOARD = ROOT / 'boards/lckfb/bk7258/board_display.c'
UI = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_ui.c'


class BKFaceAnimation(unittest.TestCase):
    def test_explicit_ai_tag_wins_and_default_speaking_uses_dots(self):
        run_c(f'#include "{UI_INCLUDE / "xiaotai_ai_face_policy.h"}"\n' + r'''
#include <assert.h>
int main(void) {
    assert(xiaotai_ai_face_mode(XIAOTAI_AI_UI_SPEAKING, true) ==
           XIAOTAI_AI_FACE_TAG);
    assert(xiaotai_ai_face_mode(XIAOTAI_AI_UI_LISTENING, true) ==
           XIAOTAI_AI_FACE_TAG);
    assert(xiaotai_ai_face_mode(XIAOTAI_AI_UI_SPEAKING, false) ==
           XIAOTAI_AI_FACE_DOTS);
    assert(xiaotai_ai_face_mode(XIAOTAI_AI_UI_LISTENING, false) ==
           XIAOTAI_AI_FACE_DEFAULT);
}
''', include_dirs=(ROOT / 'product/include',))

    def test_played_pcm_sets_bounded_columns_and_silence_clears_them(self):
        source = AUDIO.read_text()
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
''' + function(source, 'playback_columns_pack') + r'''
int main(void) {
    int16_t samples[70] = {0};
    assert(playback_columns_pack(samples, 70) == 0);
    for (unsigned i = 0; i < 10; ++i) samples[i] = 8192;
    uint32_t columns = playback_columns_pack(samples, 70);
    assert((columns & 15U) > 0U);
    for (unsigned i = 1; i < 7; ++i)
        assert(((columns >> (i * 4U)) & 15U) == 0U);
    assert(playback_columns_pack(NULL, 70) == 0U);
    assert(playback_columns_pack(samples, 0) == 0U);
    for (unsigned i = 0; i < 10; ++i) samples[i] = 512;
    assert((playback_columns_pack(samples, 70) & 15U) > 0U);
}
''')

    def test_playback_snapshot_expires_and_respects_mute(self):
        source = AUDIO.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
static atomic_bool s_running=true,s_speaker_muted;
static atomic_uint s_volume=7,s_playback_columns,s_playback_columns_at_ms;
static uint32_t now_ms;
static uint32_t rtos_get_time(void){return now_ms;}
''' + function(source, 'xiaotai_audio_playback_columns') + r'''
int main(void){
    s_playback_columns=0x7654321U;
    s_playback_columns_at_ms=90U;now_ms=100U;
    assert(xiaotai_audio_playback_columns()==0x7654321U);
    now_ms=291U;assert(xiaotai_audio_playback_columns()==0U);
    now_ms=100U;s_speaker_muted=true;
    assert(xiaotai_audio_playback_columns()==0U);
    s_speaker_muted=false;s_volume=0U;
    assert(xiaotai_audio_playback_columns()==0U);
    s_volume=7U;s_running=false;
    assert(xiaotai_audio_playback_columns()==0U);
    s_running=true;s_playback_columns_at_ms=UINT32_MAX-10U;now_ms=9U;
    assert(xiaotai_audio_playback_columns()==0x7654321U);
}
''')

    def test_full_and_mouth_writes_are_serial_and_restore_full_window(self):
        source = BOARD.read_text()
        start = source.index('typedef struct {')
        end = source.index('} display_message_t;', start) + len('} display_message_t;')
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define BK_OK 0
#define BK_ERR_PARAM -1
#define BK_ERR_BUSY -2
#define BK_ERR_NOT_INIT -3
#define BK_ERR_NO_MEM -4
#define CONFIG_LCD_SPI_REFRESH_WITH_QSPI_MAPPING_MODE 1
#define XIAOTAI_BOARD_DISPLAY_WIDTH 320U
#define XIAOTAI_BOARD_DISPLAY_HEIGHT 240U
#define XIAOTAI_LCD_SPI_ID 1
#define BK_LOGE(...) ((void)0)
typedef int bk_err_t;
typedef int beken_mutex_t;
typedef struct { uint16_t x_start,y_start,x_end,y_end; } lcd_display_area_t;
typedef struct { uint8_t *frame; size_t size; } frame_buffer_t;
''' + source[start:end] + r'''
static atomic_bool s_ready=true;
static beken_mutex_t s_display_mutex;
static int s_display_queue;
static atomic_uint s_display_generation;
static display_message_t queue[8];
static unsigned queue_count, queue_head;
static lcd_display_area_t last_area;
static int writes, waits, releases, region_frees, locked, wait_fail;
static bool queue_accept=true;
static unsigned generation_on_failed_push;
static void rtos_lock_mutex(beken_mutex_t *mutex) {(void)mutex;assert(!locked);locked=1;}
static void rtos_unlock_mutex(beken_mutex_t *mutex) {(void)mutex;assert(locked);locked=0;}
static int rtos_push_to_queue(int *queue_handle,const void *message,int wait){
    (void)queue_handle;(void)wait;assert(locked);
    if(!queue_accept){
        assert(atomic_load(&s_display_generation)==generation_on_failed_push);
        return BK_ERR_BUSY;
    }
    if(queue_count==8)return BK_ERR_BUSY;
    queue[queue_count++]=*(const display_message_t *)message;return BK_OK;
}
static frame_buffer_t *frame_buffer_display_malloc(size_t size){
    frame_buffer_t *frame=malloc(sizeof(*frame));assert(frame);
    frame->frame=malloc(size);assert(frame->frame);frame->size=size;return frame;
}
static void frame_buffer_display_free(frame_buffer_t *frame){
    region_frees++;free(frame->frame);free(frame);
}
static int bk_lcd_spi_partial_display(int id,lcd_display_area_t *area,uint8_t *pixels){
    (void)id;assert(!locked);assert(pixels[0]==0xa5);
    last_area=*area;writes++;return BK_OK;
}
static int bk_lcd_spi_wait_display_complete(int id){
    (void)id;waits++;return wait_fail ? -9 : BK_OK;
}
static int release(void *frame){(void)frame;releases++;return BK_OK;}
''' + function(source, 'display_process_message')
    + function(source, 'release_region')
    + function(source, 'xiaotai_board_display_ready')
    + function(source, 'xiaotai_board_display_flush_tagged')
    + function(source, 'xiaotai_board_display_flush')
    + function(source, 'xiaotai_board_display_flush_region')
    + function(source, 'xiaotai_board_display_generation') + r'''
int main(void){
    uint8_t pixels[320*240*2];
    uint8_t mouth_source[66*36*2];
    memset(pixels,0xa5,sizeof(pixels));
    memset(mouth_source,0xa5,sizeof(mouth_source));
    frame_buffer_t frame={pixels,sizeof(pixels)};
    uint32_t face_generation=0;
    assert(xiaotai_board_display_flush_tagged(&frame,release,&face_generation)==BK_OK);
    assert(writes==0 && waits==0 && releases==0); /* callback never waits */
    assert(queue_count==1 && xiaotai_board_display_generation()==1);
    assert(face_generation==1);
    assert(xiaotai_board_display_flush_region(127,116,66,36,mouth_source,1)==BK_OK);
    memset(mouth_source,0,sizeof(mouth_source)); /* queued region owns a copy */
    assert(queue_count==2 && writes==0);
    display_process_message(&queue[queue_head++]);
    assert(last_area.x_start==0 && last_area.y_start==0);
    assert(last_area.x_end==319 && last_area.y_end==239);
    assert(releases==1 && waits==1);
    display_process_message(&queue[queue_head++]);
    assert(last_area.x_start==127 && last_area.y_start==116);
    assert(last_area.x_end==192 && last_area.y_end==151);
    assert(writes==2 && waits==2 && region_frees==1);
    assert(xiaotai_board_display_flush_region(127,116,66,36,pixels,0)==BK_ERR_BUSY);
    assert(xiaotai_board_display_flush_region(319,239,2,2,pixels,1)==BK_ERR_PARAM);
    assert(xiaotai_board_display_flush(&frame,release)==BK_OK);
    assert(queue_count==3 && writes==2 && releases==1);
    assert(face_generation==1 && xiaotai_board_display_generation()==2);
    display_process_message(&queue[queue_head++]);
    assert(last_area.x_start==0 && last_area.y_start==0);
    assert(last_area.x_end==319 && last_area.y_end==239);
    assert(xiaotai_board_display_generation()==2 && releases==2);
    assert(xiaotai_board_display_flush_region(127,116,66,36,pixels,2)==BK_OK);
    assert(xiaotai_board_display_flush(&frame,release)==BK_OK); /* newer page */
    display_process_message(&queue[queue_head++]); /* stale mouth skipped */
    assert(writes==3 && region_frees==3);
    display_process_message(&queue[queue_head++]);
    assert(writes==4 && releases==3);
    generation_on_failed_push=xiaotai_board_display_generation();
    queue_accept=false;
    assert(xiaotai_board_display_flush(&frame,release)==BK_ERR_BUSY);
    assert(xiaotai_board_display_generation()==3 && releases==3);
    queue_accept=true;
    wait_fail=1;
    assert(xiaotai_board_display_flush(&frame,release)==BK_OK);
    display_process_message(&queue[queue_head++]);
    assert(!s_ready && releases==3);
    assert(xiaotai_board_display_generation()==4);
}
''')

    def test_dot_mouth_stays_inside_partial_region_and_has_closed_base(self):
        source = UI.read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define AI_MOUTH_W 66U
#define AI_MOUTH_H 36U
static uint8_t storage[AI_MOUTH_W * AI_MOUTH_H * 2U + 2U];
static uint8_t *s_ai_mouth_pixels;
typedef struct {uint8_t *frame;} frame_buffer_t;
static frame_buffer_t frame={storage};
static frame_buffer_t *s_ai_mouth_frame;
static frame_buffer_t *frame_buffer_display_malloc(size_t bytes){
    assert(bytes==AI_MOUTH_W*AI_MOUTH_H*2U);return &frame;
}
''' + function(source, 'ai_mouth_pixels') + r'''
int main(void){
    storage[sizeof(storage)-2]=0xa5;storage[sizeof(storage)-1]=0x5a;
    ai_mouth_pixels(0,0x07e0);
    assert(s_ai_mouth_pixels==storage);
    assert(storage[((32*AI_MOUTH_W)+6)*2]==0x07);
    assert(storage[((32*AI_MOUTH_W)+6)*2+1]==0xe0);
    assert(storage[((27*AI_MOUTH_W)+6)*2]==0);
    ai_mouth_pixels(7,0x07e0);
    assert(storage[((2*AI_MOUTH_W)+6)*2]==0x07);
    assert(storage[((27*AI_MOUTH_W)+15)*2]==0);
    assert(storage[sizeof(storage)-2]==0xa5 && storage[sizeof(storage)-1]==0x5a);
}
''')


if __name__ == '__main__':
    unittest.main()
