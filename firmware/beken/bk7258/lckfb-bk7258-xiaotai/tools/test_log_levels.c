#include <assert.h>
#include <stdarg.h>

#include "xiaotai_log.h"

static int s_writes;
static int s_last_level;

void bk_vprintf_ext(int level, char *tag, const char *format, va_list arguments)
{
    (void)tag;
    (void)format;
    (void)arguments;
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
    xiaotai_log_write(XIAOTAI_LOG_WARN, "test", "visible");
    assert(s_writes == 1);
    assert(s_last_level == XIAOTAI_LOG_WARN);
    assert(xiaotai_log_set_level(-1) != 0);
    assert(xiaotai_log_set_level(XIAOTAI_APP_LOG_MAX + 1) != 0);
    assert(xiaotai_log_get_level() == XIAOTAI_LOG_WARN);
    return 0;
}
