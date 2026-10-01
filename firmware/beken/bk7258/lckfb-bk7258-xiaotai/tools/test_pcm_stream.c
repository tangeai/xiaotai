#include "xiaotai_pcm_stream.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
    size_t chunk;
    int fail_after;
} fixture_t;

static int fixture_read(void *context, void *output, size_t capacity)
{
    fixture_t *fixture = context;
    if (fixture->fail_after >= 0 &&
        fixture->offset >= (size_t)fixture->fail_after) return -7;
    if (fixture->offset == fixture->size) return 0;
    size_t count = fixture->size - fixture->offset;
    if (count > fixture->chunk) count = fixture->chunk;
    if (count > capacity) count = capacity;
    memcpy(output, fixture->data + fixture->offset, count);
    fixture->offset += count;
    return (int)count;
}

static fixture_t fixture(const uint8_t *data, size_t size, size_t chunk)
{
    fixture_t value = {data, size, 0U, chunk, -1};
    return value;
}

int main(void)
{
    uint8_t source[6000];
    uint8_t output[6000];
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)i;
    size_t received = 0U;

    fixture_t chunked = fixture(source, 5000U, 733U);
    assert(xiaotai_pcm_stream_collect(fixture_read, &chunked, output,
                                     sizeof(output), -1, 4000U,
                                     &received) == 0);
    assert(received == 5000U && memcmp(source, output, received) == 0);

    fixture_t fixed = fixture(source, 5000U, 997U);
    assert(xiaotai_pcm_stream_collect(fixture_read, &fixed, output,
                                     sizeof(output), 5000, 4000U,
                                     &received) == 0);

    fixture_t too_large = fixture(source, sizeof(source), 1000U);
    assert(xiaotai_pcm_stream_collect(fixture_read, &too_large, output,
                                     5000U, -1, 4000U, &received) != 0);

    fixture_t odd = fixture(source, 4999U, 512U);
    assert(xiaotai_pcm_stream_collect(fixture_read, &odd, output,
                                     sizeof(output), -1, 4000U,
                                     &received) != 0);

    fixture_t short_fixed = fixture(source, 4500U, 800U);
    assert(xiaotai_pcm_stream_collect(fixture_read, &short_fixed, output,
                                     sizeof(output), 5000, 4000U,
                                     &received) != 0);

    puts("PCM stream tests passed");
    return 0;
}
