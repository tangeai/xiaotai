/* Real libopus differential replay. No network or hardware claims are made. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <modules/opus.h>
#include "xiaotai_opus_plc.h"

enum { FRAME = 320, CAPACITY = 960, MAX_FRAMES = 256 };
static short input[MAX_FRAMES][FRAME];
static unsigned char packets[MAX_FRAMES][512];
static int lengths[MAX_FRAMES];

static OpusDecoder *decoder(void)
{
    int error;
    OpusDecoder *d = opus_decoder_create(16000, 1, &error);
    assert(d && error == OPUS_OK);
    return d;
}

static xiaotai_opus_stamp_t stamp(unsigned i)
{
    return (xiaotai_opus_stamp_t){.media_ms=i*20U, .rx_ms=i*20U,
        .epoch=1, .samples=FRAME, .stream=14};
}

static void encode(int count, int fec)
{
    int error;
    OpusEncoder *e = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    assert(e && error == OPUS_OK);
    assert(xiaotai_opus_configure_loss(e) == OPUS_OK);
    opus_int32 value;
    assert(opus_encoder_ctl(e, OPUS_GET_BITRATE(&value)) == OPUS_OK && value == 20000);
    assert(opus_encoder_ctl(e, OPUS_GET_PACKET_LOSS_PERC(&value)) == OPUS_OK && value == 30);
    assert(opus_encoder_ctl(e, OPUS_GET_INBAND_FEC(&value)) == OPUS_OK && value == 1);
    assert(opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(fec)) == OPUS_OK);
    assert(opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(3)) == OPUS_OK);
    assert(opus_encoder_ctl(e, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)) == OPUS_OK);
    for (int i=0; i<count; ++i) {
        lengths[i] = opus_encode(e, input[i], FRAME, packets[i], sizeof(packets[i]));
        assert(lengths[i] > 0);
    }
    opus_encoder_destroy(e);
}

/* Compare firmware output with the exact reference sequence: for two losses,
 * oldest PLC, immediately previous FEC, current normal decode. Compare a
 * separate pure-PLC decoder to prove real redundancy contributed, rather than
 * merely counting a successful decode_fec call (which can itself return PLC). */
static void check_recovery(int count, int fec)
{
    unsigned fec_contributions=0, trials=0;
    for (int missing=1; missing<=2; ++missing) {
        for (int at=4; at+missing+1<count; ++at) {
            OpusDecoder *dut=decoder(), *ref=decoder(), *plc=decoder();
            xiaotai_opus_plc_t state={0};
            short actual[CAPACITY], expected[CAPACITY], concealed[CAPACITY];
            for (int i=0; i<at; ++i) {
                xiaotai_opus_stamp_t s=stamp((unsigned)i);
                assert(xiaotai_opus_plc_decode(&state,dut,&s,packets[i],lengths[i],actual,CAPACITY)==FRAME);
                assert(opus_decode(ref,packets[i],lengths[i],expected,FRAME,0)==FRAME);
                assert(opus_decode(plc,packets[i],lengths[i],concealed,FRAME,0)==FRAME);
                assert(!memcmp(actual,expected,FRAME*sizeof(short)));
            }
            int current=at+missing;
            if (missing==2)
                assert(opus_decode(ref,NULL,0,expected,FRAME,0)==FRAME);
            assert(opus_decode(ref,packets[current],lengths[current],
                               expected+(missing-1)*FRAME,FRAME,1)==FRAME);
            for (int j=0; j<missing; ++j)
                assert(opus_decode(plc,NULL,0,concealed+j*FRAME,FRAME,0)==FRAME);
            int different=memcmp(expected+(missing-1)*FRAME,
                                 concealed+(missing-1)*FRAME,FRAME*sizeof(short))!=0;
            fec_contributions+=(unsigned)different;
            if (!fec) assert(!different);
            assert(opus_decode(ref,packets[current],lengths[current],expected+missing*FRAME,FRAME,0)==FRAME);
            xiaotai_opus_stamp_t s=stamp((unsigned)current);
            assert(xiaotai_opus_plc_decode(&state,dut,&s,packets[current],lengths[current],actual,CAPACITY)==(missing+1)*FRAME);
            assert(!memcmp(actual,expected,(missing+1)*FRAME*sizeof(short)));
            /* The next packet verifies decoder state, not just emitted prefix. */
            ++current;
            s=stamp((unsigned)current);
            assert(xiaotai_opus_plc_decode(&state,dut,&s,packets[current],lengths[current],actual,CAPACITY)==FRAME);
            assert(opus_decode(ref,packets[current],lengths[current],expected,FRAME,0)==FRAME);
            assert(!memcmp(actual,expected,FRAME*sizeof(short)));
            ++trials;
            opus_decoder_destroy(dut); opus_decoder_destroy(ref); opus_decoder_destroy(plc);
        }
    }
    printf("FEC=%d: %u recovery trials, %u contributions distinct from pure PLC\n",fec,trials,fec_contributions);
    assert(fec ? fec_contributions>0 : fec_contributions==0);
}

/* A short output buffer must not silently skip concealment and consume the
 * current packet. Retry through the same public API must match a fresh peer. */
