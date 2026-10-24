"""Build selection and public admission contracts for the MGKFB SMP board."""
import os
import json
import re
import tempfile
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from board_registry import load_boards, build_command
from firmware_version import read_board_version
from test_s3_product_regressions import function, run_c

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'firmware/beken/bk7258/mgkfb-bk7258-xiaotai'
LEGACY = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'


class MgkfbBoard(unittest.TestCase):
    def test_sdk_default_init_routes_camera_clock_sync_and_data(self):
        fixture=(ROOT/'tools/tests/fixtures/bk7258_gpio_default_init.c').read_text()
        names=['MCLK','PCLK','HSYNC','VSYNC']+[f'PXDATA{i}' for i in range(8)]
        devices=[f'GPIO_DEV_JPEG_{n}' for n in names]
        for core,soc in (('ap','bk7258_ap'),('cp','bk7258')):
            with self.subTest(core=core):
                header=(PROJECT/f'{core}/config/{soc}/usr_gpio_cfg.h').read_text().replace('#pragma once','')
                tokens=set(re.findall(r'\bGPIO_[A-Z0-9_]+\b',header+fixture))|set(devices)
                tokens-={'GPIO_DEFAULT_DEV_CONFIG','GPIO_DEFAULT_SET_SUPPORT'}
                fixed={'GPIO_INIT_DISABLE':0,'GPIO_INIT_ENABLE':1,
                       'GPIO_SECOND_FUNC_DISABLE':0,'GPIO_SECOND_FUNC_ENABLE':1,
                       'GPIO_DEV_INVALID':0}
                macros='\n'.join(f'#define {t} '+str(fixed.get(t,
                         int(t[5:]) if t[5:].isdigit() else 100+i))
                         for i,t in enumerate(sorted(tokens)))
                expected=','.join(devices)
                run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define BK_OK 0
#define HAL_LOGV(...) ((void)0)
#define gpio_hal_output_enable(...) ((void)0)
#define gpio_hal_input_enable(...) ((void)0)
#define gpio_hal_pull_enable(...) ((void)0)
#define gpio_hal_pull_up_enable(...) ((void)0)
#define gpio_hal_disable_interrupt(...) ((void)0)
#define gpio_hal_enable_interrupt(...) ((void)0)
#define gpio_hal_set_int_type(...) ((void)0)
#define gpio_hal_set_output_value(...) ((void)0)
#define gpio_hal_set_capacity(...) ((void)0)
#define gpio_hal_func_unmap(h,p) (mapped[p]=0)
#define gpio_hal_func_map(h,p,d) (mapped[p]=(d))
typedef int bk_err_t;
typedef int gpio_hal_t;
typedef struct {unsigned gpio_id,second_func_en,second_func_dev,io_mode,pull_mode,
 int_en,int_type,low_power_io_ctrl,driver_capacity,gpio_init,gpio_time_sharing_multiplex;} gpio_default_map_t;
