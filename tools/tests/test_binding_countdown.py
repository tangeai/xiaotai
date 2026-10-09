"""The public countdown uses the current token deadline, including replayed codes."""
import base64
import unittest
from test_s3_product_regressions import ROOT, run_c, function

class BindingCountdown(unittest.TestCase):
    def test_deadline_and_seconds_copy(self):
        token = 'e30.' + base64.urlsafe_b64encode(b'{"exp":1190,"device_id":"temporary"}').decode().rstrip('=') + '.sig'
        run_c(r'''
#include <assert.h>
#include <string.h>
#include "''' + str(ROOT / 'product/include/xiaotai_binding_countdown.h') + r'''"
int main(void){
 unsigned expiry=xiaotai_binding_token_expiry("''' + token + r'''");
 assert(expiry==1190);
 assert(xiaotai_binding_seconds_left(expiry,1000)==190);
 assert(xiaotai_binding_seconds_left(expiry,1015)==175);
 assert(xiaotai_binding_seconds_left(expiry,1190)==0);
 assert(xiaotai_binding_seconds_left(expiry,1200)==0);
 assert(xiaotai_binding_token_expiry("broken")==0);
 assert(xiaotai_binding_token_expiry("e30.e30.sig")==0);
 assert(xiaotai_binding_token_expiry(NULL)==0);
 char text[96];
 xiaotai_binding_countdown_text(text,sizeof(text),190);
 assert(!strcmp(text,"有效期剩余 190 秒"));
 xiaotai_binding_countdown_text(text,sizeof(text),1);
 assert(!strcmp(text,"有效期剩余 1 秒"));
 xiaotai_binding_countdown_text(text,sizeof(text),0);
 assert(!strcmp(text,"正在刷新验证码…"));
}
''')

    def test_platform_public_remaining_time_uses_current_deadline(self):
        paths = [
            ('firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_platform_client.c', 'xiaotai_platform'),
            ('platforms/esp-idf/components/platform_client/src/platform_client.c', 'platform_client'),
        ]
        for path, prefix in paths:
            with self.subTest(platform=prefix):
                source = (ROOT / path).read_text()
                definition = source[source.index("unsigned " + prefix + "_verification_seconds_left(void)"):]
                run_c(r'''
#include <assert.h>
#include <time.h>
#include "''' + str(ROOT / 'product/include/xiaotai_binding_countdown.h') + r'''"
static uint32_t s_verification_expiry;
static time_t now;
static time_t test_time(time_t *unused){return now;}
#define time test_time
''' + function(definition, prefix + '_verification_seconds_left') + r'''
int main(void){
 now=1000;s_verification_expiry=1190;
 assert(''' + prefix + r'''_verification_seconds_left()==190);
 now=1189;assert(''' + prefix + r'''_verification_seconds_left()==1);
 now=1200;assert(''' + prefix + r'''_verification_seconds_left()==0);
 s_verification_expiry=1390;
 assert(''' + prefix + r'''_verification_seconds_left()==190);
 s_verification_expiry=0;
 assert(''' + prefix + r'''_verification_seconds_left()==0);
}
''')

    def test_bk_refresh_is_independent_of_binding_network_and_preserves_voice_error(self):
        ui = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_ui.c'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
static bool active=true;
static unsigned seconds=190,draws;
static const char *code="012345";
static atomic_bool s_verification_voice_error;
static unsigned s_verification_displayed_seconds=~0U;
static bool xiaotai_platform_binding_active(void){return active;}
static const char *xiaotai_platform_verification_code(void){return code;}
static unsigned xiaotai_platform_verification_seconds_left(void){return seconds;}
static void present(const char *status,const char *value){
 assert(!strcmp(value,"012345"));
 assert(!strcmp(status,atomic_load(&s_verification_voice_error)?"BIND VOICE ERR":"BIND"));
 draws++;
}
''' + function(ui.read_text(), 'xiaotai_ui_refresh_verification_countdown') + r'''
int main(void){
 xiaotai_ui_refresh_verification_countdown();assert(draws==1);
 xiaotai_ui_refresh_verification_countdown();assert(draws==1);
 seconds--;atomic_store(&s_verification_voice_error,true);
 xiaotai_ui_refresh_verification_countdown();assert(draws==2);
 seconds=0;xiaotai_ui_refresh_verification_countdown();assert(draws==3);
 active=false;seconds=190;xiaotai_ui_refresh_verification_countdown();assert(draws==3);
 active=true;code="";xiaotai_ui_refresh_verification_countdown();assert(draws==3);
}
''')

if __name__ == '__main__': unittest.main()
