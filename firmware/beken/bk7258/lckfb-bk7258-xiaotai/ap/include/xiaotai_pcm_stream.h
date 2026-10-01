#ifndef XIAOTAI_PCM_STREAM_H
#define XIAOTAI_PCM_STREAM_H

#include <stddef.h>
#include <stdint.h>

typedef int (*xiaotai_stream_read_fn)(void *context, void *output,
                                      size_t capacity);

/**
 * Collect a fixed-length or EOF-terminated PCM stream into a bounded buffer.
 * expected_bytes is -1 for HTTP chunked transfer. The reader must return the
 * decoded payload, not HTTP chunk framing.
 */
int xiaotai_pcm_stream_collect(xiaotai_stream_read_fn read_fn, void *context,
                               uint8_t *output, size_t capacity,
                               int expected_bytes, size_t minimum_bytes,
                               size_t *received_out);

#endif