static void check_capacity(void)
{
    OpusDecoder *dut=decoder(), *ref=decoder();
    xiaotai_opus_plc_t a={0}, b={0};
    short actual[CAPACITY], expected[CAPACITY];
    for (unsigned i=0; i<4; ++i) {
        xiaotai_opus_stamp_t s=stamp(i);
        assert(xiaotai_opus_plc_decode(&a,dut,&s,packets[i],lengths[i],actual,CAPACITY)==FRAME);
        assert(xiaotai_opus_plc_decode(&b,ref,&s,packets[i],lengths[i],expected,CAPACITY)==FRAME);
    }
    xiaotai_opus_stamp_t s=stamp(6);
    assert(xiaotai_opus_plc_decode(&a,dut,&s,packets[6],lengths[6],actual,FRAME)==OPUS_BUFFER_TOO_SMALL);
    assert(xiaotai_opus_plc_decode(&a,dut,&s,packets[6],lengths[6],actual,CAPACITY)==CAPACITY);
    assert(xiaotai_opus_plc_decode(&b,ref,&s,packets[6],lengths[6],expected,CAPACITY)==CAPACITY);
    assert(!memcmp(actual,expected,sizeof(actual)));
    opus_decoder_destroy(dut); opus_decoder_destroy(ref);
}

/* Non-20ms frames and actual encoder-produced DTX packets must break the
 * cadence. Compare with normal-only libopus, including a subsequent 20ms
 * packet after another small gap; neither gap may synthesize extra PCM. */
static void check_nonregular_packet(const unsigned char *packet, int bytes,
                                    int samples)
{
    OpusDecoder *dut=decoder(), *ref=decoder();
    xiaotai_opus_plc_t state={0};
    short actual[CAPACITY], expected[CAPACITY];
    for (unsigned i=0; i<4; ++i) {
        xiaotai_opus_stamp_t s=stamp(i);
        assert(xiaotai_opus_plc_decode(&state,dut,&s,packets[i],lengths[i],actual,CAPACITY)==FRAME);
        assert(opus_decode(ref,packets[i],lengths[i],expected,CAPACITY,0)==FRAME);
        assert(!memcmp(actual,expected,FRAME*sizeof(short)));
    }
    xiaotai_opus_stamp_t s=stamp(5); /* 20ms interior gap after established cadence */
    s.samples=(uint16_t)samples;
    assert(xiaotai_opus_plc_decode(&state,dut,&s,packet,bytes,actual,CAPACITY)==samples);
    assert(state.output_concealed_samples==0);
    assert(opus_decode(ref,packet,bytes,expected,CAPACITY,0)==samples);
    assert(!memcmp(actual,expected,samples*sizeof(short)));
    s.media_ms+=samples/16U+20U;
    s.rx_ms+=20U;
    s.samples=FRAME;
    assert(xiaotai_opus_plc_decode(&state,dut,&s,packets[8],lengths[8],actual,CAPACITY)==FRAME);
    assert(state.output_concealed_samples==0);
    assert(opus_decode(ref,packets[8],lengths[8],expected,CAPACITY,0)==FRAME);
    assert(!memcmp(actual,expected,FRAME*sizeof(short)));
    opus_decoder_destroy(dut); opus_decoder_destroy(ref);
}

static void check_variable_duration_and_dtx(void)
{
    const int durations[]={160,640,960}; /* 10, 40, 60ms at 16kHz */
    short pcm[CAPACITY];
    unsigned char packet[1024];
    for (unsigned i=0; i<sizeof(durations)/sizeof(durations[0]); ++i) {
        int error;
        OpusEncoder *e=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);
        assert(e && error==OPUS_OK);
        assert(xiaotai_opus_configure_loss(e)==OPUS_OK);
        for (int j=0; j<durations[i]; ++j) pcm[j]=input[j/FRAME][j%FRAME];
        int bytes=opus_encode(e,pcm,durations[i],packet,sizeof(packet));
        assert(bytes>2);
        assert(opus_packet_get_nb_samples(packet,bytes,16000)==durations[i]);
        check_nonregular_packet(packet,bytes,durations[i]);
        opus_encoder_destroy(e);
    }
    int error, dtx_bytes=0;
    OpusEncoder *e=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);
    assert(e && error==OPUS_OK);
    assert(xiaotai_opus_configure_loss(e)==OPUS_OK);
    assert(opus_encoder_ctl(e,OPUS_SET_DTX(1))==OPUS_OK);
    memset(pcm,0,sizeof(pcm));
    for (int i=0; i<200; ++i) {
        int bytes=opus_encode(e,pcm,FRAME,packet,sizeof(packet));
        assert(bytes>0);
        if (bytes<=2) { dtx_bytes=bytes; break; }
    }
    assert(dtx_bytes>0);
    assert(opus_packet_get_nb_samples(packet,dtx_bytes,16000)==FRAME);
    check_nonregular_packet(packet,dtx_bytes,FRAME);
    opus_encoder_destroy(e);
    printf("10/40/60ms packets and real %d-byte DTX break recovery cadence\n",dtx_bytes);
}

int main(void)
{
    int count=0;
    printf("libopus=%s\n",opus_get_version_string());
    while (count<MAX_FRAMES && fread(input[count],sizeof(short),FRAME,stdin)==FRAME) ++count;
    assert(count>10);
    for (int fec=0; fec<=1; ++fec) {
        encode(count,fec);
        check_recovery(count,fec);
    }
    fflush(stdout);
    check_capacity();
    check_variable_duration_and_dtx();
    return 0;
}
