#include "xiaotai_video.h"
#include "board_camera.h"

#include <common/bk_err.h>
#include <components/dvp_camera_types.h>
#include "xiaotai_log.h"
#include <components/media_types.h>
#include <driver/h264.h>
#include <driver/h264_types.h>
#include <os/os.h>
#include <stdatomic.h>

#include "frame_buffer.h"

#define TAG "xiaotai_video"
#define VIDEO_WIDTH 640U
#define VIDEO_HEIGHT 480U
#define VIDEO_SENSOR_FPS 20U
#define VIDEO_TARGET_FPS 20U
#define VIDEO_STATS_WINDOW_MS 5000U
#define VIDEO_QUEUE_DEPTH 4U
#define VIDEO_STOP_TIMEOUT_MS 1000U

static beken_queue_t s_frame_queue;
static beken_thread_t s_sender_thread;
static beken_semaphore_t s_sender_stopped;
static atomic_bool s_sender_run;
static atomic_bool s_running;
static xiaotai_video_uplink_fn s_uplink;
static void *s_uplink_context;
static atomic_uint_fast32_t s_sent_frames;
static atomic_uint_fast32_t s_dropped_frames;

static frame_buffer_t *frame_malloc(image_format_t format, uint32_t size)
{
    if (format != IMAGE_H264) return NULL;
    frame_buffer_t *frame = frame_buffer_encode_malloc(size);
    if (frame != NULL) {
        frame->size = size;
        frame->length = 0;
        frame->sequence = 0;
        frame->timestamp = 0;
    }
    return frame;
}

static void frame_complete(image_format_t format,
                           frame_buffer_t *frame,
                           int result)
{
    if (frame == NULL) return;
    /* Never pace after H264 encoding.  P frames reference earlier encoded
     * frames, so discarding one here corrupts the decoder chain until the next
     * IDR.  The hardware encoder is configured to produce the target FPS. */
    if (format != IMAGE_H264 || result != 0 ||
        !atomic_load_explicit(&s_sender_run, memory_order_acquire) ||
        rtos_push_to_queue(&s_frame_queue, &frame, BEKEN_NO_WAIT) != BK_OK) {
        ++s_dropped_frames;
        frame_buffer_encode_free(frame);
    }
}

static const bk_dvp_callback_t s_camera_callbacks = {
    .malloc = frame_malloc,
    .complete = frame_complete,
};

static int reserve_camera_controller(void)
{
    return xiaotai_board_camera_reserve(&s_camera_callbacks);
}

static void sender_task(beken_thread_arg_t argument)
{
    (void)argument;
    uint32_t stats_start_ms = rtos_get_time();
    uint32_t stats_bytes = 0;
    uint32_t stats_sent = 0;
    uint32_t stats_dropped = 0;
    while (atomic_load_explicit(&s_sender_run, memory_order_acquire)) {
        frame_buffer_t *frame = NULL;
        if (rtos_pop_from_queue(&s_frame_queue, &frame, 100) != BK_OK ||
            frame == NULL) {
            continue;
        }
        bool key = (frame->h264_type & (1U << H264_NAL_I_FRAME)) != 0U;
        uint32_t now_ms = rtos_get_time();
        xiaotai_video_uplink_fn callback = s_uplink;
        int rc = callback == NULL ? BK_ERR_NOT_INIT :
            callback(frame->frame, frame->length, now_ms, key,
                     s_uplink_context);
        if (rc >= 0) {
            ++s_sent_frames;
            ++stats_sent;
            stats_bytes += frame->length;
            if (s_sent_frames == 1U) {
                const uint8_t *p = frame->frame;
                BK_LOGI(TAG,
                        "first H264 frame sent bytes=%u key=%d type=0x%08x head=%02x%02x%02x%02x%02x\n",
                        (unsigned)frame->length, key,
                        (unsigned)frame->h264_type,
                        frame->length > 0U ? p[0] : 0U,
                        frame->length > 1U ? p[1] : 0U,
                        frame->length > 2U ? p[2] : 0U,
                        frame->length > 3U ? p[3] : 0U,
                        frame->length > 4U ? p[4] : 0U);
            }
        } else {
            ++s_dropped_frames;
            ++stats_dropped;
        }
        frame_buffer_encode_free(frame);

        now_ms = rtos_get_time();
        uint32_t elapsed_ms = now_ms - stats_start_ms;
        if (elapsed_ms >= VIDEO_STATS_WINDOW_MS) {
            uint32_t fps_x100 = elapsed_ms == 0U ? 0U :
                (stats_sent * 100000U) / elapsed_ms;
            uint32_t kbps = elapsed_ms == 0U ? 0U :
                (stats_bytes * 8U) / elapsed_ms;
            BK_LOGI(TAG,
                    "H264 uplink actual=%u.%02u fps target=%u bitrate=%u kbps sent=%u dropped=%u\n",
                    (unsigned)(fps_x100 / 100U),
                    (unsigned)(fps_x100 % 100U),
                    (unsigned)VIDEO_TARGET_FPS,
                    (unsigned)kbps,
                    (unsigned)stats_sent, (unsigned)stats_dropped);
            stats_start_ms = now_ms;
            stats_bytes = 0;
            stats_sent = 0;
            stats_dropped = 0;
        }
    }

    frame_buffer_t *frame = NULL;
    while (rtos_pop_from_queue(&s_frame_queue, &frame, 0) == BK_OK &&
           frame != NULL) {
        frame_buffer_encode_free(frame);
        frame = NULL;
    }
    s_sender_thread = NULL;
    rtos_set_semaphore(&s_sender_stopped);
    rtos_delete_thread(NULL);
}

