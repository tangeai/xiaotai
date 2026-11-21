/* Host replay against real libopus. Input is 16 kHz mono, signed PCM16. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <modules/opus.h>
#include "xiaotai_opus_plc.h"

enum { FRAME = 320, CAPACITY = 960 };
static short input[FRAME], reference[CAPACITY], repaired[CAPACITY], skipped[CAPACITY];
static unsigned char packets[256][512];
static int lengths[256];

static void check_interleaved_streams(void)
{
    int err;
    OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
    assert(dec && err == OPUS_OK);
    xiaotai_opus_plc_t state = {0};
    xiaotai_opus_stamp_t stamp = {.epoch=1, .stream=1, .samples=FRAME};
    assert(xiaotai_opus_plc_decode(&state, dec, &stamp,
        packets[4], lengths[4], repaired, CAPACITY) == FRAME);
    stamp.stream = 2;
    assert(xiaotai_opus_plc_decode(&state, dec, &stamp,
        packets[4], lengths[4], repaired, CAPACITY) == 0);
    stamp.stream = 1;
    assert(xiaotai_opus_plc_decode(&state, dec, &stamp,
        packets[4], lengths[4], repaired, CAPACITY) == 0);
    stamp.epoch = 2;
    stamp.stream = 2;
    assert(xiaotai_opus_plc_decode(&state, dec, &stamp,
        packets[4], lengths[4], repaired, CAPACITY) == FRAME);
    opus_decoder_destroy(dec);
}

static void check_repeated_gaps(void)
{
    const unsigned timestamps[] = {0, 20, 40, 80, 120, 60, 80, 100, 120};
    const int expected[] = {FRAME, FRAME, FRAME, 2*FRAME, 2*FRAME, 0, 0, 0, 0};
    int err;
    OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
    assert(dec && err == OPUS_OK);
    xiaotai_opus_plc_t state = {0};
    for (unsigned i = 0; i < sizeof(timestamps)/sizeof(timestamps[0]); ++i) {
        xiaotai_opus_stamp_t stamp = {.media_ms=timestamps[i],
            .rx_ms=100+i+(i >= 5 ? 500 : 0), .epoch=1, .stream=14, .samples=FRAME};
        int actual = xiaotai_opus_plc_decode(&state, dec, &stamp,
            packets[4], lengths[4], repaired, CAPACITY);
        if (actual != expected[i]) fprintf(stderr,
            "timestamp %u: expected %d, got %d\n", timestamps[i], expected[i], actual);
        assert(actual == expected[i]);
    }
    opus_decoder_destroy(dec);
}

/* Exercise the same decoder entry as the firmware with clock discontinuities.
 * Use an actual encoded packet so errors and capacity limits come from Opus. */
static void check_timeline(void)
{
    struct event {
        uint32_t media_ms, rx_ms, epoch;
        uint8_t stream;
        int expected;
    } events[] = {
        {UINT32_MAX-39, 100, 1, 14, FRAME},
        {UINT32_MAX-19, 100, 1, 14, FRAME},
        {0, 100, 1, 14, FRAME},
        {40, 111, 1, 14, 2*FRAME}, /* media clock wrapped */
        {20, 112, 1, 14, 0},       /* late copy of concealed slot */
        {60, 115, 1, 14, FRAME}, {80, 115, 1, 14, FRAME},
        {140, 400, 1, 14, FRAME},  /* receiver was idle */
        {160, 401, 1, 14, FRAME}, {180, 402, 1, 14, FRAME},
        {220, 403, 2, 14, FRAME},  /* new round */
        {240, 404, 2, 14, FRAME}, {260, 405, 2, 14, FRAME},
        {300, 406, 3, 13, FRAME},  /* new round may select a new stream */
        {320, 406, 3, 13, FRAME}, {340, 406, 3, 13, FRAME},
        {420, 406, 3, 13, FRAME},  /* three missing frames exceed budget */
        {440, 406, 3, 13, FRAME}, {460, 406, 3, 13, FRAME},
        {501, 406, 3, 13, FRAME},  /* timestamp jitter */
        {501, 406, 3, 13, 0}, {497, 406, 3, 13, 0},
    };
    int err;
    OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
    assert(dec && err == OPUS_OK);
    xiaotai_opus_plc_t state = {0};
    for (size_t i = 0; i < sizeof(events)/sizeof(events[0]); ++i) {
        struct event e = events[i];
        xiaotai_opus_stamp_t stamp = {.media_ms=e.media_ms, .rx_ms=e.rx_ms,
            .epoch=e.epoch, .stream=e.stream, .samples=FRAME};
        assert(xiaotai_opus_plc_decode(&state, dec, &stamp, packets[4], lengths[4],
                                      repaired, CAPACITY) == e.expected);
    }
    opus_decoder_destroy(dec);
}

