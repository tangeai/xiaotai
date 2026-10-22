#include "tirtc_bk_probe.h"
#include "xiaotai_build_identity.h"
#include "xiaotai_log.h"
#include <os/os.h>
#if XIAOTAI_DUAL_EYES
#define PROBE_PROFILE "eyes-key"
#define PROBE_DISPLAY "dual-gc9d01-declared"
#else
#define PROBE_PROFILE "headless-key"
#define PROBE_DISPLAY "disabled"
#endif
void tirtc_bk_probe_emit(void)
{
    BK_LOG_RAW("TIRTC_BK_PROBE {\"schema_version\":1,\"board_id\":\"mgkfb-bk7258\","
        "\"firmware_version\":\"%s\",\"sdk\":\"bk_avdk_smp/release/v3.1.1.10\","
        "\"interaction\":\"" PROBE_PROFILE "\",\"display\":\"" PROBE_DISPLAY "\",\"touch\":\"disabled\","
        "\"flash_physical_bytes\":null,\"psram_heap_bytes\":%u,\"internal_free_bytes\":%u,"
        "\"camera\":{\"sensor\":\"GC2145\",\"state\":\"declared\",\"test\":\"not_run\"},"
        "\"audio\":{\"state\":\"declared\",\"duplex_test\":\"not_run\",\"aec_test\":\"not_run\"}}\r\n",
        XIAOTAI_BUILD_VERSION, (unsigned)rtos_get_psram_total_heap_size(),
        (unsigned)rtos_get_free_heap_size());
}
