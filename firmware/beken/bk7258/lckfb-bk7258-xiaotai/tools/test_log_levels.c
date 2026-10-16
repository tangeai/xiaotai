#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xiaotai_log.h"

static int s_writes;
static int s_blocking_writes;
static int s_last_level;
static char s_last_message[512];

void bk_vprintf_ext(int level, char *tag, const char *format, va_list arguments)
{
    (void)tag;
    (void)format;
    (void)arguments;
    ++s_writes;
    ++s_blocking_writes;
    s_last_level = level;
}

void bk_printf_nonblock(int level, char *tag, const char *format, ...)
{
    (void)tag;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(s_last_message, sizeof(s_last_message), format, arguments);
    va_end(arguments);
    ++s_writes;
    s_last_level = level;
}

int main(void)
{
#if defined(CONFIG_LOG_LEVEL) && CONFIG_LOG_LEVEL <= XIAOTAI_LOG_INFO
    assert(xiaotai_log_get_level() == XIAOTAI_LOG_INFO);
#else
    assert(xiaotai_log_get_level() == XIAOTAI_LOG_DEBUG);
#endif
    assert(xiaotai_log_set_level(XIAOTAI_LOG_WARN) == 0);
    xiaotai_log_write(XIAOTAI_LOG_INFO, "test", "filtered");
    assert(s_writes == 0);
    xiaotai_log_write(XIAOTAI_LOG_WARN, "test", "visible %d", 42);
    assert(s_writes == 1);
    assert(s_blocking_writes == 0);
    assert(s_last_level == XIAOTAI_LOG_WARN);
    assert(strcmp(s_last_message, "visible 42") == 0);
    assert(xiaotai_log_set_level(-1) != 0);
    assert(xiaotai_log_set_level(XIAOTAI_APP_LOG_MAX + 1) != 0);
    assert(xiaotai_log_get_level() == XIAOTAI_LOG_WARN);
    return 0;
}
