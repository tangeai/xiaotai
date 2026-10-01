#ifndef MODULES_AUDIO_NS_H
#define MODULES_AUDIO_NS_H

#include <stdint.h>

#define BK_OK 0
#define BK_FAIL (-1)

int bk_aud_ns_init(int frame_size_20ms, int sample_rate_hz);
int bk_aud_ns_deinit(void);
int bk_aud_ns_process(int16_t *samples);
int bk_aud_ns_set_suprs(int value);

#endif
