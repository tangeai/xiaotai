#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd "$(dirname "$0")/.." && pwd)
source "$project_dir/tools/repository.sh"
workspace_dir=$(xiaotai_find_repository_root "$project_dir")
toolchain_bin="${XIAOTAI_TOOLCHAIN_DIR:-$workspace_dir/.tools/gcc-arm-none-eabi-10.3-2021.10/bin}"
python_bin="${XIAOTAI_PYTHON_BIN:-$workspace_dir/.tools/bk-python/bin}"
sdk_dir="${SDK_DIR:-$workspace_dir/.tools/bk_avdk_smp-release-v3.1.1.8}"
ap_config="$project_dir/ap/config/bk7258_ap/config"
cp_config="$project_dir/cp/config/bk7258/config"
ap_release_config="$project_dir/ap/config/bk7258_ap/release.config"
cp_release_config="$project_dir/cp/config/bk7258/release.config"
tirtc_archive="$project_dir/third_party/tirtc/lib/libTiRTC.a"
tirtc_manifest="$project_dir/third_party/tirtc/manifest/build-info.env"
expected_tirtc_sha256="47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171"
sdk_patches=(
    "$project_dir/tools/patches/bk-avdk-dvp-sccb-diagnostics.patch"
    "$project_dir/tools/patches/bk-avdk-lwip-tcp-pcb-12.patch"
    "$project_dir/tools/patches/bk-avdk-captive-dns.patch"
    "$project_dir/tools/patches/bk-avdk-gc0308-20fps.patch"
    "$project_dir/tools/patches/bk-avdk-redact-sensitive-logs.patch"
    "$project_dir/tools/patches/bk-avdk-touch-read-failure-release.patch"
    "$project_dir/tools/patches/bk-avdk-wifi-skb-dequeue-guard.patch"
)

export XIAOTAI_TOOLCHAIN_DIR="$toolchain_bin"
export PATH="$python_bin:$toolchain_bin:$PATH"

build_profile="${XIAOTAI_BUILD_PROFILE:-development}"
config_args=()
artifact_project_name="$(basename "$project_dir")"
cleanup_release_configs() {
    rm -f "$ap_release_config" "$cp_release_config"
}
if [ "$build_profile" = release ]; then
    if [ -e "$ap_release_config" ] || [ -e "$cp_release_config" ]; then
        echo "Refusing to overwrite an existing release.config" >&2
        exit 1
    fi
    python3 "$project_dir/tools/prepare_release_config.py" \
        "$ap_config" "$ap_release_config"
    python3 "$project_dir/tools/prepare_release_config.py" \
        "$cp_config" "$cp_release_config"
    trap cleanup_release_configs EXIT
    config_args+=(BK_CONFIG_FILE=release)
    artifact_project_name="${artifact_project_name}_release"
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
    'BUILD_PLATFORM_SDK_VERSION=3.1.1.8-official-1cfd56af' \
    'SSL=disabled' \
    'DTLS=disabled' \
    'TLS_PROVIDER=platform-sdk' \
    'TLS_COMPONENT=psa_mbedtls' \
    'MBEDTLS_VERSION=3.5.2' \
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

make -C "$project_dir" bk7258 SDK_DIR="$sdk_dir" "${config_args[@]}"

ap_elf="$project_dir/build/bk7258/$artifact_project_name/bk7258_ap/app.elf"
ap_map="${ap_elf%.elf}.map"
ap_nm="${ap_elf%.elf}.nm"
nm_tool="$toolchain_bin/arm-none-eabi-nm"
"$nm_tool" "$ap_elf" > "$ap_nm"
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
sha256sum "$package_dir/all-app.bin" "$package_dir/app_pack.rbl"