static unsigned mapped[64];
'''+macros+'\n'+header+'\n'+fixture+r'''
int main(void) {
 assert(gpio_hal_default_map_init(NULL)==BK_OK);
 unsigned pins[]={27,29,30,31,32,33,34,35,36,37,38,39};
 unsigned expected[]={'''+expected+r'''};
 for(unsigned i=0;i<12;i++) assert(mapped[pins[i]]==expected[i]);
 /* Reset, buttons, LEDs, power and serial are not driven by the camera table. */
 unsigned untouched[]={0,1,8,10,11,12,13,28,40,41,49,50,52};
 for(unsigned i=0;i<13;i++) assert(mapped[untouched[i]]==0);
}
''')

    def test_sdk_gpio_admission_allows_board_startup(self):
        fixture=(ROOT/'tools/tests/fixtures/bk7258_gpio_admission.c').read_text()
        board=(ROOT/'boards/mgkfb/bk7258/board_button.c').read_text()
        board=re.sub(r'^#include.*$', '', board, flags=re.M)
        for core, soc in (('ap','bk7258_ap'),('cp','bk7258')):
            with self.subTest(core=core):
                header=(PROJECT/f'{core}/config/{soc}/usr_gpio_cfg.h').read_text()
                tokens=set(re.findall(r'\bGPIO_[A-Z0-9_]+\b', header))
                tokens.discard('GPIO_DEFAULT_DEV_CONFIG')
                macros='\n'.join(f'#define {t} '+(t[5:] if t[5:].isdigit() else '0')
                                 for t in sorted(tokens))
                run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define BK_OK 0
#define BK_ERR_GPIO_INVALID_OPERATE -9
#define CONFIG_GPIO_DEFAULT_SET_SUPPORT 1
#define GPIO_LOGW(...) ((void)0)
#define gpio_hal_output_enable(...) ((void)0)
#define gpio_hal_input_enable(...) ((void)0)
#define gpio_hal_pull_enable(...) ((void)0)
#define gpio_hal_disable_interrupt(...) ((void)0)
#define gpio_hal_func_unmap(...) ((void)0)
typedef int gpio_id_t;
typedef int bk_err_t;
typedef struct {int gpio_id,a,b,c,d,e,f,g,h,i,j;} gpio_default_map_t;
#define AI_BUTTON 12
#define VOLUME_UP_BUTTON 13
#define VOLUME_DOWN_BUTTON 8
static int bk_gpio_disable_output(int p){(void)p;return 0;}
static int bk_gpio_enable_input(int p){(void)p;return 0;}
static int bk_gpio_pull_up(int p){(void)p;return 0;}
static int bk_gpio_get_input(int p){(void)p;return 1;}
''' + macros+'\n'+header.replace('#pragma once','')+'\n'+fixture+board+r'''
int main(void) {
 /* app_start stops before network_start if this public adapter fails. */
 assert(xiaotai_board_button_init()==BK_OK);
 int pins[]={27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,49,50,52};
 for(unsigned i=0;i<sizeof(pins)/sizeof(pins[0]);i++) assert(gpio_dev_unmap(pins[i])==BK_OK);
 assert(gpio_dev_unmap(63)==BK_ERR_GPIO_INVALID_OPERATE);
}
''')

    def test_native_packaged_image_keeps_selected_board_identity(self):
        tool = ROOT / 'platforms/beken/tools/build_identity.py'
        for board in ('mgkfb-bk7258', 'lckfb-bk7258'):
            with self.subTest(board=board), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                counter = root / 'BUILD_NUMBER'
                counter.write_text('0\n')
                subprocess.run([sys.executable, str(tool), 'prepare', str(root),
                                '1.0.1', 'abcdef', str(counter), 'development',
                                'Board identity regression', board], check=True)
                elf = root / 'app.elf'
                elf.write_bytes(b'1.0.1+build.1')
                elf.with_suffix('.bin').write_bytes(b'firmware')
                (root / 'partitions').mkdir()
                (root / 'partitions/bk_package.json').write_text(json.dumps({
                    'crc_enable': False, 'section': [
                        {'firmware': 'app1.bin', 'start_addr': '0x0'}]}))
                image = root / 'all-app.bin'
                image.write_bytes(b'firmware')
                subprocess.run([sys.executable, str(tool), 'finalize', str(root),
                                str(image), str(elf)], check=True, capture_output=True)
                identity = json.loads((root / 'build_identity.json').read_text())
                self.assertEqual(identity['board_id'], board)
                self.assertEqual(identity['flash_file'], f'{board}-1.0.1+build.1-all-app.bin')
                self.assertEqual((root / identity['flash_file']).read_bytes(), image.read_bytes())
                self.assertEqual(len(list(root.glob('*-all-app.bin'))), 1)

    def test_amplifier_controls_only_its_mute_and_shared_rail(self):
        source=(ROOT/'boards/mgkfb/bk7258/board_audio.c').read_text()
        source=re.sub(r'^#include.*$', '', source, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
typedef int gpio_id_t;
typedef int bk_err_t;
#define AMPLIFIER_MUTE_GPIO 50
#define PERIPHERAL_POWER_GPIO 52
#define BK_OK 0
#define BK_ERR_NOT_INIT -1
static int values[64], failure=-1, writes;
static int gpio_dev_unmap(int p){assert(p==50 || p==52);return p==failure?-7:0;}
static int bk_gpio_disable_input(int p){(void)p;return 0;}
static int bk_gpio_set_output_value(int p,bool v){assert(p==50 || (p==52 && v));values[p]=v;writes++;return 0;}
static void rtos_delay_milliseconds(unsigned ms){assert(ms<=20);}
''' + source + r'''
int main(void){
 assert(xiaotai_board_amplifier_set_playing(true)==BK_ERR_NOT_INIT);
 assert(writes==0);
 assert(xiaotai_board_amplifier_prepare()==0 && values[52]==1 && values[50]==0);
 assert(xiaotai_board_amplifier_set_playing(true)==0 && values[50]==1);
 xiaotai_board_amplifier_power_off();assert(values[50]==0 && values[52]==1);
 failure=50;assert(xiaotai_board_amplifier_prepare()==-7);
 assert(!xiaotai_board_amplifier_prepared());
 failure=52;assert(xiaotai_board_amplifier_prepare()==-7);
 assert(!xiaotai_board_amplifier_prepared());
}
''')

    def test_status_and_incoming_call_use_board_indicator(self):
        source=(ROOT/'platforms/beken/src/xiaotai_headless_ui.c').read_text()
        # Compile the production presentation entry points, not a second state machine.
        names=['xiaotai_ui_init','xiaotai_ui_show_status',
               'xiaotai_ui_refresh_verification_countdown', 'xiaotai_ui_probe_lcd']
        funcs='\n'.join(function(source,name) for name in names)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#define BK_LOGI(...) ((void)0)
static atomic_uint s_indicator;
static unsigned now;
static bool red,green;
static int xiaotai_board_indicator_init(void){return 0;}
static int xiaotai_board_indicator_set(bool r,bool g){red=r;green=g;return 0;}
static unsigned rtos_get_time(void){return now;}
''' + funcs + r'''
int main(void) {
 (void)s_indicator;
 assert(xiaotai_ui_init()==0 && xiaotai_ui_probe_lcd()==NULL);
 xiaotai_ui_show_status("READY");xiaotai_ui_refresh_verification_countdown();
 assert(green && !red);
 xiaotai_ui_show_status("VOIP CALL");now=250;xiaotai_ui_refresh_verification_countdown();
 assert(red && green);
 now=500;xiaotai_ui_refresh_verification_countdown();assert(!red && green);
 xiaotai_ui_show_status("ERROR");xiaotai_ui_refresh_verification_countdown();assert(red && !green);
}
''')

    def test_real_key_worker_delivers_each_volume_click_without_double_window(self):
        gesture=(ROOT/'product/interaction/src/xiaotai_key_gesture.c').read_text()
        worker=(ROOT/'platforms/beken/src/xiaotai_key_input.c').read_text()
        sources='\n'.join(re.sub(r'^#include.*$','',x,flags=re.M) for x in (gesture,worker))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stddef.h>
