"""Check the serial EP override at its public command and storage seams."""

import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c


P4_CONFIG = ROOT / "platforms/esp-idf/components/runtime_config/src/runtime_config.c"
P4_CONSOLE = ROOT / "platforms/esp-idf/components/starter_console/src/starter_console.c"
P4_START = ROOT / "platforms/esp-idf/main/app_main.c"
P4_SDK = ROOT / "platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"
BK_STORAGE = ROOT / "firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_storage.c"
BK_CONSOLE = ROOT / "firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_console.c"
BK_APP = ROOT / "firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_app.c"


class SerialEndpointOverride(unittest.TestCase):
    def test_both_validators_accept_path_and_reject_malformed_urls(self):
        p4 = function(P4_CONFIG.read_text(), "runtime_config_endpoint_valid")
        bk = function(BK_STORAGE.read_text(), "endpoint_valid")
        for name, source, limit in (("p4", p4, "RUNTIME_TIRTC_ENDPOINT_MAX"),
                                    ("bk", bk, "XIAOTAI_ENDPOINT_MAX")):
            with self.subTest(board=name):
                run_c(f'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define {limit} 256
{source}
int main(void) {{
    assert({name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://ep-open.tangeopen.com"));
    assert({name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://ep-open.tangeopen.com/pre"));
    assert({name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("https://ep.example.com:8443"));
    assert({name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("https://ep.example.com:8443/pre/v1"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}(""));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("ftp://ep.example.com"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://user@host"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host/pre?token=x"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host/pre#fragment"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host/pre path"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host:bad"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host:0"));
    assert(!{name == 'p4' and 'runtime_config_endpoint_valid' or 'endpoint_valid'}("http://host:65536"));
}}
''')

    def test_p4_public_storage_round_trip_and_reset(self):
        source = P4_CONFIG.read_text()
        functions = "\n".join(function(source, name) for name in (
            "runtime_config_endpoint_valid", "runtime_config_load_endpoint",
            "runtime_config_save_endpoint", "runtime_config_clear_endpoint"))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define RUNTIME_TIRTC_ENDPOINT_MAX 256
#define TIRTC_ENDPOINT_NVS_NAMESPACE "tirtc_ep"
#define TIRTC_ENDPOINT_NVS_KEY "url"
#define NVS_READONLY 1
#define NVS_READWRITE 2
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_INVALID_ARG 2
typedef int esp_err_t;
typedef int nvs_handle_t;
static char stored[256];
static int commits;
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "tirtc_ep"));
    if (mode == NVS_READONLY && !stored[0]) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; return ESP_OK;
}
static esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *size) {
    assert(!strcmp(key,"url"));
    if (!stored[0]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < strlen(stored)+1) return ESP_ERR_INVALID_ARG;
    strcpy(out,stored); return ESP_OK;
}
static esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) {
    assert(!strcmp(key,"url")); strcpy(stored,value); return ESP_OK;
}
static esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) {
    stored[0]=0; return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) { commits++; return ESP_OK; }
static void nvs_close(nvs_handle_t h) {}
''' + functions + r'''
int main(void) {
    char out[256];
    assert(runtime_config_load_endpoint(out,sizeof(out))==ESP_ERR_NVS_NOT_FOUND && !out[0]);
    assert(runtime_config_save_endpoint("http://ep-open.tangeopen.com/pre")==ESP_OK);
    assert(commits==1);
    assert(runtime_config_load_endpoint(out,sizeof(out))==ESP_OK);
    assert(!strcmp(out,"http://ep-open.tangeopen.com/pre"));
    assert(runtime_config_save_endpoint("ftp://bad")==ESP_ERR_INVALID_ARG);
    assert(runtime_config_clear_endpoint()==ESP_OK && commits==2);
    assert(runtime_config_load_endpoint(out,sizeof(out))==ESP_ERR_NVS_NOT_FOUND && !out[0]);
}
''')

    def test_bk_public_storage_round_trip_and_reset(self):
        source = BK_STORAGE.read_text()
        functions = "\n".join(function(source, name) for name in (
            "endpoint_valid", "xiaotai_storage_load_endpoint",
            "xiaotai_storage_save_endpoint", "xiaotai_storage_clear_endpoint"))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define XIAOTAI_ENDPOINT_MAX 256
#define XIAOTAI_ENDPOINT_KEY "xiaotai_ep_v1"
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_PARAM -2
static char stored[256];
static int saves, clears;
static int storage_write_blob(const char *key,const void *value,size_t size) {
    assert(!strcmp(key,"xiaotai_ep_v1")); memcpy(stored,value,size); saves++; return BK_OK;
}
static int storage_delete(const char *key) {
    assert(!strcmp(key,"xiaotai_ep_v1")); stored[0]=0; clears++; return BK_OK;
}
static size_t ef_get_env_blob(const char *key,void *out,size_t capacity,size_t *saved) {
    if (!stored[0]) return 0;
    *saved=strlen(stored)+1; memcpy(out,stored,*saved); return *saved;
}
''' + functions + r'''
int main(void) {
    char out[256];
    assert(xiaotai_storage_load_endpoint(out,sizeof(out))==BK_FAIL && !out[0]);
    assert(xiaotai_storage_save_endpoint("http://ep-open.tangeopen.com/pre")==BK_OK && saves==1);
    assert(xiaotai_storage_load_endpoint(out,sizeof(out))==BK_OK);
    assert(!strcmp(out,"http://ep-open.tangeopen.com/pre"));
    assert(xiaotai_storage_save_endpoint("ftp://bad")==BK_ERR_PARAM && saves==1);
    assert(xiaotai_storage_clear_endpoint()==BK_OK && clears==1);
    assert(xiaotai_storage_load_endpoint(out,sizeof(out))==BK_FAIL && !out[0]);
}
''')

    def test_serial_commands_persist_and_reboot(self):
        p4 = P4_CONSOLE.read_text()
        bk = BK_CONSOLE.read_text()
        for name in ("ep-show", "ep-set", "ep-reset"):
            self.assertIn(f'register_command("{name}"', p4)
            self.assertIn(f'{{"{name}",', bk)
        self.assertNotIn('register_command("ep",', p4)
        self.assertNotIn('{"ep",', bk)
        self.assertIn("runtime_config_save_endpoint(argv[1])", p4)
        self.assertIn("runtime_config_clear_endpoint()", p4)
        self.assertIn("xiaotai_storage_save_endpoint(argv[1])", bk)
        self.assertIn("xiaotai_storage_clear_endpoint()", bk)

    def test_boot_override_reaches_sdk_and_reset_restores_default(self):
        p4_start = P4_START.read_text()
        self.assertIn("runtime_config_load_endpoint(s_tirtc_endpoint_override", p4_start)
        self.assertIn(".service_endpoint = endpoint_err == ESP_OK ? s_tirtc_endpoint_override : NULL", p4_start)
        self.assertIn("TIRTC_OPT_SERVICE_ENDPOINT, config->service_endpoint", P4_SDK.read_text())
        bk_app = BK_APP.read_text()
        self.assertIn("xiaotai_storage_load_endpoint(endpoint_override", bk_app)
        self.assertIn("snprintf(s_tirtc_endpoint, sizeof(s_tirtc_endpoint)", bk_app)
        self.assertIn("XIAOTAI_ENDPOINT_KEY", BK_STORAGE.read_text())


if __name__ == "__main__":
    unittest.main()