/* Preserve media time for scoring. A slot with no emitted PCM is scored
 * as silence. The baseline skips the same packets in the same decoder. */
static double envelope_error(const short *expected, const short *actual)
{
    double error = 0;
    for (int k = 0; k < FRAME; k += 80) {
        double a = 0, b = 0;
        for (int j = k; j < k + 80; ++j) {
            a += (double)expected[j] * expected[j];
            if (actual) b += (double)actual[j] * actual[j];
        }
        error += fabs(sqrt(a / 80) - sqrt(b / 80));
    }
    return error;
}

static void check_thirty_percent(int count, int bursty)
{
    count -= count % 10;
    int lost[256] = {0}, dropped = 0, concealed = 0, err;
    unsigned seed = 7;
    while (dropped < 3 * count / 10) {
        seed = seed * 1664525U + 1013904223U;
        int i = 3 + (int)((seed >> 8) % (unsigned)(count - 4));
        int run = bursty ? 2 : 1;
        while (run-- && i < count-1 && !lost[i] && dropped < 3 * count / 10) {
            lost[i++] = 1;
            ++dropped;
        }
    }
    short ref_pcm[256][FRAME], played[256][FRAME] = {{0}}, old[256][FRAME] = {{0}};
    OpusDecoder *ref = opus_decoder_create(16000, 1, &err);
    OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
    OpusDecoder *skip = opus_decoder_create(16000, 1, &err);
    assert(ref && dec && skip);
    xiaotai_opus_plc_t state = {0};
    for (int i = 0; i < count; ++i) {
        assert(opus_decode(ref, packets[i], lengths[i], ref_pcm[i], FRAME, 0) == FRAME);
        if (lost[i]) continue;
        assert(opus_decode(skip, packets[i], lengths[i], old[i], FRAME, 0) == FRAME);
        xiaotai_opus_stamp_t stamp = {.media_ms=i*20U, .rx_ms=i*20U, .samples=FRAME};
        int n = xiaotai_opus_plc_decode(&state, dec, &stamp, packets[i], lengths[i],
                                        repaired, CAPACITY);
        assert(n >= FRAME && n <= CAPACITY && n % FRAME == 0);
        int prefix = n / FRAME - 1;
        for (int j = 1; j <= prefix; ++j) assert(i >= j && lost[i-j]);
        memcpy(played[i-prefix], repaired, n*sizeof(*repaired));
        concealed += prefix;
    }
    double before = 0, after = 0;
    for (int i = 0; i < count; ++i) {
        before += envelope_error(ref_pcm[i], old[i]);
        after += envelope_error(ref_pcm[i], played[i]);
    }
    printf("30%% %s: dropped %d/%d, filled %d, whole-stream envelope MAE %.1f (baseline %.1f)\n",
           bursty ? "clustered" : "random", dropped, count, concealed,
           after/(4*count), before/(4*count));
    assert(dropped*10 == count*3 && concealed > 0 && concealed <= dropped && after < before);
    opus_decoder_destroy(ref);
    opus_decoder_destroy(dec);
    opus_decoder_destroy(skip);
}

