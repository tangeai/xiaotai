"""Read-only board probe and allowlisted SDK transport logging contracts."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
P4=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware'
SDK=ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c'
class Probe(unittest.TestCase):
 def test_transport_formats_filter_before_varargs_and_log_only_numbers(self):
  code=function(SDK.read_text(),'sdk_transport_log')
  run_c(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static const char *TAG="probe";
static char output[512];
#define ESP_LOGI(tag,...) do{(void)(tag);snprintf(output,sizeof(output),__VA_ARGS__);}while(0)
#define ESP_LOGE ESP_LOGI
''' + code + r'''
static void emit(const char *fmt,...) {va_list ap;va_start(ap,fmt);sdk_transport_log(fmt,ap);va_end(ap);}
int main(void) {
 emit(NULL);emit("unknown secret %s");assert(!output[0]);
 emit("(%s:%d) peer_connection_tgtrp_init ok ice=%p mtu=%u poll_segments=%u pacing=%u send_buffer_max=%zu",
 "secret",12,(void*)1,1200U,8U,99U,(size_t)2097152);
 assert(strstr(output,"proto=TGMP") && strstr(output,"mtu=1200") && strstr(output,"poll_segments=8") && strstr(output,"2097152"));
 assert(!strstr(output,"secret") && !strstr(output,"ice="));
 emit("(%s:%d) peer_connection_kcp_init ikcp_nodelay(%d %d %d %d) sndwnd=%d mtu=%d",
 "secret",1,1,20,2,1,128,1200);
 assert(strstr(output,"proto=KCP") && strstr(output,"interval_ms=20"));
 emit("(%s:%d) peer_connection_tgtrp_init failed ice=%p ret=%d mtu=%u poll_segments=%u send_buffer_max=%zu",
 "secret",1,(void*)1,-35,1200U,8U,(size_t)99);
 assert(strstr(output,"init-failed") && strstr(output,"rc=-35"));
}
''')
 def test_board_probe_refuses_active_media_and_keeps_unknown_identity_unknown(self):
  code=function((P4/'p4_board_probe.c').read_text(),'command_board_probe')
  run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
static char printed[8192];
static int capture(const char *fmt,...) {va_list ap;va_start(ap,fmt);int n=vsnprintf(printed+strlen(printed),sizeof(printed)-strlen(printed),fmt,ap);va_end(ap);return n;}
#define printf capture
#define ESP_OK 0
#define WIFI_IF_STA 0
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 360
#define MALLOC_CAP_SPIRAM 1
#define CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH 4
#define CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ 40000
#define CONFIG_ESP_HOSTED_SDIO_PIN_CLK 18
#define CONFIG_ESP_HOSTED_SDIO_PIN_CMD 19
#define CONFIG_ESP_HOSTED_SDIO_PIN_D0 14
#define CONFIG_ESP_HOSTED_SDIO_PIN_D1 15
#define CONFIG_ESP_HOSTED_SDIO_PIN_D2 16
#define CONFIG_ESP_HOSTED_SDIO_PIN_D3 17
#define CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE 54
static bool active,online=true;static unsigned rpc_calls;static int rpc_error;static unsigned cp_major;
typedef struct {int model,revision,cores;} esp_chip_info_t;
typedef struct {unsigned major1,minor1,patch1;} esp_hosted_coprocessor_fwver_t;
typedef struct {int rssi,primary;} wifi_ap_record_t;
static bool starter_tirtc_connected(void){return active;}
static bool wifi_manager_connected(void){return online;}
static void esp_chip_info(esp_chip_info_t *p){*p=(esp_chip_info_t){18,100,2};}
static int esp_flash_get_size(void *p,uint32_t *s){(void)p;*s=33554432;return 0;}
static size_t heap_caps_get_total_size(int c){(void)c;return 8388608;}
static const char *esp_get_idf_version(void){return "test-idf";}
static int esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *v){rpc_calls++;*v=(esp_hosted_coprocessor_fwver_t){cp_major,4,7};if(!cp_major)*v=(esp_hosted_coprocessor_fwver_t){0};return rpc_error;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *p){*p=(wifi_ap_record_t){-55,6};return 0;}
static int esp_wifi_get_protocol(int i,uint8_t *p){(void)i;*p=7;return 0;}
''' + code + r'''
int main(void){
 active=true;assert(command_board_probe(1,NULL)==1 && rpc_calls==0);
 active=false;assert(command_board_probe(2,NULL)==1 && rpc_calls==0);
 online=false;assert(command_board_probe(1,NULL)==0 && rpc_calls==0);
 online=true;rpc_error=-1;assert(command_board_probe(1,NULL)==0 && rpc_calls==1);
 rpc_error=0;printed[0]=0;assert(command_board_probe(1,NULL)==0 && rpc_calls==2);
 assert(strstr(printed,"version=unknown rc=0") && !strstr(printed,"version=0.0.0"));
 assert(strstr(printed,"carrier=unverified") && strstr(printed,"capability setting, not negotiated PHY"));
 cp_major=1;printed[0]=0;assert(command_board_probe(1,NULL)==0 && rpc_calls==3);
 assert(strstr(printed,"version=1.4.7; not a full app identity"));
}
''')
if __name__=='__main__':unittest.main()
