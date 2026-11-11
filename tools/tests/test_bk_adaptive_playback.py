"""Deterministic playback policy tests, including a one-packet reply."""
import unittest
from test_s3_product_regressions import ROOT, run_c

INCLUDE = ROOT / 'platforms/beken/runtime/include'
POLICY = ROOT / 'platforms/beken/runtime/src/xiaotai_audio_policy.c'


class AdaptivePlayback(unittest.TestCase):
    def replay(self, body):
        run_c('#include <assert.h>\n#include <stdio.h>\n' + POLICY.read_text() +
              '\nint main(void) {\n' + body + '\n}\n', [INCLUDE])

    def test_one_packet_without_end_has_bounded_wait(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    assert(!xiaotai_audio_buffer_ready(&p, 1000, 20, 1000, 1000, false));
    assert(!xiaotai_audio_buffer_ready(&p, 1399, 20, 1000, 1000, false));
    assert(xiaotai_audio_buffer_ready(&p, 1400, 20, 1000, 1000, false));
    assert(p.target_ms == 100);
''')

    def test_short_reply_end_before_or_after_audio_and_empty_timeout(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    for (unsigned t=0; t<10000; t+=5)
        assert(!xiaotai_audio_buffer_ready(&p,t,0,0,0,false));
    assert(p.target_ms==100 && p.underruns==0 && p.timeout_releases==0);
    assert(!xiaotai_audio_buffer_ready(&p,10000,20,10000,0,false));
    assert(xiaotai_audio_buffer_ready(&p,10005,20,10000,0,true));
    assert(!xiaotai_audio_buffer_ready(&p,10025,0,0,10025,true));
    /* The command can overtake the only audio packet. Keep accepting tail. */
    assert(xiaotai_audio_buffer_ready(&p,10040,20,10040,10025,true));
    xiaotai_audio_buffer_begin_round(&p);
    assert(!xiaotai_audio_buffer_ready(&p,11000,20,11000,10060,false));
    assert(xiaotai_audio_buffer_ready(&p,11400,20,11000,10060,false));
    assert(p.underruns==0);
''')

    def test_recovery_threshold_persists_across_polls(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    p.target_ms=60;
    assert(xiaotai_audio_buffer_ready(&p,0,100,0,0,false));
    assert(!xiaotai_audio_buffer_ready(&p,105,0,0,120,false));
    assert(p.playing); /* Queue empty while hardware still plays. */
    assert(!xiaotai_audio_buffer_ready(&p,145,0,0,120,false));
    assert(!xiaotai_audio_buffer_ready(&p,150,20,150,120,false));
    assert(p.target_ms==100 && p.underruns==1);
    assert(!xiaotai_audio_buffer_ready(&p,200,100,150,120,false));
    assert(xiaotai_audio_buffer_ready(&p,220,140,150,120,false));
    assert(p.underruns==1);
    assert(xiaotai_audio_buffer_ready(&p,240,20,230,240,false));
''')

    def test_caps_stable_media_time_long_gaps_and_clock_wrap(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    unsigned now=100;
    for(unsigned i=0;i<20;i++) {
        assert(xiaotai_audio_buffer_ready(&p,now,300,now,now,false));
        assert(!xiaotai_audio_buffer_ready(&p,now+40,0,0,now,false));
        assert(!xiaotai_audio_buffer_ready(&p,now+60,20,now+60,now,false));
        now+=1000;
    }
    assert(p.target_ms==300 && p.underruns==20);
    for(unsigned i=0;i<500;i++) xiaotai_audio_buffer_written(&p,20);
    assert(p.target_ms==280);
    for(unsigned i=0;i<10000;i++) xiaotai_audio_buffer_written(&p,20);
    assert(p.target_ms==60);
    xiaotai_audio_buffer_reset(&p);
    assert(xiaotai_audio_buffer_ready(&p,0,100,0,0,false));
    assert(!xiaotai_audio_buffer_ready(&p,100,0,0,20,false));
    assert(!xiaotai_audio_buffer_ready(&p,5000,20,5000,20,false));
    assert(p.target_ms==100 && p.underruns==0);
    xiaotai_audio_buffer_reset(&p);
    assert(!xiaotai_audio_buffer_ready(&p,UINT32_MAX-99,20,UINT32_MAX-99,0,false));
    assert(!xiaotai_audio_buffer_ready(&p,299,20,UINT32_MAX-99,0,false));
    assert(xiaotai_audio_buffer_ready(&p,300,20,UINT32_MAX-99,0,false));
''')

    def test_timeout_survives_changing_queue_head(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    p.target_ms=300;
    assert(!xiaotai_audio_buffer_ready(&p,1000,20,1000,0,false));
    /* Eviction/new arrivals must not restart this wait forever. */
    assert(!xiaotai_audio_buffer_ready(&p,1399,160,1390,0,false));
    assert(xiaotai_audio_buffer_ready(&p,1400,160,1395,0,false));
    assert(p.timeout_releases==1);
''')

    def test_replay_reduces_gaps_without_losing_short_tail(self):
        self.replay(r'''
    unsigned simulate(bool adaptive) {
        xiaotai_audio_buffer_t p;
        xiaotai_audio_buffer_reset(&p);
        unsigned arrival[101];
        for(unsigned i=0;i<101;i++)
            arrival[i]=i<25 ? i*20 : 500+((i-25)/5)*100+(((i-25)/5)%2 ? 60:0);
        unsigned received=0,played=0,tail=0,gap_ms=0,first=0;
        for(unsigned t=0;t<4000;t+=5) {
            while(received<101 && arrival[received]<=t) ++received;
            while((int32_t)(tail-t)<40) {
                bool ready = adaptive ? xiaotai_audio_buffer_ready(&p,t,
                    (received-played)*20,played<received?arrival[played]:t,
                    tail,false) : played<received;
                if(!ready || played==received) break;
                if(played==0) first=t;
                ++played;
                tail=xiaotai_audio_playback_tail_after_write(tail,t,20);
                xiaotai_audio_buffer_written(&p,20);
            }
            if(played && played<101 && t>=tail) gap_ms+=5;
        }
        assert(played==101); /* Last isolated packet has no end command. */
        assert(first<=400 && p.target_ms<=300);
        return gap_ms;
    }
    unsigned immediate=simulate(false),adaptive=simulate(true);
    printf("Deterministic jitter replay: immediate gap=%u ms adaptive gap=%u ms\n",immediate,adaptive);
    assert(immediate>0 && adaptive<immediate);
''')

    def test_delayed_worker_observes_gap_from_arrival_without_empty_poll(self):
        self.replay(r'''
    xiaotai_audio_buffer_t p;
    xiaotai_audio_buffer_reset(&p);
    assert(xiaotai_audio_buffer_ready(&p,0,100,0,0,false));
    /* Sink was waiting in receive; data arrived after the hardware tail. */
    assert(!xiaotai_audio_buffer_ready(&p,200,20,180,100,false));
    assert(p.target_ms==140 && p.underruns==1);
    assert(xiaotai_audio_buffer_ready(&p,580,20,180,100,false));
''')


if __name__ == '__main__':
    unittest.main()
