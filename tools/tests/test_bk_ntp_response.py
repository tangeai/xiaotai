"""Exercise the real BK clock synchronization path with malformed peers/replies."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class BkNtpResponse(unittest.TestCase):
    def test_response_validation_fallback_and_socket_failures(self):
        s=(ROOT/'platforms/beken/runtime/src/xiaotai_platform_client.c').read_text()
        run_c(r'''
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#if __has_include("xiaotai_ntp_response.h")
#include "xiaotai_ntp_response.h"
#endif
#define BK_OK 0
#define BK_FAIL -1
#define XIAOTAI_NTP_SERVER_MAX 254
#define NTP_UNIX_EPOCH_DELTA 2208988800UL
#define BK_LOGE(...) ((void)0)
#define BK_LOGI(...) ((void)0)
static bool s_clock_synchronized;
static time_t clock_value;
static int scenario,attempts,connected,sends,closes,random_calls;
static unsigned char request_nonce[8];
static time_t xiaotai_time_now(void){return clock_value;}
static int bk_rtc_settimeofday(const struct timeval *v,void *tz){clock_value=v->tv_sec;return 0;}
static int bk_fill_rand(void *out,size_t n){memset(out,++random_calls,n);return scenario==8?-1:0;}
static int xiaotai_storage_load_ntp_server(char *out,size_t cap){snprintf(out,cap,"time.lan");return 0;}
static int resolve(const char*a,const char*b,const struct addrinfo*c,struct addrinfo**out){
 static struct addrinfo addr={.ai_family=AF_INET,.ai_socktype=SOCK_DGRAM};*out=&addr;attempts++;return 0;
}
static int open_socket(int a,int b,int c){connected=0;return 5;}
static void free_addr(struct addrinfo *a){}
static int connect_peer(int a,const struct sockaddr*b,socklen_t c){
 if(scenario==6 && attempts==1)return -1;
 connected=1;return 0;
}
static int set_opt(int a,int b,int c,const void*d,socklen_t e){return scenario==7 && attempts==1?-1:0;}
static ssize_t send_packet(int a,const void*b,size_t n,int c){sends++;memcpy(request_nonce,(const char*)b+40,8);return n;}
static ssize_t send_to(int a,const void*b,size_t n,int c,const struct sockaddr*d,socklen_t e){return send_packet(a,b,n,c);}
static ssize_t receive_packet(int a,void *b,size_t n,int c){
 unsigned char *p=b;memset(p,0,n);p[0]=0x24;p[1]=2;
 memcpy(p+24,request_nonce,8);p[32]=1;
 uint32_t ts=2208988800U+1800000000U;
 p[40]=ts>>24;p[41]=ts>>16;p[42]=ts>>8;p[43]=ts;
 if(scenario==2 || (scenario==1 && attempts==1))p[0]=0xe4;
 if(scenario==3 && attempts==1)p[1]=0;
 if(scenario==4 && attempts==1)p[24]^=0xff;
 if(scenario==5 && attempts==1)p[0]=0x23;
 if(scenario==9 && attempts==1)p[1]=16;
 if(scenario==10 && attempts==1)p[0]=0x14;
 if(scenario==11 && attempts==1)return 47;
 if(scenario==12 && attempts==1)memset(p+40,0,8);
 if(scenario==13 && attempts==1)memset(p+32,0,8);
 return n;
}
static int close_socket(int a){closes++;return 0;}
#define getaddrinfo resolve
#define freeaddrinfo free_addr
#define socket open_socket
#define setsockopt set_opt
#define connect connect_peer
#define send send_packet
#define sendto send_to
#define recv receive_packet
#define close close_socket
''' + function(s,'synchronize_clock') + r'''
int main(void){
 for(scenario=0;scenario<=13;scenario++){
  s_clock_synchronized=false;clock_value=0;attempts=sends=closes=random_calls=0;
  int rc=synchronize_clock();
  if(scenario==2 || scenario==8)assert(rc==BK_FAIL && !s_clock_synchronized && clock_value==0);
  else assert(rc==BK_OK && s_clock_synchronized && clock_value==1800000000);
  assert(attempts==(scenario==0?1:2));
  assert(closes==attempts);
  if(rc==BK_OK){assert(connected);int before=attempts;assert(synchronize_clock()==BK_OK && attempts==before);}
 }
}
''',[ROOT/'platforms/common/include'],cflags=['-Wno-unused-variable'])

if __name__=='__main__':unittest.main()
