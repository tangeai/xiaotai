"""Run the build-selected SDK DNS patches against actual AP/CP request handling."""
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import function, run_c

ROOT = Path(__file__).resolve().parents[2]


class BekenCaptiveDNS(unittest.TestCase):
    def test_both_cores_resolve_phone_probe_hosts_to_the_portal(self):
        fixture = (ROOT/'tools/tests/fixtures/bk7258_captive_dns.c').read_text()
        build = (ROOT/'platforms/beken/tools/build.sh').read_text()
        patches = re.findall(r'"\$support_dir/(tools/patches/bk-avdk-(?:cp-)?captive-dns\.patch)"', build)
        self.assertTrue(patches)
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            for core in ('ap', 'cp'):
                path = sdk/core/'components/lwip_intf_v2_1/dhcpd/dhcp-server.c'
                path.parent.mkdir(parents=True)
                path.write_text(fixture)
            for patch in patches:
                subprocess.run(['patch', '--batch', '-p1', '-i', str(ROOT/'platforms/beken'/patch)],
                               cwd=sdk, check=True, capture_output=True)
            for core in ('ap', 'cp'):
                with self.subTest(core=core):
                    source = (sdk/core/'components/lwip_intf_v2_1/dhcpd/dhcp-server.c').read_text()
                    defines = '\n'.join(line for line in source.splitlines() if line.startswith('#define'))
                    queries = []
                    for host in ('captive.apple.com', 'connectivitycheck.gstatic.com',
                                 'www.msftconnecttest.com'):
                        labels = b''.join(bytes([len(s)])+s.encode() for s in host.split('.'))+b'\0'
                        query = struct.pack('!HHHHHH', 0x1234, 0x0100, 1, 0, 0, 0)+labels+struct.pack('!HH', 1, 1)
                        queries.append('{'+','.join(str(b) for b in query)+'}')
                    run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#define dhcp_d(...) ((void)0)
#define dhcp_e(...) ((void)0)
#define SERVER_BUFFER_SIZE 512
struct dns_header {
 uint16_t id;
 union { struct { uint16_t rcode:4,cd:1,ad:1,z:1,ra:1,rd:1,tc:1,aa:1,opcode:4,qr:1; } fields; uint16_t num; } flags;
 uint16_t num_questions,answer_rrs,authority_rrs,additional_rrs;
} __attribute__((packed));
static struct {int dnssock;uint32_t router_ip;} dhcps;
static unsigned char captured[512];static int captured_size;
static int capture(int fd,struct sockaddr *from,char *msg,int len){
 (void)fd;(void)from;assert(len>0 && len<=512);
 memcpy(captured,msg,(size_t)len);captured_size=len;return 0;
}
#define SEND_RESPONSE capture
''' + defines + '\n' + function(source, 'process_dns_message') + r'''
int main(void){
 unsigned char queries[3][512]={''' + ','.join(queries) + r'''};
 dhcps.router_ip=inet_addr("192.168.6.1");
 for(unsigned i=0;i<3;i++){
  unsigned end=12;while(queries[i][end])end+=1+queries[i][end];end+=5;
  captured_size=0;
  int rc=process_dns_message((char *)queries[i],(int)end,NULL);
  assert(captured_size>=12 && (captured[3]&15)==0);
  assert(rc==0 && captured_size==(int)end+16);
  assert(captured[0]==0x12 && captured[1]==0x34);
  assert((captured[3]&15)==0 && captured[7]==1);
  assert(memcmp(captured+captured_size-4,&dhcps.router_ip,4)==0);
  /* AAAA receives an empty successful answer, not REFUSED or a bogus IPv4 RR. */
  queries[i][2]=1;queries[i][3]=0;queries[i][7]=0;
  queries[i][end-3]=28;captured_size=0;
  assert(process_dns_message((char *)queries[i],(int)end,NULL)==0);
  assert(captured_size==(int)end && (captured[3]&15)==0 && captured[7]==0);
 }
 captured_size=0;
 assert(process_dns_message((char *)queries[0],8,NULL)==-1 && captured_size==0);
}
''')


if __name__ == '__main__':
    unittest.main()
