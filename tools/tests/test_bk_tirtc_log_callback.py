"""Exercise the TiRTC SDK log callback without a blocking console write."""

import unittest

from test_s3_product_regressions import ROOT, function, run_c


class BKTirtcLogCallback(unittest.TestCase):
    def test_release_sdk_logging_has_explicit_info_ceiling(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c').read_text()
        # Exercise the actual public start boundary's logging preamble.
        start = function(source, 'xiaotai_tirtc_start')
        begin = start.index('    TiRtcLogSetCallback(')
        end = start.index('    if (s_log_level ==', begin)
        for level in (3, 4):
            run_c(f'#define CONFIG_LOG_LEVEL {level}\n' + r'''
#include <assert.h>
static int s_log_level=-1,applied=-1;
static void tirtc_sdk_log_callback(void){}
static void TiRtcLogSetCallback(void (*cb)(void)){assert(cb==tirtc_sdk_log_callback);}
static void TiRtcLogSetLevel(int level){applied=level;}
int main(void){
''' + start[begin:end] + r'''
(void)s_log_level;assert(applied==(CONFIG_LOG_LEVEL<=3 ? 4:-1));
}
''')

    def test_sdk_log_callback_uses_nonblocking_console_and_chunks_output(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TIRTC_SDK_LOG_CHUNK_LEN 255U
#define BK_LOG_NONE 0
#define BK_LOG_RAW(...) assert(!"blocking console write from SDK callback")
static char captured[600];
static unsigned calls;
static void bk_printf_raw_nonblock(int level, char *tag, const char *format, ...)
{
    assert(level == BK_LOG_NONE && tag == NULL);
    assert(strcmp(format, "%s") == 0);
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(captured + strlen(captured),
                            sizeof(captured) - strlen(captured),
                            format, arguments);
    va_end(arguments);
    assert(written > 0 && written <= 255);
    calls++;
}
''' + function(source, 'tirtc_sdk_log_callback') + r'''
int main(void)
{
    tirtc_sdk_log_callback(NULL, 10);
    tirtc_sdk_log_callback("ignored", 0);
    assert(calls == 0);
    char message[521];
    memset(message, 'X', 520);
    message[520] = '\0';
    tirtc_sdk_log_callback(message, 520);
    assert(calls == 3);
    assert(strlen(captured) == 520);
    assert(strcmp(captured, message) == 0);
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
