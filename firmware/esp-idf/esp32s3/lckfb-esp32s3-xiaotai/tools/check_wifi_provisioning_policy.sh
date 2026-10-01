#!/usr/bin/env bash
set -euo pipefail

root="${1:?usage: check_wifi_provisioning_policy.sh <project-root>}"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$root")"
wifi="$repo_dir/platforms/esp-idf/components/wifi_manager/src/wifi_manager.c"
dns="$repo_dir/platforms/esp-idf/components/wifi_manager/src/captive_dns.c"
product="$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c"

rg -Fq '"XiaoTai-%02X%02X"' "$wifi"
rg -Fq 'ap.ap.authmode = WIFI_AUTH_OPEN' "$wifi"
if rg -q 'WIFI_SETUP_PASSWORD|ap\.ap\.authmode = WIFI_AUTH_WPA' "$wifi"; then
    echo "FAIL: provisioning SoftAP must be open and have no password" >&2
    exit 1
fi
rg -Fq '#define WIFI_SETUP_URL "http://192.168.6.1"' "$wifi"
if rg -q 'ESP_NETIF_CAPTIVEPORTAL_URI' "$wifi"; then
    echo "FAIL: DHCP option 114 must not advertise the local HTTP/HTML setup page" >&2
    exit 1
fi
rg -Fq 'ESP_NETIF_DOMAIN_NAME_SERVER' "$wifi"
rg -Fq 'captive_dns_start(s_ap_netif)' "$wifi"
rg -Fq 'httpd_register_err_handler' "$wifi"
rg -Fq 'wildcard DNS ready on UDP/53' "$dns"
rg -Fq '无需密码，系统通常会提示打开配网页' "$product"
rg -Fq 'bool wifi_manager_connection_failed(void)' "$wifi"
rg -Fq 's_has_saved_credentials' "$wifi"
rg -Fq 'else if (!s_provisioning)' "$wifi"
rg -Fq 'PRODUCT_WIFI_CONNECTED_TEXT "Wi-Fi 连接成功"' "$product"
rg -Fq 'PRODUCT_WIFI_FAILED_TEXT "Wi-Fi 连接失败，请重新配网"' "$product"
rg -Fq 's_pending_wifi_notice' "$product"
rg -Fq 's_wifi_notice != WIFI_NOTICE_NONE' "$product"
rg -Fq '!wifi_notice_visible' "$product"

echo "Wi-Fi provisioning policy passed: open XiaoTai SoftAP and captive portal"
