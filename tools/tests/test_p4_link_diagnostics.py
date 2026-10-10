"""Replay the hosted RX callback boundary and passive diagnostic formatter."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

HOST = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/espressif__esp_hosted/host'


class LinkDiagnostics(unittest.TestCase):
    def test_rx_handoff_counts_success_and_failure_without_changing_ownership(self):
        source = (HOST / 'drivers/transport/sdio/sdio_drv.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#define ESP_PKT_STATS 1
#define ESP_STA_IF 0
#define ESP_AP_IF 1
typedef struct {int if_type;} interface_buffer_handle_t;
static struct {atomic_uint sta_rx_out,sta_rx_fail;} pkt_stats;
static unsigned callbacks;
static int result;
static void *expected_payload,*expected_owner;
static int callback(void *channel,void *payload,void *owner,unsigned len){
 assert(channel==(void*)123 && payload==expected_payload && owner==expected_owner && len==160);
 callbacks++;return result;
}
static struct {void*api_chan;int(*rx)(void*,void*,void*,unsigned);} channel={(void*)123,callback};
static __typeof__(channel)*chan_arr[]={&channel,&channel};
''' + function(source, 'sdio_dispatch_network_rx') + r'''
int main(void){
 interface_buffer_handle_t b={.if_type=ESP_STA_IF};
 expected_payload=(void*)456;expected_owner=(void*)789;
 for(unsigned i=0;i<100;i++){
  result=0;assert(sdio_dispatch_network_rx(&b,expected_payload,expected_owner,160)==0);
  result=-7;assert(sdio_dispatch_network_rx(&b,expected_payload,expected_owner,160)==-7);
 }
 assert(callbacks==200 && pkt_stats.sta_rx_out==100 && pkt_stats.sta_rx_fail==100);
 b.if_type=ESP_AP_IF;result=0;
 assert(sdio_dispatch_network_rx(&b,expected_payload,expected_owner,160)==0);
 assert(callbacks==201 && pkt_stats.sta_rx_out==100 && pkt_stats.sta_rx_fail==100);
}
''')

    def test_stats_identify_host_boundary_and_never_claim_c6_or_delivery(self):
        source = (HOST / 'utils/stats.c').read_text()
        header = (HOST / 'utils/stats.h').read_text()
        start = header.index('struct pkt_stats_t {')
        end = header.index('};', start) + 2
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>
''' + header[start:end] + r'''
static struct pkt_stats_t pkt_stats;
static unsigned wifi_tx_throttling;
static char output[2048];
static void capture(int tag,const char*format,...){
 (void)tag;va_list ap;va_start(ap,format);
 vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,ap);va_end(ap);
}
#define TAG 0
#define ESP_LOGI capture
''' + function(source, 'stats_timer_func') + r'''
int main(void){
 pkt_stats.sta_rx_in=100;pkt_stats.sta_rx_out=90;pkt_stats.sta_rx_fail=3;
 pkt_stats.sta_tx_in_pass=70;pkt_stats.sta_tx_in_drop=2;pkt_stats.sta_tx_out=60;
 stats_timer_func(NULL);
 assert(strstr(output,"[DEBUG-link71] host") && strstr(output,"rx-valid/stack-ok/stack-fail=100/90/3"));
 assert(strstr(output,"tx-queued/drop/bus-written=70/2/60"));
 assert(!strstr(output,"slave:") && !strstr(output,"delivered"));
}
''')


if __name__ == '__main__':
    unittest.main()
