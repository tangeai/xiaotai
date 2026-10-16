#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd "$(dirname "$0")/.." && pwd)
source "$project_dir/tools/repository.sh"
workspace_dir=$(xiaotai_find_repository_root "$project_dir")
toolchain_bin="${XIAOTAI_TOOLCHAIN_DIR:-$workspace_dir/.tools/gcc-arm-none-eabi-10.3-2021.10/bin}"
python_bin="${XIAOTAI_PYTHON_BIN:-$workspace_dir/.tools/bk-python/bin}"
sdk_dir="${SDK_DIR:-$workspace_dir/.tools/bk_avdk_smp-release-v3.1.1.10}"
expected_sdk_commit="398cab28b938e69ba2071a597daf00845dd080b6"
ap_config="$project_dir/ap/config/bk7258_ap/config"
cp_config="$project_dir/cp/config/bk7258/config"
ap_release_config="$project_dir/ap/config/bk7258_ap/release.config"
cp_release_config="$project_dir/cp/config/bk7258/release.config"
tirtc_archive="$project_dir/third_party/tirtc/lib/libTiRTC.a"
tirtc_manifest="$project_dir/third_party/tirtc/manifest/build-info.env"
expected_tirtc_sha256="1a6327ebed1d74b5008d56896a2110dc872633321124ad1267eeb740a80d4cf9"
sdk_patches=(
    "$project_dir/tools/patches/bk-avdk-scheduler-snapshot.patch"
    "$project_dir/tools/patches/bk-avdk-dvp-sccb-diagnostics.patch"
    "$project_dir/tools/patches/bk-avdk-lwip-tcp-pcb-12.patch"
    "$project_dir/tools/patches/bk-avdk-captive-dns.patch"
    "$project_dir/tools/patches/bk-avdk-gc0308-20fps.patch"
    "$project_dir/tools/patches/bk-avdk-redact-sensitive-logs.patch"
    "$project_dir/tools/patches/bk-avdk-touch-read-failure-release.patch"
    "$project_dir/tools/patches/bk-avdk-ft6336-transient-log-level.patch"
    "$project_dir/tools/patches/bk-avdk-touch-active-release-poll.patch"
    "$project_dir/tools/patches/bk-avdk-wifi-skb-dequeue-guard.patch"
    "$project_dir/tools/patches/bk-avdk-ap-tx-owned-buffer.patch"
    "$project_dir/tools/patches/bk-avdk-wifi-command-list-mutex.patch"
    "$project_dir/tools/patches/bk-avdk-pbuf-owner-diagnostics.patch"
    "$project_dir/tools/patches/bk-avdk-speaker-close-diagnostics.patch"
    "$project_dir/tools/patches/bk-avdk-heartbeat-8s.patch"
    "$project_dir/tools/patches/bk-avdk-heartbeat-owner-diagnostics.patch"
    "$project_dir/tools/patches/bk-avdk-ipc-worker-sram.patch"
    "$project_dir/tools/patches/bk-avdk-rtos-assert-caller-diagnostics.patch"
)

export XIAOTAI_TOOLCHAIN_DIR="$toolchain_bin"
export PATH="$python_bin:$toolchain_bin:$PATH"

actual_sdk_commit=$(git -C "$sdk_dir" rev-parse HEAD 2>/dev/null || true)
if [ "$actual_sdk_commit" != "$expected_sdk_commit" ]; then
    echo "Refusing unverified BK-AVDK SDK: $actual_sdk_commit" >&2
    echo "Expected release/v3.1.1.10: $expected_sdk_commit" >&2
    exit 1
fi

build_profile="${XIAOTAI_BUILD_PROFILE:-development}"
build_purpose="${XIAOTAI_BUILD_PURPOSE:-Local development and debugging}"
firmware_version=$(sed -nE 's/^`([0-9]+\.[0-9]+\.[0-9]+)`$/\1/p' \
    "$project_dir/VERSION.md")