#include "xiaotai_key_gesture.h"
#include "xiaotai_button.h"
#define BK_LOGD(...) ((void)0)
#define BK_LOGI(...) ((void)0)
#define BK_ERR_PARAM -1
#define BK_ERR_BUSY -2
#define BK_OK 0
typedef void *beken_thread_t;
typedef void *beken_thread_arg_t;
static uint32_t now;
static jmp_buf done;
static void (*task)(beken_thread_arg_t);
static unsigned up,down,primary_double,first_up;
static unsigned rtos_get_time(void){return now;}
static void rtos_delay_milliseconds(unsigned ms){now+=ms;if(now>=1400)longjmp(done,1);}
static int xiaotai_board_button_init(void){return 0;}
static bool xiaotai_board_key_released(unsigned i){
 bool press=(now>=100 && now<200)||(now>=280 && now<380);
 if(i==1)press=press||(now>=600 && now<700);
 if(i==2)press=press||(now>=1000 && now<1100);
 return !press;
}
static int rtos_create_thread(beken_thread_t *t,int priority,const char *name,
 void (*fn)(beken_thread_arg_t),unsigned stack,beken_thread_arg_t arg){
 (void)priority;(void)name;(void)stack;(void)arg;*t=(void *)1;task=fn;return 0;
}
'''+sources+r'''
static void got(xiaotai_button_event_t e,void *ctx){
 assert(ctx==(void *)42);
 if(e==XIAOTAI_BUTTON_VOLUME_UP){if(!up)first_up=now;up++;}
 if(e==XIAOTAI_BUTTON_VOLUME_DOWN)down++;
 if(e==XIAOTAI_BUTTON_DOUBLE)primary_double++;
}
int main(void){
 assert(xiaotai_button_start(got,(void *)42)==0);
 if(setjmp(done)==0)task(NULL);
 assert(up==3 && down==3 && primary_double==1);
 assert(first_up>=200 && first_up<=260);
}
''',include_dirs=[ROOT/'product/interaction/include',ROOT/'platforms/beken/runtime/include'])

    def test_key_debounce_double_long_boot_hold_and_clock_wrap(self):
        header = (ROOT/'product/interaction/include/xiaotai_key_gesture.h').read_text()
        source = (ROOT/'product/interaction/src/xiaotai_key_gesture.c').read_text()
        run_c('#include <assert.h>\n' + header.replace('#pragma once', '') +
              source.replace('#include "xiaotai_key_gesture.h"', '') + r'''
