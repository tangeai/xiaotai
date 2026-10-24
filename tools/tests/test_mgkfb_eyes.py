"""Dual-eye rendering and board display ownership through production interfaces."""
import re
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from board_registry import load_boards,build_command
from test_s3_product_regressions import run_c,function
ROOT=Path(__file__).resolve().parents[2]

class DualEyes(unittest.TestCase):
    def test_native_source_selection_keeps_other_boards_isolated(self):
        # Execute each native CMake entry, including target_sources added after
        # component registration; a manifest alone cannot prove isolation.
        for board, variant in (('lckfb-bk7258', ''), ('mgkfb-bk7258', '')):
            with self.subTest(board=board, variant=variant), tempfile.TemporaryDirectory() as temp:
                temp = Path(temp)
                (temp/'xiaotai_build_identity.h').write_text('/* test identity */')
                script = temp/'selection.cmake'
                script.write_text('''
function(set_property)
endfunction()
function(armino_component_register)
  cmake_parse_arguments(A "" "" "SRCS;INCLUDE_DIRS;PRIV_REQUIRES" ${ARGN})
  file(WRITE "${CMAKE_BINARY_DIR}/../sources.txt" "${A_SRCS}")
  set(COMPONENT_LIB test_component PARENT_SCOPE)
endfunction()
function(target_sources target scope)
  file(APPEND "${CMAKE_BINARY_DIR}/../sources.txt" ";${ARGN}")
endfunction()
function(target_compile_definitions)
endfunction()
function(target_include_directories)
endfunction()
function(target_link_libraries)
endfunction()
function(add_prebuilt_library)
endfunction()
''' + f'set(CMAKE_BINARY_DIR "{temp}/build")\n' +
                    f'include("{ROOT}/firmware/beken/bk7258/{board}-xiaotai/ap/CMakeLists.txt")\n')
                subprocess.run(['cmake', '-P', str(script)], check=True,
                               capture_output=True, env={**os.environ,
                               'XIAOTAI_BOARD_VARIANT': variant})
                sources = (temp/'sources.txt').read_text().split(';')
                names = [Path(s).name for s in sources]
                self.assertTrue(all(Path(s).is_file() for s in sources))
                self.assertEqual(len(sources), len(set(sources)))
                eyes = board == 'mgkfb-bk7258'
                for name in ('board_eyes.c', 'xiaotai_eyes_ui.c',
                             'xiaotai_eyes.c', 'xiaotai_eye_text.c'):
                    self.assertEqual(name in names, eyes, name)
                legacy = board == 'lckfb-bk7258'
                for name in ('xiaotai_ui.c', 'xiaotai_button.c', 'board_touch.c'):
                    self.assertEqual(name in names, legacy, name)
                self.assertEqual('xiaotai_key_input.c' in names, not legacy)
                self.assertEqual('xiaotai_ui_assets.c' in names, legacy or eyes)
                other = 'mgkfb' if legacy else 'lckfb'
                self.assertFalse(any(f'/boards/{other}/' in s for s in sources))

    def test_tag_raster_is_bounded_distinct_and_has_no_mouth(self):
        source=(ROOT/'product/src/xiaotai_eyes.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "xiaotai_eyes.h"
'''+source.replace('#include "xiaotai_eyes.h"','')+r'''
int main(void){
 uint16_t guarded[25602],left[25600],right[25600];
 guarded[0]=0x1234;guarded[25601]=0x5678;
 const char *tags[]={"neutral","happy","laughing","funny","sad","angry","crying","loving","embarrassed","surprised","shocked","thinking","winking","cool","relaxed","delicious","kissy","confident","sleepy","silly","confused"};
 uint32_t hashes[21];
 for(unsigned t=0;t<21;t++){
  xiaotai_eye_render(guarded+1,tags[t],false,1000);
  assert(guarded[0]==0x1234 && guarded[25601]==0x5678);
  unsigned count=0;uint32_t hash=2166136261u;
  for(unsigned i=0;i<25600;i++){count+=guarded[i+1]!=0;hash=(hash^guarded[i+1])*16777619u;}
  assert(count>50 && count<15000);hashes[t]=hash;
  for(unsigned y=144;y<160;y++)for(unsigned x=0;x<160;x++)assert(guarded[1+y*160+x]==0);
  for(unsigned j=0;j<t;j++) assert(hashes[j]!=hashes[t]);
 }
 xiaotai_eye_render(left,"winking",false,1000);
 xiaotai_eye_render(right,"winking",true,1000);assert(memcmp(left,right,sizeof left)!=0);
 xiaotai_eye_render(left,"unknown",false,1000);
 xiaotai_eye_render(right,"neutral",false,1000);assert(memcmp(left,right,sizeof left)==0);
 xiaotai_eye_render(left,"neutral",false,50);assert(memcmp(left,right,sizeof left)!=0);
}
''',include_dirs=[ROOT/'product/include'])

    def test_text_wraps_utf8_and_stays_inside_six_rows(self):
        source=(ROOT/'product/src/xiaotai_eye_text.c').read_text()
        run_c(r'''
#include <assert.h>
#include "xiaotai_eye_text.h"
'''+source.replace('#include "xiaotai_eye_text.h"','')+r'''
static unsigned looked_up;
static const uint8_t *font(uint32_t cp){assert(cp==0x4e2d);looked_up++;return NULL;}
int main(void){
 uint16_t image[25602];image[0]=0x1234;image[25601]=0x5678;
 assert(xiaotai_eye_text_render(image+1,"192.168.4.1",NULL)==11);
 for(unsigned y=32;y<160;y++)for(unsigned x=0;x<160;x++)assert(image[1+y*160+x]==0);
 const char *longtext="中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中中";
 assert(xiaotai_eye_text_render(image+1,longtext,font)==48 && looked_up==48);
 assert(image[0]==0x1234 && image[25601]==0x5678);
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<160;x++)
  if(x<12 || x>=148 || y<16 || y>=132)assert(image[1+y*160+x]==0);
 assert(xiaotai_eye_text_render(image+1,"\xe4",NULL)==1); /* truncated UTF8 */
}
''',include_dirs=[ROOT/'product/include'])

    def test_dma_waits_for_both_panels_even_when_one_fails(self):
        source=(ROOT/'boards/mgkfb/bk7258/board_eyes.c').read_text()
        source=re.sub(r'^#include.*$','',source,flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BK_OK 0
#define BK_ERR_NOT_INIT -2
#define PERIPHERAL_POWER_GPIO 52
#define EYES_BACKLIGHT_GPIO 25
#define EYES_LEFT_BUS 1
#define EYES_RIGHT_BUS 0
#define EYES_LEFT_RESET_GPIO 45
#define EYES_LEFT_DC_GPIO 5
#define EYES_RIGHT_RESET_GPIO 6
#define EYES_RIGHT_DC_GPIO 7
#define LCD_QSPI_40M 40
typedef int gpio_id_t;
typedef struct {int clk;} lcd_spi_t;
typedef struct {const lcd_spi_t *spi;int width,height;} lcd_device_t;
static lcd_spi_t sdk_spi={60};
static lcd_device_t device={&sdk_spi,160,160};
static unsigned initialized,waits[2],submits[2];static int fail_submit=-1,fail_wait=-1;
static int gpio_dev_unmap(int p){assert(p==52 || p==25);return 0;}
static int bk_gpio_disable_input(int p){(void)p;return 0;}
static int bk_gpio_set_output_value(int p,bool v){assert(p!=52 || v);return 0;}
static void rtos_delay_milliseconds(unsigned n){assert(n==20);}
static int bk_qspi_driver_init(void){return 0;}
static const lcd_device_t *get_lcd_device_by_name(char *n){(void)n;return &device;}
static void bk_lcd_spi_init(int id,const lcd_device_t *d,int reset,int dc){
 assert(d->width==160 && d->height==160 && d->spi->clk==40);
 assert((id==1 && reset==45 && dc==5)||(id==0 && reset==6 && dc==7));initialized++;
}
static int bk_lcd_spi_frame_display(int id,uint8_t *p,unsigned n){assert(p && n==51200);submits[id]++;return id==fail_submit?-3:0;}
static int bk_lcd_spi_wait_display_complete(int id){waits[id]++;return id==fail_wait?-4:0;}
'''+source+r'''
int main(void){uint16_t a[25600],b[25600];
 assert(xiaotai_board_eyes_present(a,b)==-2);
 assert(xiaotai_board_eyes_init()==0 && initialized==2);
 assert(xiaotai_board_eyes_present(a,b)==0 && waits[0]==1 && waits[1]==1);
 fail_wait=1;assert(xiaotai_board_eyes_present(a,b)==-4 && waits[0]==2 && waits[1]==2);
 assert(xiaotai_board_eyes_present(a,b)==-2);
 assert(xiaotai_board_eyes_init()==0);fail_wait=-1;fail_submit=0;
 assert(xiaotai_board_eyes_present(a,b)==-3 && waits[1]==3 && waits[0]==2);
}
''')

    def test_runtime_ai_tags_and_provisioning_reach_eye_presentation(self):
        source=(ROOT/'platforms/beken/src/xiaotai_headless_ui.c').read_text()
        network=(ROOT/'platforms/beken/runtime/src/xiaotai_network.c').read_text()
        setup_ip=re.search(r'^#define SETUP_IP "([^"]+)"',network,re.M).group(1)
        funcs='\n'.join(function(source,n) for n in ['xiaotai_ui_show_ai','xiaotai_ui_update_ai_mouth','xiaotai_ui_show_network_setup'])
        run_c('#define EXPECTED_SETUP_IP "'+setup_ip+'"\n'+r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define XIAOTAI_DUAL_EYES 1
#define BK_LOGI(...) ((void)0)
typedef int xiaotai_ai_ui_phase_t;
static atomic_uint s_indicator;
static char tag[32],left[192],right[192];static bool tagged;static int calls,phase_seen;
static void xiaotai_eyes_ui_tag(const char *t,bool explicit_tag,int phase){snprintf(tag,sizeof tag,"%s",t);tagged=explicit_tag;phase_seen=phase;calls++;}
static void xiaotai_eyes_ui_text(const char *l,const char *r){snprintf(left,sizeof left,"%s",l);snprintf(right,sizeof right,"%s",r);}
'''+funcs+r'''
int main(void){
 xiaotai_ui_show_ai(2,"neutral","caption",true,true);
 assert(tagged && strcmp(tag,"neutral")==0 && phase_seen==2);
 xiaotai_ui_update_ai_mouth(0x1234);assert(calls==1);
 xiaotai_ui_show_ai(1,"happy","caption",true,false);assert(!tagged && phase_seen==1);
 xiaotai_ui_show_network_setup("XiaoTai-1234");
 assert(strstr(left,"XiaoTai-1234") && strstr(right,EXPECTED_SETUP_IP));
}
''')

    def test_both_sdk_build_passes_preserve_selected_config(self):
        script=(ROOT/'platforms/beken/tools/build.sh').read_text().replace('\\\n',' ')
        commands=[line.strip() for line in script.splitlines() if line.strip().startswith('make -C')]
        self.assertEqual(len(commands),2)
        for config in ('config','panel.config','panel_release.config','release.config'):
            harness='make(){ printf "%s\\n" "$*"; }; project_dir=/tmp/board; sdk_dir=/tmp/sdk; build_version=1.0.1; config_args=(CONFIG_SUBTITUTE_FILE='+config+');\n'+'\n'.join(commands)
            result=subprocess.run(['bash','-c',harness],check=True,capture_output=True,text=True)
            calls=result.stdout.splitlines()
            self.assertEqual(len(calls),2)
            for call in calls:
                self.assertIn('CONFIG_SUBTITUTE_FILE='+config,call)
                self.assertNotIn('BK_CONFIG_FILE=',call)

    def test_sdk_configuration_selection_keeps_one_project_directory(self):
        script=(ROOT/'platforms/beken/tools/build.sh').read_text()
        selection=script[script.index('unset BK_CONFIG_FILE'):script.index('# Lock the prebuilt SDK')]
        for variant in ('', 'default', 'panel'):
            for profile in ('development', 'release'):
                with self.subTest(variant=variant, profile=profile), tempfile.TemporaryDirectory() as temp:
                    project=Path(temp)/'demo'
                    for core,soc in (('ap','bk7258_ap'),('cp','bk7258')):
                        directory=project/core/'config'/soc
                        directory.mkdir(parents=True)
                        for name in ('config','panel.config'):
                            (directory/name).write_text('CONFIG_LOG_LEVEL=4\n')
                    setup=f'''project_dir="{project}"
support_dir="{ROOT}/platforms/beken"
board_variant="{variant}"
build_profile="{profile}"
ap_config="$project_dir/ap/config/bk7258_ap/config"
cp_config="$project_dir/cp/config/bk7258/config"
ap_release_config="$project_dir/ap/config/bk7258_ap/release.config"
cp_release_config="$project_dir/cp/config/bk7258/release.config"
'''
                    result=subprocess.run(['bash','-eu','-c',setup+selection+
                        '\nprintf "%s\\n" "$artifact_project_name" "${config_args[@]}"'],
                        check=True,capture_output=True,text=True,
                        env={**os.environ,'BK_CONFIG_FILE':'stale','CONFIG_SUBTITUTE_FILE':'stale'})
                    config='panel.config' if variant=='panel' else 'config'
                    if profile=='release':config='panel_release.config' if variant=='panel' else 'release.config'
                    self.assertEqual(result.stdout.splitlines(),['demo','CONFIG_SUBTITUTE_FILE='+config])

    def test_board_has_one_build_with_displays_enabled(self):
        board=next(b for b in load_boards(ROOT) if b.id=='mgkfb-bk7258')
        self.assertEqual(board.data['variants'], [{'name':'default','default':True,
            'interaction_profile':'eyes-key','layout_profile':'compact'}])
        command,_=build_command(ROOT,board,board.default_variant())
        self.assertEqual(command,['bash','tools/build.sh'])
        for obsolete in ('eyes','headless'):
            result=subprocess.run([sys.executable,str(ROOT/'tools/build.py'),
                '--board',board.id,'--variant',obsolete,'--dry-run'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
        project=ROOT/board.data['project_dir']
        for core,soc in (('ap','bk7258_ap'),('cp','bk7258')):
            config=(project/core/'config'/soc/'config').read_text()
            self.assertIn('CONFIG_QSPI_LINE_MODE=1\n',config)
            self.assertFalse((project/core/'config'/soc/'eyes.config').exists())
        config=(project/'ap/config/bk7258_ap/config').read_text()
        for option in ('LCD_SPI','LCD_SPI_GC9D01','LCD_SPI_REFRESH_WITH_QSPI_MAPPING_MODE'):
            self.assertIn('CONFIG_'+option+'=y\n',config)


if __name__=='__main__':unittest.main()