if [[ ! "$firmware_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "VERSION.md must contain one major.minor.patch firmware version" >&2
    exit 1
fi
config_args=()
artifact_project_name="$(basename "$project_dir")"
snapshot_source=""
cleanup_build() {
    if [ -n "$snapshot_source" ]; then
        python3 "$project_dir/tools/instrument_critical_lr.py" restore \
            --sdk-file "$snapshot_source" --backup "$build_root/heartbeat.before.c" \
            --contract "$build_root/heartbeat.symbols" \
            --expected-source-sha256 "$snapshot_source_sha"
    fi
    if [ "$build_profile" = release ]; then
        rm -f "$ap_release_config" "$cp_release_config"
    fi
}
trap cleanup_build EXIT
if [ "$build_profile" = release ]; then
    if [ -e "$ap_release_config" ] || [ -e "$cp_release_config" ]; then
        echo "Refusing to overwrite an existing release.config" >&2
        exit 1
    fi
    python3 "$project_dir/tools/prepare_release_config.py" \
        "$ap_config" "$ap_release_config"
    python3 "$project_dir/tools/prepare_release_config.py" \
        "$cp_config" "$cp_release_config"
    config_args+=(BK_CONFIG_FILE=release)
    artifact_project_name="${artifact_project_name}_release"
    build_purpose="${XIAOTAI_BUILD_PURPOSE:-Online release candidate}"
elif [ "$build_profile" != development ]; then
    echo "Unknown XIAOTAI_BUILD_PROFILE: $build_profile" >&2
    exit 2
fi

# Lock the prebuilt SDK and its platform-crypto contract. A differently named or
# locally rebuilt archive must be admitted deliberately instead of silently
# changing the firmware's ABI.
actual_tirtc_sha256=$(sha256sum "$tirtc_archive" | cut -d ' ' -f 1)
if [ "$actual_tirtc_sha256" != "$expected_tirtc_sha256" ]; then
    echo "Refusing unverified TiRTC archive: $actual_tirtc_sha256" >&2
    echo "Expected BK7258 v2.5.0 mini archive: $expected_tirtc_sha256" >&2
    exit 1
fi

for fact in \
    'TARGET=beken-bk7258' \
    'BUILD_PLATFORM_SDK_VERSION=3.1.1.10-official-398cab28' \
    'SSL=disabled' \
    'DTLS=disabled' \
    'CRYPTO_PROVIDER=platform-sdk' \
    'CRYPTO_COMPONENT=psa_mbedtls' \
    'CRYPTO_VERSION=3.5.2' \
    'BUNDLED_MBEDTLS=no'; do
    if ! grep -Fxq "$fact" "$tirtc_manifest"; then
        echo "TiRTC manifest does not satisfy required contract: $fact" >&2
        exit 1
    fi
done

if ! grep -Fq 'CONFIG_PSA_MBEDTLS=y' "$ap_config"; then
    echo "TiRTC requires the BK SDK psa_mbedtls component" >&2
    exit 1
fi

for sdk_patch in "${sdk_patches[@]}"; do
    # The pinned SDK contains sources whose whitespace can differ after CRLF
    # normalization. Allow whitespace variation while still requiring each
    # patch hunk to match the surrounding source context.
    if git -C "$sdk_dir" apply --check --ignore-whitespace "$sdk_patch" 2>/dev/null; then
        git -C "$sdk_dir" apply --ignore-whitespace "$sdk_patch"
    elif git -C "$sdk_dir" apply --reverse --check --ignore-whitespace "$sdk_patch" 2>/dev/null; then
        : # Project SDK patch is already applied.
    else
        echo "SDK patch does not match $sdk_dir: $sdk_patch" >&2
        exit 1
    fi
done

build_root="$project_dir/build/bk7258/$artifact_project_name"
for cache in "$build_root/bk7258_ap/CMakeCache.txt" \
             "$build_root/bk7258/CMakeCache.txt"; do
    if [ -f "$cache" ] &&
       { ! grep -Fq "beken-armino_SOURCE_DIR:STATIC=$sdk_dir/" "$cache" ||
         ! grep -Fq "CMAKE_TOOLCHAIN_FILE:FILEPATH=$sdk_dir/" "$cache"; }; then
        echo "Refusing mixed-SDK build: stale cache $cache" >&2
        echo "Move or remove $build_root, then rebuild with the pinned SDK." >&2
        exit 1
    fi
done

source_commit=$(git -C "$workspace_dir" rev-parse HEAD)
python3 "$project_dir/tools/build_identity.py" prepare \
    "$build_root" "$firmware_version" "$source_commit" \
    "$project_dir/BUILD_NUMBER" "$build_profile" "$build_purpose"
build_version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["build_version"])' \
    "$build_root/build_identity.json")
make -C "$project_dir" bk7258 SDK_DIR="$sdk_dir" \
    APP_VERSION="$build_version" "${config_args[@]}"

ap_elf="$project_dir/build/bk7258/$artifact_project_name/bk7258_ap/app.elf"
ap_map="${ap_elf%.elf}.map"
ap_nm="${ap_elf%.elf}.nm"
nm_tool="$toolchain_bin/arm-none-eabi-nm"
"$nm_tool" "$ap_elf" > "$ap_nm"
if [ "$build_profile" = development ] &&
   grep -Fxq 'CONFIG_CRITICAL_LR_RECORD=y' "$ap_config"; then
    snapshot_candidate="$sdk_dir/cp/middleware/driver/mailbox/mb_ipc_heartbeat.c"
    snapshot_source_sha=$(sha256sum "$snapshot_candidate" | cut -d ' ' -f 1)
    python3 "$project_dir/tools/instrument_critical_lr.py" apply \
        --sdk-file "$snapshot_candidate" --nm-file "$ap_nm" \
        --backup "$build_root/heartbeat.before.c" \
        --contract "$build_root/heartbeat.symbols" \
        --expected-source-sha256 "$snapshot_source_sha"
    snapshot_source="$snapshot_candidate"
    make -C "$project_dir" bk7258 SDK_DIR="$sdk_dir" APP_VERSION="$build_version"
    "$nm_tool" "$ap_elf" > "$ap_nm"
    python3 "$project_dir/tools/instrument_critical_lr.py" verify \
        --sdk-file "$snapshot_source" --nm-file "$ap_nm" \
        --backup "$build_root/heartbeat.before.c" \
        --contract "$build_root/heartbeat.symbols"
fi
if ! grep -Fq 'armino/psa_mbedtls/libpsa_mbedtls.a(md5.c.obj)' "$ap_map" ||
   ! grep -Fq ' T mbedtls_md5' "$ap_nm"; then
    echo "Final AP image did not resolve TiRTC crypto through the BK psa_mbedtls provider" >&2
    exit 1
fi
if grep -Fq 'mbedtls_x509write_crt_init' "$ap_nm"; then
    echo "DTLS-disabled TiRTC image unexpectedly linked the X.509 certificate writer" >&2
    exit 1
fi
package_dir="$project_dir/build/bk7258/$artifact_project_name/package"
python3 "$project_dir/tools/test_contract.py"
python3 "$project_dir/tools/build_identity.py" finalize \
    "$build_root" "$package_dir/all-app.bin" "$ap_elf"
sha256sum "$package_dir/all-app.bin" "$package_dir/app_pack.rbl"
echo "firmware-version: $build_version"
