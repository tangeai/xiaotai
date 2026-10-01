#ifndef PLATFORM_CLIENT_H
#define PLATFORM_CLIENT_H

/**
 * @file platform_client.h
 * @brief ThingConnect 服务发现、设备鉴权、业务 HTTP 和 MQTT 信令。
 *
 * 本模块隐藏签名、token 和 MQTT topic 细节。组合根只在加载配置和启动本模块
 * 时传入设备密钥；正常业务模块不持有设备密钥或 bearer token。首次绑定是
 * 同步流程，正常业务 HTTP 通过固定队列异步执行。
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 可由异步请求路由的服务；生成的起步工程只保留 DEVICE 和 AI。 */
typedef enum {
    PLATFORM_SERVICE_DEVICE = 0,
    PLATFORM_SERVICE_AI,
} platform_service_t;

typedef struct {
    const char *device_id;     /**< 已绑定设备 ID。 */
    const char *device_secret; /**< 设备密钥，模块内部用于签名。 */
    const char *client_id;     /**< MQTT/TiRTC 客户端标识。 */
    const char *mac_address;   /**< STA MAC 字符串。 */
    const char *discovery_url; /**< 服务发现入口，NULL 使用默认值。 */
} platform_client_config_t;

/** 首次绑定或服务端解绑后重绑的输入。 */
typedef struct {
    const char *mac_address;          /**< 设备指纹。 */
    const char *existing_device_id;   /**< 重绑时提供；首次绑定传 NULL。 */
    const char *existing_device_secret; /**< 必须与 existing_device_id 同时提供。 */
    const char *discovery_url;        /**< NULL 使用默认发现入口。 */
    unsigned timeout_seconds;         /**< 等待 auth_grant，0 使用默认值。 */
    void (*on_verification_code)(const char *code, void *user_data);
    void *verification_code_user_data;
} platform_provision_config_t;

/** 绑定完成后由调用者持久化，结构内容不会由模块自动写入 NVS。 */
typedef struct {
    char device_id[65];
    char device_secret[257];
} platform_provision_result_t;

/**
 * 异步 HTTP 结果；传输失败时 body 为 NULL，其他情况下只在回调期间有效。
 */
typedef void (*platform_response_callback_t)(const char *body, void *user_data);

/** MQTT 信令回调；json 只在回调期间有效，保留时必须复制。 */
typedef void (*platform_signal_callback_t)(const char *json,
                                           size_t length,
                                           void *user_data);

/**
 * 完成服务发现、签名设备登录并启动 HTTP 请求任务和永久 MQTT。
 * 函数执行网络 I/O，必须在 app_main 之外的工作任务调用；重复调用安全。
 */
esp_err_t platform_client_start(const platform_client_config_t *config);

/**
 * 上报设备指纹、显示验证码、使用临时 MQTT 等待 auth_grant 并发送 ACK。
 * 该函数阻塞至成功、失败或超时，只能在工作任务调用。已有凭证用于服务端
 * 解绑后的签名重绑；首次绑定将两个 existing 字段都设为 NULL。
 */
esp_err_t platform_client_provision(const platform_provision_config_t *config,
                                    platform_provision_result_t *result);

/** 以下查询返回模块当前瞬时状态。 */
bool platform_client_ready(void);
bool platform_client_mqtt_connected(void);
bool platform_client_provisioning(void);

/** 仅绑定等待期间返回验证码，否则返回空字符串；返回值由模块持有。 */
const char *platform_client_verification_code(void);

/**
 * 返回最近一次服务发现得到的 TiRTC endpoint。
 * 仅在 platform_client_ready() 为 true 时有效；未发现或未就绪时返回 NULL。
 * 返回字符串由本模块持有，调用者不得修改或释放。
 */
const char *platform_client_tirtc_endpoint(void);

/**
 * json_body 为 NULL 时发送 GET，否则发送 POST。请求复制进固定队列后立即返回；
 * callback 在平台请求任务中执行，body 只在 callback 返回前有效。
 */
esp_err_t platform_client_request(platform_service_t service,
                                  const char *path,
                                  const char *json_body,
                                  platform_response_callback_t callback,
                                  void *user_data);
esp_err_t platform_client_request_timeout(platform_service_t service,
                                          const char *path,
                                          const char *json_body,
                                          unsigned timeout_ms,
                                          platform_response_callback_t callback,
                                          void *user_data);

/** 注册永久 MQTT 的信令消费者；应在 platform_client_start() 前调用。 */
void platform_client_set_signal_handler(platform_signal_callback_t callback,
                                        void *user_data);

#ifdef __cplusplus
}
#endif

#endif
