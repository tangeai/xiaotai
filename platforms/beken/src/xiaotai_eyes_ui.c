#include "xiaotai_eyes_ui.h"
#include "xiaotai_eyes.h"
#include "xiaotai_eye_text.h"
#include "xiaotai_ui_assets.h"
#include "xiaotai_log.h"
#include "board_eyes.h"
#include <os/os.h>
#include <os/mem.h>
#include <stdio.h>
#include <string.h>

#define TAG "xiaotai_eyes"
typedef struct { bool text; char emotion[16],left[192],right[192]; } eyes_view_t;
static eyes_view_t view;
static beken_mutex_t lock;
static const uint8_t *glyph(uint32_t codepoint)
{
    size_t lo=0,hi=xiaotai_cjk_glyph_count;
    while(lo<hi){size_t m=(lo+hi)/2;if(xiaotai_cjk_glyphs[m].codepoint<codepoint)lo=m+1;else hi=m;}
    return lo<xiaotai_cjk_glyph_count && xiaotai_cjk_glyphs[lo].codepoint==codepoint?
           xiaotai_cjk_glyphs[lo].bitmap:NULL;
}
static void eyes_task(beken_thread_arg_t arg)
{
    (void)arg;
    uint16_t *left=psram_malloc(160U*160U*2U),*right=psram_malloc(160U*160U*2U);
    if (!left || !right) {
        if(left) psram_free(left);
        if(right) psram_free(right);
        BK_LOGE(TAG,"eye frame allocation failed\n");rtos_delete_thread(NULL);return;
    }
    eyes_view_t previous={0};bool first=true,previous_blink=false;
    for(;;){
        eyes_view_t next;
        rtos_lock_mutex(&lock);next=view;rtos_unlock_mutex(&lock);
        uint32_t now=rtos_get_time();bool blink=now%4000U<100U;
        if(first || memcmp(&next,&previous,sizeof next)!=0 || (!next.text && blink!=previous_blink)){
            if(next.text){xiaotai_eye_text_render(left,next.left,glyph);xiaotai_eye_text_render(right,next.right,glyph);}
            else{xiaotai_eye_render(left,next.emotion,false,now);xiaotai_eye_render(right,next.emotion,true,now);}
            int rc=xiaotai_board_eyes_present(left,right);
            if(rc!=0){
                /* Failed DMA may still own either buffer. Never free or reuse them. */
                BK_LOGE(TAG,"dual-eye DMA failure rc=%d; rendering stopped, frames retained\n",rc);
                rtos_delete_thread(NULL);return;
            }
            previous=next;previous_blink=blink;first=false;
        }
        rtos_delay_milliseconds(125);
    }
}
int xiaotai_eyes_ui_start(void)
{
    if(lock) return 0;
    int rc=rtos_init_mutex(&lock);
    if(rc!=0)return rc;
    rc=xiaotai_board_eyes_init();
    if(rc!=0){rtos_deinit_mutex(&lock);lock=NULL;return rc;}
    snprintf(view.emotion,sizeof view.emotion,"neutral");
    rc=rtos_create_psram_thread(NULL,4,"xiaotai_eyes",eyes_task,4096,NULL);
    if(rc!=0){rtos_deinit_mutex(&lock);lock=NULL;}
    BK_LOGI(TAG,"dual eyes LCD1=left/QSPI1 LCD2=right/QSPI0 160x160; init rc=%d; physical mapping pending\n",rc);
    return rc;
}
void xiaotai_eyes_ui_tag(const char *emotion,bool explicit_tag,int phase)
{
    if(!lock)return;
    rtos_lock_mutex(&lock);view.text=false;
    snprintf(view.emotion,sizeof view.emotion,"%s",explicit_tag && emotion?emotion:phase==1?"thinking":phase==2?"happy":"neutral");
    rtos_unlock_mutex(&lock);
}
void xiaotai_eyes_ui_idle(void){xiaotai_eyes_ui_tag("neutral",true,0);}
void xiaotai_eyes_ui_text(const char *left,const char *right)
{
    if(!lock)return;
    rtos_lock_mutex(&lock);view.text=true;
    snprintf(view.left,sizeof view.left,"%s",left?left:"");
    snprintf(view.right,sizeof view.right,"%s",right?right:"");
    rtos_unlock_mutex(&lock);
}
