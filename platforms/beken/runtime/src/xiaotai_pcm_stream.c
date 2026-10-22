#include "xiaotai_pcm_stream.h"

int xiaotai_pcm_stream_collect(xiaotai_stream_read_fn read_fn, void *context,
                               uint8_t *output, size_t capacity,
                               int expected_bytes, size_t minimum_bytes,
                               size_t *received_out)
{
    if (received_out != NULL) *received_out = 0U;
    if (read_fn == NULL || output == NULL || received_out == NULL ||
        capacity == 0U || expected_bytes < -1 ||
        (expected_bytes >= 0 && (size_t)expected_bytes > capacity)) {
        return -1;
    }

    size_t limit = expected_bytes >= 0 ? (size_t)expected_bytes : capacity;
    size_t received = 0U;
    while (received < limit) {
        size_t available = limit - received;
        int count = read_fn(context, output + received, available);
        if (count < 0 || (size_t)count > available) {
            *received_out = received;
            return -1;
        }
        if (count == 0) break;
        received += (size_t)count;
    }

    if (expected_bytes >= 0) {
        if (received != (size_t)expected_bytes) {
            *received_out = received;
            return -1;
        }
    } else if (received == capacity) {
        uint8_t overflow;
        int count = read_fn(context, &overflow, sizeof(overflow));
        if (count != 0) {
            *received_out = received;
            return -1;
        }
    }

    *received_out = received;
    if (received < minimum_bytes || (received & 1U) != 0U) return -1;
    return 0;
}
