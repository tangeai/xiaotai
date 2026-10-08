#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
source_file="$project_dir/components/starter_media/src/starter_media.c"
adapter_file="$repo_dir/boards/lckfb/esp32s3/board_adapter.c"
board_config="$repo_dir/boards/lckfb/esp32s3/board_config.h"
source_compact="$(tr -d '[:space:]' < "$source_file")"
adapter_compact="$(tr -d '[:space:]' < "$adapter_file")"
board_compact="$(tr -d '[:space:]' < "$board_config")"
capture_body="$(sed -n '/static void audio_capture_task/,/static bool play_audio_item/p' "$source_file")"
allocation_count="$(rg -c '^[[:space:]]*esp_err_t[[:space:]]+[^=]+=[[:space:]]*i2s_new_channel\(' "$adapter_file")"

if [[ "$allocation_count" != "1" ]]; then
    echo "FAIL: full-duplex audio must allocate paired TX/RX in one call; found $allocation_count calls" >&2
    exit 1
fi

if [[ "$board_compact" != *'#defineI2S_AUDIO_PORTI2S_NUM_0'* ]]; then
    echo 'FAIL: selected board must use I2S0 for full-duplex audio' >&2
    exit 1
fi

for token in \
    'i2s_new_channel(&audio_channel,&s_i2s_tx,&s_i2s_rx)' \
    'i2s_channel_init_std_mode(s_i2s_tx,&tx_config)' \
    'i2s_channel_init_tdm_mode(s_i2s_rx,&rx_config)' \
    'I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO)' \
    'I2S_TDM_SLOT0|I2S_TDM_SLOT1|I2S_TDM_SLOT2|I2S_TDM_SLOT3' \
    'audio_codec_new_i2s_data(&i2s_cfg)' \
    'esp_codec_dev_open(s_speaker_dev,&speaker_format)' \
    'esp_codec_dev_open(s_microphone_dev,&microphone_format)' \
    '.channel=AUDIO_TDM_SLOTS' \
    'ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)|ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)'; do
    if [[ "$adapter_compact" != *"$token"* ]]; then
        echo "FAIL: missing full-duplex I2S invariant: $token" >&2
        exit 1
    fi
done

if [[ "$(rg -o 'I2S_MCLK_MULTIPLE_256' "$adapter_file" | wc -l)" -lt 2 ]]; then
    echo "FAIL: 16 kHz TX standard-I2S and RX four-slot TDM must both use 256 Fs MCLK" >&2
    exit 1
fi

for token in \
    'EXT_RAM_BSS_ATTRstaticint16_ts_play_stereo' \
    's_play_stereo[output_index]=needs_upsample?midpoint:current' \
    's_play_stereo[output_index+2U]=current' \
    'AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT*sizeof(int16_t)' \
    's_audio_adapter->write_pcm(' \
    's_audio_adapter->read_pcm('; do
    if [[ "$source_compact" != *"$token"* ]]; then
        echo "FAIL: ES8311 TX must use ordered interleaved 16-bit stereo PCM: $token" >&2
        exit 1
    fi
done

for token in \
    'esp_codec_dev_write(s_speaker_dev,(void*)samples,bytes)' \
    'esp_codec_dev_read(s_microphone_dev,samples,bytes)' \
    'esp_codec_dev_set_out_vol(s_speaker_dev,70)' \
    'esp_codec_dev_set_in_channel_gain(s_microphone_dev,'; do
    if [[ "$adapter_compact" != *"$token"* ]]; then
        echo "FAIL: board adapter must own codec operation: $token" >&2
        exit 1
    fi
done

if [[ "$source_compact" == *'i2s_channel_enable(s_i2s_tx)'* ||
      "$source_compact" == *'i2s_channel_enable(s_i2s_rx)'* ||
      "$source_compact" == *'es8311_sample_frequency_config('* ||
      "$source_compact" == *'es7210_init('* ]]; then
    echo "FAIL: codec adapter must own duplex enable/configuration; direct codec or I2S ownership found" >&2
    exit 1
fi

if rg -q 'I2S_NUM_1|i2s_channel_reconfig_(std|tdm)_gpio' "$source_file" "$adapter_file"; then
    echo "FAIL: paired full-duplex must not use a second master or GPIO handoff" >&2
    exit 1
fi

if rg -q 's_playback_active' <<<"$capture_body"; then
    echo "FAIL: full-duplex capture must continue while downlink audio is playing" >&2
    exit 1
fi

echo "PASS: esp_codec_dev owns shared ES7210 RX/ES8311 TX I2S0 clocks and concurrent media"
