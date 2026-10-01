#include <driver/aon_rtc.h>
#include <sys/time.h>
#include <time.h>

#include "xiaotai_time.h"

/* Newlib's time() reaches the platform clock through _gettimeofday().
 * BK-AVDK does not provide that syscall in this configuration and otherwise
 * links libnosys, whose stub always fails.  Bridge it to the AON RTC used by
 * network time synchronization and by the TiRTC platform adapter. */
int _gettimeofday(struct timeval *tv, void *timezone)
{
    return bk_rtc_gettimeofday(tv, timezone);
}

time_t xiaotai_time_now(void)
{
    return time(NULL);
}
