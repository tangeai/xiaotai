#include "starter_jpeg_collector.h"

#include <string.h>

size_t starter_jpeg_collect(void *argument,
                            size_t index,
                            const void *data,
                            size_t length)
{
    starter_jpeg_collector_t *collect = argument;
    if (collect == NULL) {
        return 0;
    }

    /* jpge uses (data == NULL, length == 0) to signal end-of-image. */
    if (length == 0) {
        return 0;
    }

    if (data == NULL || collect->buffer == NULL ||
        index > collect->capacity || length > collect->capacity - index) {
        collect->overflow = true;
        return 0;
    }
    memcpy(collect->buffer + index, data, length);
    collect->length = index + length;
    return length;
}
