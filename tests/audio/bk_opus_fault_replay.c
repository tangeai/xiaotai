/* Linker-wrapped real decoder: inject failures after state has advanced. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <modules/opus.h>
#include "xiaotai_opus_plc.h"

enum { FRAME=320, CAPACITY=960 };
static int calls, fail_call;
static int kinds[8];
int __real_opus_decode(OpusDecoder *, const unsigned char *, opus_int32,
                       opus_int16 *, int, int);
int __wrap_opus_decode(OpusDecoder *decoder, const unsigned char *data,
                       opus_int32 len, opus_int16 *pcm, int capacity, int fec)
{
    ++calls;
    if (calls<=8) kinds[calls-1]=data ? (fec ? 1 : 2) : 0;
    int rc=__real_opus_decode(decoder,data,len,pcm,capacity,fec);
    return calls==fail_call ? OPUS_INTERNAL_ERROR : rc;
}

static const unsigned char packet[]={0xf8,0xff,0xfe};
static xiaotai_opus_stamp_t stamp(unsigned i)
{
    return (xiaotai_opus_stamp_t){.media_ms=i*20U,.rx_ms=100+i,
        .epoch=1,.samples=FRAME,.stream=14};
}

int main(void)
{
    for (int failure=1; failure<=3; ++failure) {
        int error;
        OpusDecoder *d=opus_decoder_create(16000,1,&error);
        assert(d && error==OPUS_OK);
        xiaotai_opus_plc_t state={0};
        short pcm[CAPACITY];
        fail_call=0;
        for (unsigned i=0; i<4; ++i) {
            xiaotai_opus_stamp_t s=stamp(i);
            assert(xiaotai_opus_plc_decode(&state,d,&s,packet,sizeof(packet),pcm,CAPACITY)==FRAME);
        }
        calls=0; fail_call=failure;
        xiaotai_opus_stamp_t s=stamp(6);
        int n=xiaotai_opus_plc_decode(&state,d,&s,packet,sizeof(packet),pcm,CAPACITY);
        assert(n==(failure==1 ? OPUS_INTERNAL_ERROR : (failure-1)*FRAME));
        assert(calls==failure);
        for (int i=0; i<failure; ++i) assert(kinds[i]==i);
        assert(state.output_concealed_samples==(failure-1)*FRAME);
        assert(state.timeline.desync);
        /* Successfully emitted prefixes are permanently consumed even after
         * the following failed operation. A stale frame cannot replay them. */
        fail_call=0;
        if (failure>1) {
            s=stamp(4);
            int before=calls;
            assert(xiaotai_opus_plc_decode(&state,d,&s,packet,sizeof(packet),pcm,CAPACITY)==0);
            assert(calls==before);
        }
        /* Resume at a later real packet, with reset and no speculative gap. */
        s=stamp(7);
        int before=calls;
        assert(xiaotai_opus_plc_decode(&state,d,&s,packet,sizeof(packet),pcm,CAPACITY)==FRAME);
        assert(calls==before+1);
        assert(!state.timeline.desync && state.output_concealed_samples==0);
        opus_decoder_destroy(d);
    }
    puts("Decoder faults: PLC/FEC/current failures retain committed prefixes and reanchor");
    return 0;
}
