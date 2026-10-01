#include "xiaotai_log.h"

#include <stdarg.h>

#include <common/bk_err.h>
#include <components/system.h>

#if defined(CONFIG_LOG_LEVEL) && CONFIG_LOG_LEVEL <= XIAOTAI_LOG_INFO
static volatile int s_app_log_level = XIAOTAI_LOG_INFO;
#else
static volatile int s_app_log_level = XIAOTAI_LOG_DEBUG;
#endif

int xiaotai_log_set_level(int level)
{
    if (level < XIAOTAI_LOG_NONE || level > XIAOTAI_APP_LOG_MAX) {
        return BK_ERR_PARAM;
    }
    s_app_log_level = level;
    return BK_OK;
}

int xiaotai_log_get_level(void)
{
    return s_app_log_level;
}

void xiaotai_log_write(int level, const char *tag, const char *format, ...)
{
    if (level > s_app_log_level) return;
    va_list arguments;
    va_start(arguments, format);
    bk_vprintf_ext(level, (char *)tag, format, arguments);
    va_end(arguments);
}
