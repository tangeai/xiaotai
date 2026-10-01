#ifndef STARTER_RUNTIME_H
#define STARTER_RUNTIME_H

/**
 * @file starter_runtime.h
 * @brief H5 与 AI 共用媒体资源的会话状态机。
 *
 * 本模块隐藏 token 获取、WHIP 建连、AI JSON-RPC 握手、超时和迟到回调过滤。
 * 所有状态变化都在一个 FreeRTOS 任务内串行执行；调用者只投递意图并读取快照。
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "xiaotai_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef xiaotai_runtime_state_t starter_runtime_state_t;
#define STARTER_RUNTIME_WAITING XIAOTAI_STATE_WAITING
#define STARTER_RUNTIME_H5_ACTIVE XIAOTAI_STATE_STREAM_ACTIVE
#define STARTER_RUNTIME_AI_CONNECTING XIAOTAI_STATE_AI_CONNECTING
#define STARTER_RUNTIME_AI_ACTIVE XIAOTAI_STATE_AI_ACTIVE

typedef struct {
    starter_runtime_state_t state; /**< 当前公开状态。 */
    uint32_t session_generation;   /**< 每次业务会话递增，用于过滤 HTTP 响应。 */
    uint32_t connection_generation; /**< TiRTC 连接代次，0 表示无连接。 */
    int last_error;                /**< 最近一次结束原因，0 表示正常结束。 */
} starter_runtime_status_t;

/**
 * 创建状态任务并注册平台/TiRTC 回调。必须在 platform_client_start() 和
 * starter_tirtc_start() 前完成；重复调用安全。
 */
esp_err_t starter_runtime_start(const char *device_id);

/** 非阻塞请求启动 AI 对讲；返回值只表示事件是否成功入队。 */
esp_err_t starter_runtime_ai_start(void);

/** 非阻塞请求结束 AI 对讲；返回值只表示事件是否成功入队。 */
esp_err_t starter_runtime_ai_stop(void);

/** 返回线程安全的瞬时状态快照。 */
starter_runtime_status_t starter_runtime_status(void);

/** 返回用于日志和串口状态输出的静态状态名称。 */
const char *starter_runtime_state_name(starter_runtime_state_t state);

#ifdef __cplusplus
}
#endif

#endif
