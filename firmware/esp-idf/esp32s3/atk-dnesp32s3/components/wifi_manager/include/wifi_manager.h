#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/**
 * @file wifi_manager.h
 * @brief Wi-Fi STA 连接和首次 SoftAP 配网。
 *
 * 有可用 NVS 配置时连接 STA；配置缺失或重试耗尽时同时开启 SoftAP、captive
 * portal 自动发现和配置页。
 * 模块关闭 Wi-Fi 省电以降低实时音视频抖动。
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_SSID_MAX 32
#define WIFI_MANAGER_PASSWORD_MAX 64

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX + 1];         /**< UTF-8 SSID。 */
    char password[WIFI_MANAGER_PASSWORD_MAX + 1]; /**< 空串表示开放网络。 */
} wifi_manager_credentials_t;

/** 初始化网络栈并启动 STA/SoftAP 流程；重复调用安全。 */
esp_err_t wifi_manager_start(void);

/** 以下查询函数返回当前瞬时状态。 */
bool wifi_manager_connected(void);
bool wifi_manager_provisioning(void);

/** 配网未启用时返回空字符串；返回值由模块持有。 */
const char *wifi_manager_provisioning_ssid(void);

/** 从 wifi_cfg 命名空间加载配置；失败时清空输出结构。 */
esp_err_t wifi_manager_load_credentials(wifi_manager_credentials_t *credentials);

/** 校验并提交 Wi-Fi 配置到 NVS；调用后不会自动重启。 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/** 删除 Wi-Fi NVS 配置；调用后不会自动重启。 */
esp_err_t wifi_manager_forget_credentials(void);

/** 按 ESP32 字节长度规则校验 SSID 和密码；error 可为 NULL。 */
bool wifi_manager_credentials_valid(const char *ssid,
                                    const char *password,
                                    char *error,
                                    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
