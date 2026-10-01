#ifndef STARTER_MEDIA_H
#define STARTER_MEDIA_H

/**
 * @file starter_media.h
 * @brief 产品音视频硬件的唯一接入点。
 *
 * ATK 板的媒体接入点。一个 generation 代表一次 TiRTC 连接，旧连接的
 * 下行帧会被丢弃。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "starter_tirtc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;                 /**< 当前会话是否允许媒体任务运行。 */
    starter_tirtc_mode_t mode;   /**< 当前是 H5 还是 AI；AI 只发送音频。 */
    uint32_t generation;         /**< 当前连接代次，用于过滤迟到帧。 */
    uint32_t audio_sent;         /**< 产品适配器成功发送的音频帧数。 */
    uint32_t video_sent;         /**< 产品适配器成功发送的视频帧数。 */
    uint32_t audio_received;     /**< 成功复制到播放队列的音频帧数。 */
    uint32_t audio_dropped;      /**< 参数无效、队列满或代次过期的帧数。 */
} starter_media_status_t;

typedef enum {
    STARTER_MEDIA_CONTROL_WIFI_RESET = 1,
    STARTER_MEDIA_CONTROL_BINDING_RESET = 2,
} starter_media_control_intent_t;

typedef void (*starter_media_control_handler_t)(starter_media_control_intent_t intent,
                                                 void *user_data);

/** 探测板级外设并创建固定媒体队列和任务。 */
esp_err_t starter_media_init(void);

/** 异步播报验证码三遍；只接受十进制数字，副本由媒体模块持有。 */
esp_err_t starter_media_announce_binding_code(const char *code);

/** 绑定结束或失效时立即停止播报并清除验证码副本。 */
void starter_media_clear_binding_code(void);

/** KEY2/KEY3 长按只投递意图；回调必须零等待，业务动作在其他任务执行。 */
void starter_media_set_control_handler(starter_media_control_handler_t handler,
                                       void *user_data);

/**
 * 允许指定连接代次开始媒体采集。
 *
 * 该函数由会话状态任务调用。板级实现应启动自己的采集/编码任务，但不要在
 * 此函数中无限阻塞。AI 模式不得启动视频采集。
 */
esp_err_t starter_media_start(starter_tirtc_mode_t mode, uint32_t generation);

/** 停止并回收板级采集任务；重复调用安全。 */
void starter_media_stop(void);

/** 把远端刷新请求转交给所选编码器；MJPEG 可忽略，过期 generation 会被忽略。 */
void starter_media_request_key_frame(uint32_t generation);

/**
 * 提交一帧下行 A-law 音频。
 *
 * 可从 SDK 回调调用：函数只做有界复制并以零等待时间投递固定队列；data 的
 * 所有权仍属于 SDK，函数返回后不会继续引用它。
 */
void starter_media_submit_audio(starter_tirtc_mode_t mode,
                                uint32_t generation,
                                const starter_tirtc_frame_t *frame,
                                const void *data);

/** 返回由原子变量组成的瞬时状态快照，可从任意任务调用。 */
starter_media_status_t starter_media_status(void);

#ifdef __cplusplus
}
#endif

#endif