static unsigned counts[4];
static uint32_t now;
static xiaotai_key_gesture_t key;
static void sample(bool released, unsigned n) {
 for(unsigned i=0;i<n;i++) { now+=20; counts[xiaotai_key_sample(&key,released,now)]++; }
}
static void reset(uint32_t t) { key=(xiaotai_key_gesture_t){0}; now=t;
 for(unsigned i=0;i<4;i++) counts[i]=0; }
int main(void) {
 reset(0); sample(false,60); sample(true,30);
 assert(counts[1]+counts[2]+counts[3]==0); /* boot-held release is not a click */
 sample(false,1);sample(true,1);sample(false,1);sample(true,25);
 assert(counts[1]+counts[2]+counts[3]==0); /* bounce */
 sample(false,5); sample(true,25); assert(counts[XIAOTAI_KEY_SHORT]==1);
 reset(0);sample(true,4);sample(false,5);sample(true,5);sample(false,5);sample(true,25);
 assert(counts[XIAOTAI_KEY_DOUBLE]==1 && counts[XIAOTAI_KEY_SHORT]==0);
 reset(0);sample(true,4);sample(false,70);sample(true,25);
 assert(counts[XIAOTAI_KEY_LONG]==1 && counts[XIAOTAI_KEY_SHORT]==0);
 reset(UINT32_MAX-100);sample(true,4);sample(false,5);sample(true,25);
 assert(counts[XIAOTAI_KEY_SHORT]==1);
}
''')

    def test_selected_cmake_sources_are_platform_and_mgkfb_only(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            (temp/'xiaotai_build_identity.h').write_text('/* test identity */')
            script = temp/'selection.cmake'
            script.write_text('''
function(set_property)
endfunction()
function(armino_component_register)
  cmake_parse_arguments(A "" "" "SRCS;INCLUDE_DIRS;PRIV_REQUIRES" ${ARGN})
  file(WRITE "${CMAKE_BINARY_DIR}/../sources.txt" "${A_SRCS}")
