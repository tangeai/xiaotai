#!/usr/bin/env bash
set -euo pipefail

project_root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_root")"
platform_source="$repo_dir/platforms/esp-idf/components/platform_client/src/platform_client.c"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"

require_text() {
    local text=$1
    local file=$2
    local reason=$3
    if ! rg -Fq "$text" "$file"; then
        echo "time sync policy failed: $reason" >&2
        exit 1
    fi
}

require_text 'ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(' "$platform_source" \
    'the product clock must configure both primary and fallback servers'
require_text 'ESP_SNTP_SERVER_LIST(s_ntp_server, "ntp.aliyun.com")' \
    "$platform_source" 'the persisted primary must retain Aliyun fallback'
require_text 'runtime_config_load_ntp_server(s_ntp_server' "$platform_source" \
    'the primary server must load the persistent override'
require_text 'snprintf(s_ntp_server, sizeof(s_ntp_server), "%s", "pool.ntp.org")' "$platform_source" \
    'an absent or invalid override must retain the original primary'
require_text 'CONFIG_LWIP_SNTP_MAX_SERVERS=2' "$project_root/sdkconfig.defaults" \
    'the generated project must retain two SNTP server slots'
require_text 'esp_netif_sntp_sync_wait' "$platform_source" \
    'platform authentication must wait for the background SNTP result'
require_text 'NTP_SYNC_BACKGROUND_TASK' "$main_source" \
    'SNTP synchronization must run outside app_main'
require_text 'setenv("TZ", "CST-8", 1)' "$main_source" \
    'the S3 product clock must render Asia/Shanghai local time'
require_text 'xTaskCreate(starter_start_task' "$main_source" \
    'network clock work must run on the existing startup worker'
require_text 'temporarily disable the flash cache' "$main_source" \
    'the NVS/model-loading worker must document why its stack stays internal'

echo 'time sync policy passed'
