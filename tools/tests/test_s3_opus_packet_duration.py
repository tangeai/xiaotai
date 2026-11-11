"""Variable Opus output lengths must reach the board without truncation."""
import re
import unittest
from unittest.mock import patch
import test_s3_opus_media as fixture
from test_s3_product_regressions import MEDIA, function, run_c


class S3OpusPacketDuration(unittest.TestCase):
    def replay(self, invalid=False):
        captured=[]
        with patch.object(fixture, 'run_c', captured.append):
            fixture.S3OpusMedia().check_downlink(g711=True)
        code=captured[0].split('int main(void)')[0]
        source=MEDIA.read_text()
        declaration=re.search(r'static int16_t s_decode_pcm\[[^;]+;', source)[0]
        code=code.replace('static int16_t s_decode_pcm[1500],s_play_stereo[6000];',
                          declaration+'\nstatic int16_t s_play_stereo[6000];')
        code=code.replace('static size_t alaw_samples=160;',
                          'static size_t alaw_samples=160,opus_samples=320;static bool partial,bad_format,odd_output;')
        code=code.replace('size_t samples=opus ? 320 : alaw_samples;', 'size_t samples=opus ? opus_samples : alaw_samples;')
        code=code.replace('assert(o->len>=samples*2);', 'if(o->len<samples*2)return -1;')
        code=code.replace('info->channel=1;info->bits_per_sample=16;',
                          'o->decoded_size+=odd_output;info->channel=bad_format?2:1;info->bits_per_sample=16;')
        code=code.replace('i->consumed=i->len;', 'i->consumed=i->len-(partial?1:0);')
        code=code.replace('mode==STARTER_TIRTC_AI ? 320 : alaw_samples*2',
                          'mode==STARTER_TIRTC_AI ? opus_samples : alaw_samples*2')
        code=code.replace('if(mode==STARTER_TIRTC_AI)assert(pcm[2]==101 && pcm[3]==101);',
            'if(mode==STARTER_TIRTC_AI){for(size_t n=0;n<frames;n++)assert(pcm[2*n]==(int16_t)(100+n) && pcm[2*n+1]==pcm[2*n]);}')
        main=r'''
int main(void){
 audio_rx_item_t item={.mode=STARTER_TIRTC_AI,.generation=7,.frame.length=37};
 unsigned durations[]={40,80,160,320,640,960,1280,1600,1920};
 for(unsigned i=0;i<sizeof(durations)/sizeof(*durations);i++){
  opus_samples=durations[i];assert(play(&item) && "valid Opus samples must not be dropped");
 }
 assert(writes==9);
}
'''
        if invalid:
            main=r'''
int main(void){
 audio_rx_item_t item={.mode=STARTER_TIRTC_AI,.generation=7,.frame.length=37};
 partial=true;assert(!play(&item) && "partial Opus input cannot be reported as full playback");
 partial=false;bad_format=true;assert(!play(&item));
 bad_format=false;odd_output=true;assert(!play(&item));
 assert(writes==0 && s_audio_decode_failed==3);
}
'''
        run_c(code+main)

    def test_all_packet_durations_reach_output(self):
        self.replay()

    def test_incomplete_or_invalid_decoder_output_is_not_silent_success(self):
        self.replay(invalid=True)


if __name__=='__main__':unittest.main()
