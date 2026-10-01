#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    XIAOTAI_CAMERA_FRAME_UNKNOWN = 0,
    XIAOTAI_CAMERA_FRAME_JPEG,
    XIAOTAI_CAMERA_FRAME_RGB565,
} xiaotai_camera_frame_format_t;

/* token is owned by the adapter.  data remains valid until release() and must
 * never be retained by product code after that call. */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t width;
    uint16_t height;
    xiaotai_camera_frame_format_t format;
    void *token;
} xiaotai_board_camera_frame_t;

typedef struct {
    void *context;
    int (*start)(void *context);
    bool (*ready)(void *context);
    int (*acquire)(void *context, xiaotai_board_camera_frame_t *frame);
    void (*release)(void *context, xiaotai_board_camera_frame_t *frame);
} xiaotai_board_camera_adapter_t;

static inline bool xiaotai_board_camera_adapter_is_valid(
    const xiaotai_board_camera_adapter_t *adapter)
{
    return adapter != NULL && adapter->start != NULL &&
           adapter->ready != NULL && adapter->acquire != NULL &&
           adapter->release != NULL;
}

static inline bool xiaotai_board_camera_frame_is_valid(
    const xiaotai_board_camera_frame_t *frame)
{
    return frame != NULL && frame->data != NULL && frame->size > 0U &&
           frame->width > 0U && frame->height > 0U && frame->token != NULL &&
           frame->format != XIAOTAI_CAMERA_FRAME_UNKNOWN;
}
