#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd "$(dirname "$0")/.." && pwd)
source "$project_dir/tools/repository.sh"
workspace_dir=$(xiaotai_find_repository_root "$project_dir")
tools_dir="$workspace_dir/.tools"
sdk="$tools_dir/bk_avdk_smp-release-v3.1.1.8"
sdk_url='https://gitee.com/bekencorp/bk_avdk_smp.git'
sdk_tag='release/v3.1.1.8'
sdk_commit='1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7'
archive="$tools_dir/downloads/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2"
toolchain="$tools_dir/gcc-arm-none-eabi-10.3-2021.10"
python_env="$tools_dir/bk-python"
toolchain_url='https://dl.bekencorp.com/d/tools/toolchain/arm/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2?sign=RXdqFf5PRB5upxFDS7UXTUtTsStCbrQgoAfcUDJe9M8=:0'
toolchain_sha256='97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3'

mkdir -p "$tools_dir/downloads"
if [ ! -d "$sdk/.git" ]; then
    git clone --depth 1 --branch "$sdk_tag" "$sdk_url" "$sdk"
fi
actual_sdk_commit=$(git -C "$sdk" rev-parse HEAD)
if [ "$actual_sdk_commit" != "$sdk_commit" ]; then
    echo "BK-AVDK identity mismatch: expected $sdk_commit, got $actual_sdk_commit" >&2
    exit 1
fi
if [ ! -x "$toolchain/bin/arm-none-eabi-gcc" ]; then
    if [ ! -s "$archive" ]; then
        curl -fL --retry 3 --connect-timeout 20 -o "$archive" "$toolchain_url"
    fi
    actual_toolchain_sha256=$(sha256sum "$archive" | cut -d ' ' -f 1)
    if [ "$actual_toolchain_sha256" != "$toolchain_sha256" ]; then
        echo "Refusing unverified toolchain archive: $actual_toolchain_sha256" >&2
        echo "Expected gcc-arm-none-eabi-10.3-2021.10: $toolchain_sha256" >&2
        exit 1
    fi
    tar --no-same-owner -xjf "$archive" -C "$tools_dir"
fi

if [ ! -x "$python_env/bin/python" ]; then
    python3 -m venv "$python_env"
fi
"$python_env/bin/python" -m pip install \
    pycryptodome click future click_option_group cryptography jinja2 PyYAML cbor2 intelhex

"$toolchain/bin/arm-none-eabi-gcc" --version | sed -n '1p'
echo "BK-AVDK ready: $sdk_tag ($actual_sdk_commit)"
echo "BK7258 build environment ready: $tools_dir"