int main(void)
{
    int err, count = 0;
    OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
    assert(encoder && err == OPUS_OK);
    assert(xiaotai_opus_configure_loss(encoder) == OPUS_OK);
    opus_int32 loss, fec;
    assert(opus_encoder_ctl(encoder, OPUS_GET_PACKET_LOSS_PERC(&loss)) == OPUS_OK && loss == 30);
    assert(opus_encoder_ctl(encoder, OPUS_GET_INBAND_FEC(&fec)) == OPUS_OK && fec == 1);
    assert(opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(3)) == OPUS_OK);
    assert(opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)) == OPUS_OK);
    while (count < 256 && fread(input, sizeof(short), FRAME, stdin) == FRAME) {
        lengths[count] = opus_encode(encoder, input, FRAME, packets[count], 512);
        assert(lengths[count++] > 0);
    }
    opus_encoder_destroy(encoder);
    assert(count > 10);
    check_interleaved_streams();
    check_repeated_gaps();
    check_timeline();
    check_thirty_percent(count, 0);
    check_thirty_percent(count, 1);
    for (int burst = 0; burst <= 2; ++burst) {
        double energy_error = 0, silence_error = 0, join = 0, skip_join = 0;
        double recovery_error = 0, skipped_error = 0, compressed_error = 0;
        unsigned trials = 0;
        for (int start = 4; start + 2*burst < count; ++start) {
            OpusDecoder *ref = opus_decoder_create(16000, 1, &err);
            OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
            OpusDecoder *skip = opus_decoder_create(16000, 1, &err);
            assert(ref && dec && skip);
            xiaotai_opus_plc_t state = {0};
            double target[8] = {0};
            int last = 0;
            for (int i = 0; i <= start + burst; ++i) {
                assert(opus_decode(ref, packets[i], lengths[i], reference, FRAME, 0) == FRAME);
                if (i >= start && i < start + burst) {
                    for (int j = 0; j < FRAME; ++j)
                        target[4*(i-start) + j/80] += (double)reference[j]*reference[j];
                    continue;
                }
                assert(opus_decode(skip, packets[i], lengths[i], skipped, FRAME, 0) == FRAME);
                xiaotai_opus_stamp_t stamp = {
                    .media_ms = (unsigned)i*20, .rx_ms = (unsigned)(i/3)*60,
                    .samples = FRAME, .stream = 14, .epoch = 1
                };
                int n = xiaotai_opus_plc_decode(&state, dec, &stamp, packets[i],
                                                lengths[i], repaired, CAPACITY);
                if (i < start || !burst) {
                    assert(n == FRAME && !memcmp(repaired, reference, sizeof(input)));
                    last = reference[FRAME-1];
                    continue;
                }
                assert(n == FRAME*(1+burst));
                join += abs(repaired[0]-last);
                skip_join += abs(skipped[0]-last);
                for (int k = 0; k < burst*4; ++k) {
                    double sum = 0;
                    for (int j = 0; j < 80; ++j) {
                        double v = repaired[80*k+j];
                        sum += v*v;
                    }
                    double expected = sqrt(target[k]/80);
                    energy_error += fabs(expected-sqrt(sum/80));
                    silence_error += expected;
                }
                for (int k = 0; k < 4; ++k) {
                    double a = 0, b = 0, c = 0;
                    for (int j = 80*k; j < 80*(k+1); ++j) {
                        double v = repaired[burst*FRAME+j];
                        a += v*v;
                        b += (double)reference[j]*reference[j];
                        c += (double)skipped[j]*skipped[j];
                    }
                    recovery_error += fabs(sqrt(a/80)-sqrt(b/80));
                    skipped_error += fabs(sqrt(c/80)-sqrt(b/80));
                }
                /* Compare the actual old playback timeline. Skipping the loss
                 * plays the following real packets early when already queued. */
                for (int f = 0; f <= burst; ++f) {
                    if (f) {
                        int next = i + f;
                        assert(opus_decode(skip, packets[next], lengths[next], skipped, FRAME, 0) == FRAME);
                    }
                    for (int k = 0; k < 4; ++k) {
                        double sum = 0, expected = 0;
                        for (int j = k*80; j < (k+1)*80; ++j) {
                            sum += (double)skipped[j]*skipped[j];
                            expected += (double)reference[j]*reference[j];
                        }
                        if (f < burst) expected = target[f*4+k];
                        compressed_error += fabs(sqrt(sum/80)-sqrt(expected/80));
                    }
                }
                ++trials;
            }
            opus_decoder_destroy(dec); opus_decoder_destroy(ref); opus_decoder_destroy(skip);
        }
        if (burst) {
            printf("%d x 20 ms, %u trials: envelope MAE %.1f (silence %.1f), "
                   "mean entry step %.1f (skip %.1f)\n", burst, trials,
                   energy_error/(trials*burst*4), silence_error/(trials*burst*4),
                   join/trials, skip_join/trials);
            printf("first good packet envelope MAE %.1f (skip %.1f)\n",
                   recovery_error/(trials*4), skipped_error/(trials*4));
            printf("aligned gap + recovery MAE %.1f (queued skip %.1f)\n",
                   (energy_error+recovery_error)/(trials*(burst+1)*4),
                   compressed_error/(trials*(burst+1)*4));
            assert(energy_error + recovery_error < compressed_error);
            assert(energy_error < silence_error && join <= skip_join + trials);
        }
    }
    return 0;
}