endfunction()
function(target_sources)
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
                f'include("{PROJECT}/ap/CMakeLists.txt")\n')
            subprocess.run(['cmake','-P',str(script)],check=True,capture_output=True,
                           env={**os.environ,'XIAOTAI_BOARD_VARIANT':''})
            sources=(temp/'sources.txt').read_text().split(';')
            self.assertTrue(all(Path(s).is_file() for s in sources))
            self.assertFalse(any('lckfb-bk7258-xiaotai' in s for s in sources))
            self.assertFalse(any(Path(s).name in ('board_display.c','board_touch.c',
                'xiaotai_ui.c','xiaotai_touch.c') for s in sources))
            self.assertIn(str(ROOT/'boards/mgkfb/bk7258/board_camera.c'),sources)
            self.assertIn(str(ROOT/'platforms/beken/runtime/src/xiaotai_app.c'),sources)

    def test_reported_identity_and_capabilities_match_selected_board(self):
        source=(ROOT/'platforms/beken/runtime/src/xiaotai_platform_client.c').read_text()
        declaration=re.search(r'static const char s_device_profile\[\].*?;',source,re.S).group(0)
        for board,disabled in [('mgkfb-bk7258',1),('lckfb-bk7258',0)]:
            with self.subTest(board=board), tempfile.TemporaryDirectory() as tmp:
                tmp=Path(tmp)
                (tmp/'report.c').write_text('#include <stdio.h>\n' +
                    f'#define XIAOTAI_BOARD_ID "{board}"\n#define XIAOTAI_DISABLE_DEVICE_CALL {disabled}\n' +
                    '#define XIAOTAI_BUILD_VERSION "1.0.1+build.1"\n' + declaration +
                    '\nint main(void){puts(s_device_profile);}\n')
                subprocess.run(['cc',str(tmp/'report.c'),'-o',str(tmp/'report')],check=True)
                body=json.loads(subprocess.check_output([str(tmp/'report')],text=True))
                self.assertEqual(body['hardware']['board_model'],board)
                self.assertEqual(set(body['profiles']), {'stream','voip'} if disabled else {'stream','call','voip'})
                self.assertFalse(body['profiles']['stream']['no_video'])
                self.assertTrue(body['profiles']['voip']['no_video'])

    def test_board_selects_independent_smp_project(self):
        boards = load_boards(ROOT)
        board = next(b for b in boards if b.id == 'mgkfb-bk7258')
        self.assertEqual(board.data['platform'], 'beken')
        data = json.loads((ROOT / 'boards/mgkfb/bk7258/board.json').read_text())
        self.assertEqual(data['sdk'], {'name': 'bk_avdk_smp', 'version': 'release/v3.1.1.10'})
        self.assertIn({'name': 'default', 'default': True,
            'interaction_profile': 'eyes-key', 'layout_profile': 'compact'}, data['variants'])
        self.assertNotIn('touch', data['capabilities'])
        self.assertIn('camera-h264', data['capabilities'])
        self.assertEqual(str(read_board_version(ROOT, board.id)),
                         re.search(r'`([^`]+)`', (PROJECT/'VERSION.md').read_text()).group(1))
        before = (PROJECT / 'BUILD_NUMBER').read_bytes()
        result = subprocess.run([sys.executable, str(ROOT/'tools/build.py'),
            '--board', board.id, '--dry-run'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('mgkfb-bk7258-xiaotai', result.stdout)
        self.assertEqual((PROJECT/'BUILD_NUMBER').read_bytes(), before)

    def test_unsupported_public_requests_never_queue_work(self):
        source = (ROOT / 'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        funcs = '\n'.join(function(source, name) for name in (
            'decimal_string', 'xiaotai_app_request_call',
            'xiaotai_app_request_room_join', 'xiaotai_app_request_room_leave',
            'xiaotai_app_request_room_talk', 'xiaotai_app_request_room_sync'))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define XIAOTAI_DISABLE_DEVICE_CALL 1
#define XIAOTAI_DISABLE_GROUP_ROOM 1
#define BK_ERR_NOT_SUPPORT -8
#define BK_ERR_PARAM -2
enum {CONTROL_CONSOLE_CALL, CONTROL_CONSOLE_ROOM, CONSOLE_ROOM_JOIN,
 CONSOLE_ROOM_LEAVE, CONSOLE_ROOM_TALK_START, CONSOLE_ROOM_TALK_STOP,
 CONSOLE_ROOM_SYNC, XIAOTAI_CONTACT_VOIP, XIAOTAI_CONTACT_DEVICE};
static unsigned queued;
static int queue_console_event(int type,const char *text,uint32_t action,int value)
{(void)type;(void)text;(void)action;(void)value;queued++;return 0;}
''' + funcs + r'''
int main(void) {
 assert(xiaotai_app_request_call("device",false)==BK_ERR_NOT_SUPPORT);
 assert(xiaotai_app_request_room_join("012345",NULL)==BK_ERR_NOT_SUPPORT);
 assert(xiaotai_app_request_room_leave()==BK_ERR_NOT_SUPPORT);
 assert(xiaotai_app_request_room_talk(true)==BK_ERR_NOT_SUPPORT);
 assert(xiaotai_app_request_room_sync()==BK_ERR_NOT_SUPPORT);
 assert(queued==0);
 assert(xiaotai_app_request_call("wechat",true)==0 && queued==1);
 assert(decimal_string("012345",6));
}
''')

    def test_native_configuration_does_not_claim_lckfb_pins_or_screen(self):
        for core in ('ap/config/bk7258_ap', 'cp/config/bk7258'):
            config = (PROJECT/core/'config').read_text()
            if core.startswith('ap'):
                self.assertIn('CONFIG_SOC_SMP=y', config)
            else:
                self.assertIn('# CONFIG_SOC_SMP is not set', config)
            self.assertNotIn('\nCONFIG_TP=y', config)
            self.assertEqual('\nCONFIG_LCD_SPI=y' in config, core.startswith('ap'))
            self.assertNotIn('\nCONFIG_LDO3V3_CTRL_GPIO=13\n', config)
        config = (PROJECT/'ap/config/bk7258_ap/config').read_text()
        self.assertIn('CONFIG_LDO3V3_CTRL_GPIO=52', config)
        self.assertIn('CONFIG_DVP_CTRL_POWER_GPIO_ID=49', config)


if __name__ == '__main__':
    unittest.main()