int xiaotai_video_init(void)
{
    return reserve_camera_controller();
}

int xiaotai_video_start(xiaotai_video_uplink_fn uplink, void *context)
{
    if (uplink == NULL) return BK_ERR_PARAM;
    if (atomic_load_explicit(&s_running, memory_order_acquire)) return BK_ERR_BUSY;
    int rc = reserve_camera_controller();
    if (rc != BK_OK) return rc;

    rc = rtos_init_queue(&s_frame_queue, "xiaotai_h264",
                         sizeof(frame_buffer_t *), VIDEO_QUEUE_DEPTH);
    if (rc != BK_OK) return rc;
    rc = rtos_init_semaphore(&s_sender_stopped, 1);
    if (rc != BK_OK) {
        rtos_deinit_queue(&s_frame_queue);
        s_frame_queue = NULL;
        return rc;
    }
    s_uplink = uplink;
    s_uplink_context = context;
    s_sent_frames = 0;
    s_dropped_frames = 0;
    atomic_store_explicit(&s_sender_run, true, memory_order_release);
    rc = rtos_create_psram_thread(&s_sender_thread, 5, "xiaotai_h264_tx",
                                  sender_task, 4096, NULL);
    if (rc != BK_OK) goto fail;

    rc = xiaotai_board_camera_start();
    if (rc != BK_OK) goto fail_thread;
    rc = bk_h264_updata_encode_fps(VIDEO_TARGET_FPS);
    if (rc != BK_OK) {
        BK_LOGW(TAG, "H264 timing update to %u fps failed rc=%d\n",
                VIDEO_TARGET_FPS, rc);
    }
    atomic_store_explicit(&s_running, true, memory_order_release);
    BK_LOGI(TAG, "native H264 camera started %ux%u sensor=%u fps uplink=%u fps\n",
            VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_SENSOR_FPS, VIDEO_TARGET_FPS);
    return BK_OK;

fail_thread:
    xiaotai_board_camera_stop();
    atomic_store_explicit(&s_sender_run, false, memory_order_release);
    rtos_get_semaphore(&s_sender_stopped, VIDEO_STOP_TIMEOUT_MS);
fail:
    atomic_store_explicit(&s_sender_run, false, memory_order_release);
    s_uplink = NULL;
    s_uplink_context = NULL;
    rtos_deinit_semaphore(&s_sender_stopped);
    rtos_deinit_queue(&s_frame_queue);
    s_frame_queue = NULL;
    return rc;
}

int xiaotai_video_request_keyframe(void)
{
    if (!atomic_load_explicit(&s_running, memory_order_acquire)) {
        return BK_ERR_NOT_INIT;
    }
    return bk_h264_soft_reset();
}

int xiaotai_video_stop(void)
{
    if (!atomic_load_explicit(&s_running, memory_order_acquire)) return BK_OK;
    atomic_store_explicit(&s_running, false, memory_order_release);
    xiaotai_board_camera_stop();
    atomic_store_explicit(&s_sender_run, false, memory_order_release);
    int rc = rtos_get_semaphore(&s_sender_stopped, VIDEO_STOP_TIMEOUT_MS);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "H264 sender stop timed out rc=%d\n", rc);
        atomic_store_explicit(&s_running, true, memory_order_release);
        return rc;
    }
    s_uplink = NULL;
    s_uplink_context = NULL;
    rtos_deinit_semaphore(&s_sender_stopped);
    rtos_deinit_queue(&s_frame_queue);
    s_frame_queue = NULL;
    BK_LOGI(TAG, "camera stopped sent=%u dropped=%u\n",
            (unsigned)s_sent_frames, (unsigned)s_dropped_frames);
    return BK_OK;
}

bool xiaotai_video_running(void)
{
    return atomic_load_explicit(&s_running, memory_order_acquire);
}
