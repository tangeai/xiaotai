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
        {300, 406, 2, 13, FRAME},  /* new stream */
        {320, 406, 2, 13, FRAME}, {340, 406, 2, 13, FRAME},
        {420, 406, 2, 13, FRAME},  /* three missing frames exceed budget */
        {440, 406, 2, 13, FRAME}, {460, 406, 2, 13, FRAME},
        {501, 406, 2, 13, FRAME},  /* timestamp jitter */
        {501, 406, 2, 13, FRAME}, {497, 406, 2, 13, FRAME},
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

int main(void)
{
    int err, count = 0;
    OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
    assert(encoder && err == OPUS_OK);
    assert(opus_encoder_ctl(encoder, OPUS_SET_BITRATE(16000)) == OPUS_OK);
    while (count < 256 && fread(input, sizeof(short), FRAME, stdin) == FRAME) {
        lengths[count] = opus_encode(encoder, input, FRAME, packets[count], 512);
        assert(lengths[count++] > 0);
    }
    opus_encoder_destroy(encoder);
    assert(count > 10);
    check_timeline();
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
