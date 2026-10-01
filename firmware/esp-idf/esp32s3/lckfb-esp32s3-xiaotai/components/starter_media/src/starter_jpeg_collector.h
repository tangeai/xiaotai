#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} starter_jpeg_collector_t;

size_t starter_jpeg_collect(void *argument,
                            size_t index,
                            const void *data,
                            size_t length);
