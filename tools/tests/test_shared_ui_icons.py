"""Validate SDK-neutral icon resources and the actual BK framebuffer renderer."""
import hashlib
import json
import subprocess
import unittest
from pathlib import Path
from test_s3_product_regressions import ROOT, function, run_c

ASSETS = ROOT / 'product/assets/icons/wechat_call'
BK = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
ESP = ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c'

class SharedUiIcons(unittest.TestCase):
    def test_native_vector_sizes_are_current(self):
        import xml.etree.ElementTree as ET
        manifest=json.loads((ASSETS / 'manifest.json').read_text())
        self.assertEqual(manifest['source'],'icon.svg')
        source=(ASSETS / manifest['source']).read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),manifest['source_sha256'])
        svg=ET.fromstring(source)
        self.assertEqual(svg.attrib['viewBox'],'0 0 28 28')
        self.assertEqual(svg.find('{http://www.w3.org/2000/svg}path').attrib['fill-rule'],'evenodd')
        self.assertEqual(len((ASSETS / 'icon_small.a4').read_bytes()),392)
        subprocess.run(['python3',str(ROOT / 'tools/generate_ui_icons.py'),'--check'],check=True)
        self.assertEqual(manifest['variants'],{'small':[28,28],'big':[56,56]})

    def test_shared_alpha_access_and_native_size_selection(self):
        run_c('#include "'+str(ASSETS / 'icon_small.h')+'"\n' +
              '#include "'+str(ASSETS / 'icon_big.h')+'"\n' + r'''
#include <assert.h>
int main(void){
    assert(sizeof(xiaotai_wechat_call_small)==392);
    assert(sizeof(xiaotai_wechat_call_big)==1568);
    unsigned small_sum=0,big_sum=0;
    for(unsigned y=0;y<28;y++)for(unsigned x=0;x<28;x++)
        small_sum+=xiaotai_icon_alpha4(xiaotai_wechat_call_small,28,28,x,y);
    for(unsigned y=0;y<56;y++)for(unsigned x=0;x<56;x++)
        big_sum+=xiaotai_icon_alpha4(xiaotai_wechat_call_big,56,56,x,y);
    assert(big_sum>small_sum*39/10 && big_sum<small_sum*41/10);
    assert(xiaotai_icon_alpha4(xiaotai_wechat_call_small,28,28,28,0)==0);
    assert(xiaotai_icon_alpha4(xiaotai_wechat_call_small,28,28,0,28)==0);
    assert(xiaotai_icon_alpha4(NULL,28,28,0,0)==0);
    assert(xiaotai_wechat_call_use_big(480));
    assert(!xiaotai_wechat_call_use_big(320));
    assert(!xiaotai_wechat_call_use_big(240));
}
''')
        source=ESP.read_text()
        button=function(source,'make_home_wechat_button')
        self.assertIn('xiaotai_wechat_call_use_big(display_driver_height())',button)
        self.assertIn('lv_img_set_src(icon, &s_wechat_call_icon_big)',button)
        self.assertIn('lv_img_set_src(icon, &s_wechat_call_icon)',button)
        self.assertNotIn('lv_img_set_zoom',button)
        self.assertNotIn('s_wechat_call_icon_map[]',source)

    def test_native_vector_has_crisp_outline_and_open_handset(self):
        run_c('#include "'+str(ASSETS / 'icon_small.h')+'"\n'+
              '#include "'+str(ASSETS / 'icon_big.h')+'"\n'+r'''
#include <assert.h>
static unsigned big(unsigned x,unsigned y){
    return xiaotai_icon_alpha4(xiaotai_wechat_call_big,56,56,x,y);
}
int main(void){
    assert(sizeof(xiaotai_wechat_call_big)==1568);
    /* Radius11 circle at (14,13): native56px outline crosses x6/x50.
     * Adjacent outside/inside pixels must stay crisp, without blur halos. */
    assert(big(5,26)==0 && big(6,26)==255);
    assert(big(49,26)==255 && big(50,26)==0);
    assert(big(36,36)==0); /* receiver cutout stays open */
    assert(big(28,26)==255); /* bubble body stays solid */
    unsigned edge_pixels=0;
    for(unsigned y=0;y<56;y++)for(unsigned x=0;x<56;x++){
        unsigned a=big(x,y);if(a>0 && a<255)edge_pixels++;
    }
    assert(edge_pixels>80 && edge_pixels<300); /* thin antialiased contour */
    assert(big(0,0)==0 && big(55,55)==0);
}
''')

    def test_bk_draws_shared_artwork_without_rgb_bitmap_or_extra_heap(self):
        source=(BK / 'ap/src/xiaotai_ui.c').read_text()
        draw=function(source,'phone_shortcut')
        self.assertNotIn('xiaotai_phone_shortcut_rgb565',draw)
        self.assertNotIn('malloc',draw)
        run_c('#include "'+str(ASSETS / 'icon_small.h')+'"\n'+r'''
#include <assert.h>
#include <string.h>
#define XIAOTAI_UI_HOME_QUICK_CALL_ICON_X 276U
#define XIAOTAI_UI_HOME_QUICK_CALL_ICON_Y 150U
typedef struct {uint16_t pixels[320*240];} frame_buffer_t;
static void pixel(frame_buffer_t *f,int x,int y,uint16_t c){
    assert(x>=0 && x<320 && y>=0 && y<240);f->pixels[y*320+x]=c;
}
static uint16_t frame_pixel(frame_buffer_t *f,int x,int y){return f->pixels[y*320+x];}
static void circle(frame_buffer_t *f,int cx,int cy,int radius,uint16_t color){
    for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++)
        if(x*x+y*y<=radius*radius)pixel(f,cx+x,cy+y,color);
}
''' + function(source,'blend_rgb565') + draw + r'''
int main(void){
    frame_buffer_t frame;memset(&frame,0,sizeof(frame));phone_shortcut(&frame);
    const uint16_t green=xiaotai_icon_rgb565(XIAOTAI_WECHAT_CALL_BACKGROUND);
    const uint16_t white=xiaotai_icon_rgb565(XIAOTAI_WECHAT_CALL_FOREGROUND);
    /* BK preserves the44px hit/layout region; its40px drawn button uses
     * the same28px glyph as the compact ESP32 UI, centred with8px inset. */
    for(unsigned y=0;y<28;y++)for(unsigned x=0;x<28;x++){
        unsigned alpha=xiaotai_icon_alpha4(xiaotai_wechat_call_small,28,28,x,y);
        int dx=(int)x-14,dy=(int)y-14;
        int distance=dx*dx+dy*dy;
        uint16_t base=distance<=19*19 ? green : distance<=20*20 ?
            xiaotai_icon_rgb565(XIAOTAI_WECHAT_CALL_BORDER) : 0;
        uint16_t expected=blend_rgb565(white,base,alpha);
        assert(frame.pixels[(158+y)*320+284+x]==expected);
    }
    assert(frame.pixels[149*320+275]==0); /* outside button remains untouched */
    return 0;
}
''')

if __name__=='__main__':
    unittest.main()
