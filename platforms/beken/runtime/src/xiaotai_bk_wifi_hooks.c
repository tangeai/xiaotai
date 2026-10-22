#include "xiaotai_log.h"

/*
 * In the BK7258 SMP layout the CP owns the Wi-Fi state machine.  The AP lwIP
 * port still references two legacy application hooks that are declared by
 * bk_wifi but intentionally have no AP implementation.  Link them here; the
 * XiaoTai network adapter observes link/IP events through the public Beken
 * event API, so these hooks must not maintain a second connection state.
 */

#define TAG "xiaotai_wifi"

void wifi_netif_call_status_cb_when_sta_dhcp_timeout(void)
{
    BK_LOGD(TAG, "legacy AP DHCP-timeout hook\n");
}

void wifi_netif_notify_sta_disconnect(void)
{
    BK_LOGD(TAG, "legacy AP STA-disconnect hook\n");
}
