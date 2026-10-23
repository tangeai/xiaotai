"""Replay the production portal loop; diagnostics must not expose request contents."""
from pathlib import Path
import unittest
from test_s3_product_regressions import function, run_c

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platforms/beken/runtime/src/xiaotai_network.c'

class PortalDiagnostics(unittest.TestCase):
    def test_startup_timing_keeps_original_order_and_reports_thread_failure(self):
        source = SOURCE.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define BK_OK 0
#define TAG "test"
#define SETUP_IP "192.168.6.1"
#define NETIF_IF_AP 1
#define MAC_TYPE_BASE 0
#define BEKEN_APPLICATION_PRIORITY 4
typedef struct {char ip[16],mask[16],gateway[16],dns[16];} netif_ip4_config_t;
typedef struct {char ssid[33],password[65];int channel;} wifi_ap_config_t;
#define WIFI_DEFAULT_AP_CONFIG() ((wifi_ap_config_t){0})
static bool s_provisioning;
static char s_ap_ssid[33], logs[4096];
static uint32_t now=1000,s_portal_start_ms;
static int stage,thread_rc;
static void log_capture(const char*t,const char*f,...){(void)t;va_list a;va_start(a,f);
 vsnprintf(logs+strlen(logs),sizeof(logs)-strlen(logs),f,a);va_end(a);}
#define BK_LOGI log_capture
#define BK_LOGW log_capture
static uint32_t rtos_get_time(void){return now;}
static int bk_get_mac(uint8_t*m,int t){(void)t;memset(m,0,6);return 0;}
static int bk_netif_set_ip4_config(int i,netif_ip4_config_t*p){(void)i;
 assert(!strcmp(p->dns,SETUP_IP));return 0;}
static int bk_wifi_ap_set_config(wifi_ap_config_t*p){assert(p->channel==1);return 0;}
static int bk_wifi_ap_start(void){assert(stage==0);stage=1;now+=20;return 0;}
static void xiaotai_ui_show_network_setup(const char*s){(void)s;assert(stage==1);stage=2;now+=250;}
static void portal_task(int a){(void)a;}
static int rtos_create_thread(void*a,int b,const char*c,void(*d)(int),int e,void*f){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;assert(stage==2);stage=3;return thread_rc;}
''' + function(source, 'start_provisioning') + r'''
int main(void){
 assert(start_provisioning()==0 && stage==3);
 assert(strstr(logs,"event=ap-started elapsed-ms=20"));
 assert(strstr(logs,"event=ui-returned elapsed-ms=270"));
 assert(strstr(logs,"event=task-submitted rc=0 elapsed-ms=270"));
 assert(start_provisioning()==0 && stage==3);
 s_provisioning=false;stage=0;thread_rc=-7;logs[0]=0;
 assert(start_provisioning()==-7);
 assert(strstr(logs,"event=task-submitted rc=-7"));
}
''')

    def test_phone_probes_and_wifi_submission_are_observable_without_secrets(self):
        source = SOURCE.read_text()
        # Exercise the real accept/routing/response loop, not only a logging helper.
        names = ['send_response', 'send_redirect', 'is_captive_probe', 'portal_task']
        helper = ''
        if 'static const char *portal_request_label(' in source:
            helper = function(source, 'portal_request_label')
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#define TAG "test"
#define SETUP_IP "192.168.6.1"
#define HTTP_PORT 80
#define HTTP_REQUEST_MAX 1024
#define PORTAL_IO_TIMEOUT_SECONDS 5
#define BK_OK 0
typedef int beken_thread_arg_t;
static char logs[16384], wire[16384];
static unsigned now=1000;
static uint32_t s_portal_start_ms=900;
static char s_ap_ssid[]="XiaoTai-test";
static const char s_setup_page[]="SETUP PAGE";
static jmp_buf finished;
static int index_request=-1, fail_send;
static const char *requests[]={
 "GET /hotspot-detect.html HTTP/1.1\r\nHost: captive.apple.com\r\n\r\n",
 "GET / HTTP/1.1\r\n\r\n",
 "POST /api/wifi HTTP/1.1\r\nAuthorization: SECRET_HEADER\r\n\r\nSECRET_PASSWORD",
 "GET /private-SECRET_PATH?token=SECRET_QUERY HTTP/1.1\r\n\r\n",
 NULL};
static void capture_log(const char *tag,const char *fmt,...){
 (void)tag;va_list ap;va_start(ap,fmt);
 vsnprintf(logs+strlen(logs),sizeof(logs)-strlen(logs),fmt,ap);va_end(ap);
}
#define BK_LOGI capture_log
#define BK_LOGW capture_log
#define BK_LOGE capture_log
static unsigned rtos_get_time(void){return now++;}
static int fake_socket(int a,int b,int c){(void)a;(void)b;(void)c;return 3;}
static int fake_setsockopt(int a,int b,int c,const void*d,unsigned e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
static int fake_bind(int a,const struct sockaddr*b,unsigned c){(void)a;(void)b;(void)c;return 0;}
static int fake_listen(int a,int b){(void)a;(void)b;return 0;}
static int fake_accept(int a,void*b,void*c){(void)a;(void)b;(void)c;
 if(!requests[++index_request]){longjmp(finished,1);}return 4;}
static int close(int fd){(void)fd;return 0;}
static bool send_all(int fd,const void *data,size_t n){(void)fd;
 if(fail_send){return false;}strncat(wire,data,n);return true;}
static bool receive_request(int fd,char*b,size_t cap,size_t*n){(void)fd;
 snprintf(b,cap,"%s",requests[index_request]);*n=strlen(b);return true;}
static int start_portal_scan(void){return 0;}
static char *scan_json(void){return NULL;}
static void psram_free(void*p){(void)p;}
static bool save_request_wifi(char*p){(void)p;return false;}
static void rtos_delay_milliseconds(int n){(void)n;}
static void bk_reboot(void){}
static void rtos_delete_thread(void*p){(void)p;}
#define socket fake_socket
#define setsockopt fake_setsockopt
#define bind fake_bind
#define listen fake_listen
#define accept fake_accept
''' + helper + '\n'.join(function(source,n) for n in names) + r'''
int main(void){
 if(!setjmp(finished))portal_task(0);
 assert(strstr(logs,"event=ready"));
 assert(strstr(logs,"request=apple-probe"));
 assert(strstr(logs,"request=page"));
 assert(strstr(logs,"request=wifi-save"));
 assert(strstr(logs,"request=other"));
 assert(strstr(logs,"status=302 sent=1"));
 assert(strstr(logs,"status=200 OK sent=1"));
 assert(strstr(logs,"status=400 Bad Request sent=1"));
 assert(!strstr(logs,"SECRET_"));
 assert(strstr(wire,"Location: http://192.168.6.1/\r\n"));
 assert(strstr(wire,"SETUP PAGE"));
 logs[0]=0;fail_send=1;send_redirect(4);
 assert(strstr(logs,"status=302 sent=0"));
 logs[0]=0;send_response(4,"200 OK","text/plain","SECRET_BODY");
 assert(strstr(logs,"status=200 OK sent=0"));
 assert(!strstr(logs,"SECRET_"));
 (void)s_portal_start_ms;(void)rtos_get_time;
}
''')

if __name__ == '__main__':
    unittest.main()
