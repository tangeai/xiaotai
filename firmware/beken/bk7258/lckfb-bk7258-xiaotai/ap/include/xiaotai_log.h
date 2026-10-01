#ifndef XIAOTAI_LOG_H
#define XIAOTAI_LOG_H

#include <components/log.h>

/* Values intentionally mirror Beken's public BK_LOG_* levels. */
#define XIAOTAI_LOG_NONE 0
#define XIAOTAI_LOG_ERROR 1
#define XIAOTAI_LOG_WARN 2
#define XIAOTAI_LOG_INFO 3
#define XIAOTAI_LOG_DEBUG 4
#define XIAOTAI_APP_LOG_MAX XIAOTAI_LOG_DEBUG

int xiaotai_log_set_level(int level);
int xiaotai_log_get_level(void);
void xiaotai_log_write(int level, const char *tag, const char *format, ...);

#ifdef BK_LOG_DEBUG
#undef BK_LOGE
#undef BK_LOGW
#undef BK_LOGI
#undef BK_LOGD
#undef BK_LOGV

#define BK_LOGE(tag, format, ...) \
    xiaotai_log_write(XIAOTAI_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define BK_LOGW(tag, format, ...) \
    xiaotai_log_write(XIAOTAI_LOG_WARN, tag, format, ##__VA_ARGS__)
#define BK_LOGI(tag, format, ...) \
    xiaotai_log_write(XIAOTAI_LOG_INFO, tag, format, ##__VA_ARGS__)
#define BK_LOGD(tag, format, ...) \
    xiaotai_log_write(XIAOTAI_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define BK_LOGV(tag, format, ...) ((void)0)
#endif

#endif
