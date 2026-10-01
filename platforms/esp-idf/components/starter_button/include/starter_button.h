#ifndef STARTER_BUTTON_H
#define STARTER_BUTTON_H

/**
 * @file starter_button.h
 * @brief ESP-IDF 用户按键产品控制入口。
 *
 * 板 adapter 负责采样和有效电平；本模块只负责消抖和 AI 启停意图。
 * TiRTC 会话状态仍由 starter_runtime 串行管理。
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 配置 BOOT/用户按键并启动消抖任务；重复调用安全。 */
esp_err_t starter_button_start(void);

#ifdef __cplusplus
}
#endif

#endif
