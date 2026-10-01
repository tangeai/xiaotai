#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
defaults="$project_dir/sdkconfig.defaults"
resolved="$project_dir/sdkconfig"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"
platform_source="$repo_dir/platforms/esp-idf/components/platform_client/src/platform_client.c"
media_source="$project_dir/components/starter_media/src/starter_media.c"
aec_source="$repo_dir/platforms/esp-idf/components/starter_media_common/src/starter_aec.c"
runtime_source="$repo_dir/platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
product_source="$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c"
button_source="$repo_dir/platforms/esp-idf/components/starter_button/src/starter_button.c"

require_config() {
    local file="$1"
    local value="$2"
    if ! grep -qxF "$value" "$file"; then
        echo "FAIL: $(basename "$file") must contain $value" >&2
        exit 1
    fi
}

require_config "$defaults" "# CONFIG_CAMERA_PSRAM_DMA is not set"
require_config "$resolved" "# CONFIG_CAMERA_PSRAM_DMA is not set"
require_config "$defaults" "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"
require_config "$resolved" "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"
require_config "$defaults" "# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set"
require_config "$resolved" "# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set"
require_config "$defaults" "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y"
require_config "$resolved" "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y"
require_config "$defaults" "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4"
require_config "$resolved" "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4"
require_config "$defaults" "CONFIG_ESP_WIFI_RX_BA_WIN=4"
require_config "$resolved" "CONFIG_ESP_WIFI_RX_BA_WIN=4"
require_config "$defaults" "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y"
require_config "$resolved" "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y"

main_compact="$(tr -d '[:space:]' < "$main_source")"
platform_compact="$(tr -d '[:space:]' < "$platform_source")"
media_compact="$(tr -d '[:space:]' < "$media_source")"
aec_compact="$(tr -d '[:space:]' < "$aec_source")"
runtime_compact="$(tr -d '[:space:]' < "$runtime_source")"
product_compact="$(tr -d '[:space:]' < "$product_source")"
button_compact="$(tr -d '[:space:]' < "$button_source")"

if [[ "$main_compact" != *'heap_caps_register_failed_alloc_callback(allocation_failed)'* ||
      "$main_compact" != *'platform_client_run_request_loop()'* ||
      "$main_compact" != *'#defineSTARTER_TASK_STACK_BYTES13312U'* ||
      "$main_compact" != *'xTaskCreate(starter_start_task,"starter_start",STARTER_TASK_STACK_BYTES,NULL,4,NULL)'* ||
      "$main_compact" != *'temporarilydisabletheflashcache'* ]]; then
    echo "FAIL: startup must register allocation diagnostics and keep its NVS/model-loading worker stack cache-safe in internal SRAM" >&2
    exit 1
fi

if [[ "$main_compact" != *'xTaskCreateWithCaps(platform_request_task,"platform_http",PLATFORM_REQUEST_TASK_STACK_BYTES,NULL,4,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$main_compact" != *'platformrequestloopmovedtoPSRAM'* ||
      "$runtime_compact" != *'xTaskCreateWithCaps(runtime_task,"starter_session",RUNTIME_TASK_STACK_BYTES,NULL,6,&s_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$product_compact" != *'.task_stack_caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT'* ||
      "$button_compact" != *'xTaskCreateWithCaps(button_task,"ai_button",BUTTON_TASK_STACK_BYTES,NULL,BUTTON_TASK_PRIORITY,&s_button_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ]]; then
    echo "FAIL: HTTP, button, session, UI long-lived memory must remain in PSRAM" >&2
    exit 1
fi

if [[ "$platform_compact" == *'xTaskCreate(request_task'* ||
      "$platform_compact" != *'heap_caps_calloc(PLATFORM_REQUEST_QUEUE_DEPTH,sizeof(*s_request_pool),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$platform_compact" != *'heap_caps_malloc(PLATFORM_HTTP_BODY_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$platform_compact" != *'xQueueCreate(PLATFORM_REQUEST_QUEUE_DEPTH,sizeof(uint8_t))'* ]]; then
    echo "FAIL: platform requests must use a PSRAM pool, byte-index queues, and the deferred startup task" >&2
    exit 1
fi

if [[ "$media_compact" != *'heap_caps_calloc(AUDIO_RX_QUEUE_DEPTH,sizeof(*s_audio_rx_pool),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'heap_caps_malloc(JPEG_BUFFER_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'xQueueCreate(AUDIO_RX_QUEUE_DEPTH,sizeof(uint8_t))'* ||
      "$media_compact" != *'xTaskCreateWithCaps(audio_sink_task,"board_audio_rx",6144,NULL,8,NULL,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'xTaskCreatePinnedToCoreWithCaps(audio_capture_task,"board_audio_tx",6144,NULL,7,NULL,MEDIA_REALTIME_CORE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'xTaskCreatePinnedToCoreWithCaps(camera_task,"board_mjpeg",8192,NULL,5,NULL,MEDIA_REALTIME_CORE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'frame2jpg_cb('* ]]; then
    echo "FAIL: media must use PSRAM audio/JPEG pools, stacks, and byte-index queues" >&2
    exit 1
fi

if [[ "$aec_compact" != *'.caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT'* ||
      "$aec_compact" != *'heap_caps_aligned_calloc('* ||
      "$aec_compact" != *'MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT'* ||
      "$aec_compact" != *'MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT'* ]]; then
    echo "FAIL: AEC state/work frames must use aligned PSRAM while its I2S destination stays internal" >&2
    exit 1
fi

echo "PASS: cache-safe internal startup, PSRAM session/media stacks, GC2145 staging DMA, network BSS, dynamic Wi-Fi TX, AEC/media pools, and allocation diagnostics match policy"
