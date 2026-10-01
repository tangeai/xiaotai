#!/usr/bin/env bash
# ESP-SR remains for AEC; no command recognition implementation may reach ELF.
set -euo pipefail
elf="${1:?usage: check_wake_image_policy.sh <elf>}"
symbols="$(${CROSS_NM:-xtensa-esp32s3-elf-nm} --defined-only "$elf")"
if printf '%s\n' "$symbols" | rg ' (esp_mn_[[:alnum:]_]*|esp_srmodel_init|[[:alnum:]_]*multinet[[:alnum:]_]*)$'; then
    echo 'FAIL: removed MultiNet/model-loader implementation remains linked' >&2
    exit 1
fi
printf '%s\n' "$symbols" | rg ' wake_backend_run$' >/dev/null
printf '%s\n' "$symbols" | rg ' _binary_nihaoxiaotai_tflite_start$' >/dev/null
echo 'PASS: embedded Voicute model/backend present; no MultiNet or ESP-SR model-loader in final ELF'
