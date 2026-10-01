#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
media_source="$project_dir/components/starter_media/src/starter_media.c"
aec_header="$repo_dir/platforms/esp-idf/components/starter_media_common/include/starter_aec.h"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"
platform_source="$repo_dir/platforms/esp-idf/components/platform_client/src/platform_client.c"
tirtc_source="$repo_dir/platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"
sdkconfig_defaults="$project_dir/sdkconfig.defaults"
media_compact="$(tr -d '[:space:]' < "$media_source")"
aec_header_compact="$(tr -d '[:space:]' < "$aec_header")"
main_compact="$(tr -d '[:space:]' < "$main_source")"
platform_compact="$(tr -d '[:space:]' < "$platform_source")"
tirtc_compact="$(tr -d '[:space:]' < "$tirtc_source")"

if [[ "$aec_header_compact" != *'STARTER_AEC_MIC_SLOT=0'* ]]; then
    echo "FAIL: ES7210 TDM-I2S MIC1 must be read from serialized slot 0" >&2
    exit 1
fi

if [[ "$media_compact" != *'starter_tirtc_send_alaw('*'>=0)'* ||
      "$media_compact" != *'starter_tirtc_send_mjpeg('*'>=0)'* ]]; then
    echo "FAIL: TiRTC media send success must accept non-negative return values" >&2
    exit 1
fi

if [[ "$main_compact" != *'#defineDISCOVERY_URLCONFIG_XIAOTAI_DISCOVERY_URL'* ||
      "$platform_compact" != *'#definePLATFORM_DEFAULT_DISCOVERYCONFIG_XIAOTAI_DISCOVERY_URL'* ]]; then
    echo "FAIL: startup and platform must share the configured discovery endpoint" >&2
    exit 1
fi

if [[ "$tirtc_compact" == *'TiRtcSetOption(TIRTC_OPT_SERVICE_ENDPOINT'* ]]; then
    echo "FAIL: platform discovery transport must not override the TiRTC SDK endpoint" >&2
    exit 1
fi

if ! rg -q '^CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y$' "$sdkconfig_defaults" ||
   ! rg -q '^CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y$' "$sdkconfig_defaults"; then
    echo "FAIL: deferred HTTPS support must remain compiled into the firmware" >&2
    exit 1
fi

if [[ "$main_compact" != *'.max_send_buffer_bytes=256U*1024U'* ]]; then
    echo "FAIL: TiRTC send buffer must match the ESP32 reference value of 256 KiB" >&2
    exit 1
fi

if [[ "$media_compact" != *'#defineVIDEO_BACKPRESSURE_BYTES(192U*1024U)'* ]]; then
    echo "FAIL: video backpressure must remain below the 256 KiB TiRTC send limit" >&2
    exit 1
fi

echo "PASS: MIC1 slot, TiRTC send returns, HTTP discovery, deferred TLS and the 256 KiB buffer match policy"
