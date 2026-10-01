#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
media_source="$project_dir/components/starter_media/src/starter_media.c"
media_common="$repo_dir/platforms/esp-idf/components/starter_media_common"
aec_source="$media_common/src/starter_aec.c"
aec_header="$media_common/include/starter_aec.h"
resampler_source="$media_common/src/starter_audio_resampler.c"
resampler_header="$media_common/include/starter_audio_resampler.h"
manifest="$project_dir/components/starter_media/idf_component.yml"
lock_file="$project_dir/dependencies.lock"
component_cmake="$project_dir/components/starter_media/CMakeLists.txt"
board_config="$repo_dir/boards/lckfb/esp32s3/board_config.h"

media_compact="$(tr -d '[:space:]' < "$media_source")"
aec_compact="$(tr -d '[:space:]' < "$aec_source")"
header_compact="$(tr -d '[:space:]' < "$aec_header")"
cmake_compact="$(tr -d '[:space:]' < "$component_cmake")"
board_compact="$(tr -d '[:space:]' < "$board_config")"

if ! rg -q 'version:[[:space:]]*"==2\.4\.7"' "$manifest" ||
   ! sed -n '/espressif\/esp-sr:/,/^  [^ ]/p' "$lock_file" |
       rg -q 'version:[[:space:]]*2\.4\.7'; then
    echo "FAIL: Espressif esp-sr must be reproducibly pinned to 2.4.7" >&2
    exit 1
fi

for token in \
    'STARTER_AEC_SAMPLE_RATE_HZ=16000' \
    'STARTER_AEC_TRANSPORT_RATE_HZ=8000' \
    'STARTER_AEC_CAPTURE_DMA_CHANNELS=2' \
    'STARTER_AEC_MIC_SLOT=0' \
    'STARTER_AEC_REFERENCE_SLOT=1'; do
    if [[ "$header_compact" != *"$token"* ]]; then
        echo "FAIL: missing AEC board invariant: $token" >&2
        exit 1
    fi
done

for token in \
    '.mic_num=1' \
    '.ref_num=1' \
    '.sample_rate=STARTER_AEC_SAMPLE_RATE_HZ' \
    '.mode=AEC_MODE_FD_LOW_COST' \
    '.nlp_level=AEC_NLP_LEVEL_AGGR' \
    'aec_create_from_config(&config)' \
    'aec_get_chunksize(s_aec.handle)' \
    'aec_process(s_aec.handle,s_aec.mic,s_aec.reference,s_aec.clean)' \
    's_aec.tdm[i*STARTER_AEC_CAPTURE_DMA_CHANNELS+STARTER_AEC_MIC_SLOT]' \
    's_aec.tdm[i*STARTER_AEC_CAPTURE_DMA_CHANNELS+STARTER_AEC_REFERENCE_SLOT]' \
    'starter_audio_resampler_16k_to_8k_process(&s_aec.resampler,'; do
    if [[ "$aec_compact" != *"$token"* ]]; then
        echo "FAIL: missing full-duplex AEC invariant: $token" >&2
        exit 1
    fi
done

if ! rg -q 'STARTER_AUDIO_RESAMPLER_TAPS 31U' "$resampler_header" ||
   ! rg -q 'resampler->emit_phase' "$resampler_source"; then
    echo "FAIL: 16 kHz to 8 kHz path must use the stateful 31-tap anti-alias resampler" >&2
    exit 1
fi

if rg -q 'sum[[:space:]]*/[[:space:]]*2|clean\[i \* 2U\]' "$aec_source"; then
    echo "FAIL: two-point decimation aliases high frequencies into A-law uplink" >&2
    exit 1
fi

if rg -q 'STARTER_AEC_TDM_SLOTS|frame_samples \* 4U' "$aec_source" "$aec_header"; then
    echo "FAIL: ES7210 selected slots are compacted to two DMA channels, not four TDM strides" >&2
    exit 1
fi

for token in \
    '#defineAUDIO_TRANSPORT_SAMPLE_RATE_HZ8000U' \
    'starter_aec_process_capture(capture_bytes,&clean)' \
    'packet_pcm[packet_samples++]=clean.pcm_8k[i]' \
    'AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT(AUDIO_PLAYBACK_UPSAMPLE*2U)' \
    's_play_stereo[output_index+2U]=midpoint'; do
    if [[ "$media_compact" != *"$token"* ]]; then
        echo "FAIL: missing 16 kHz AEC / 8 kHz transport bridge: $token" >&2
        exit 1
    fi
done

for token in \
    '#defineAUDIO_HW_SAMPLE_RATE_HZ16000U' \
    '#defineAUDIO_MCLK_MULTIPLE256U'; do
    if [[ "$board_compact" != *"$token"* ]]; then
        echo "FAIL: missing board audio clock invariant: $token" >&2
        exit 1
    fi
done

if [[ "$cmake_compact" != *'starter_media_common/CMakeLists.txt'* ||
      "$cmake_compact" != *'esp-sr'* ]]; then
    echo "FAIL: starter_media must compile and link the esp-sr AEC adapter" >&2
    exit 1
fi

if rg -q 's_playback_active|STARTER_AEC_REFERENCE_SLOT[[:space:]]*=[[:space:]]*0' \
        "$media_source" "$aec_header"; then
    echo "FAIL: AEC build must not pause capture or alias the reference to MIC1" >&2
    exit 1
fi

echo "PASS: ESP-SR 2.4.7 FD_LOW_COST uses MIC1/MIC3 and stateful anti-alias 16 kHz->8 kHz G.711 bridge"
