#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 "$repo_dir/tools/tests/test_contact_pagination_layout.py"
python3 "$repo_dir/tools/tests/test_tip_layout.py"
python3 "$repo_dir/tools/tests/test_room_keypad.py"
python3 "$repo_dir/tools/tests/test_p4_ai_playback.py"
python3 "$repo_dir/tools/tests/test_bk_ai_playback.py"
python3 "$repo_dir/tools/tests/test_shared_ui_icons.py"
python3 "$repo_dir/tools/tests/test_temperature_font_symbols.py"
python3 "$repo_dir/tools/tests/test_s3_product_regressions.py"
python3 "$repo_dir/tools/tests/test_s3_room_refresh.py"
python3 "$repo_dir/tools/tests/test_s3_async_regressions.py"
python3 "$repo_dir/tools/tests/test_s3_opus_media.py"
python3 "$repo_dir/tools/tests/test_s3_audio_cadence.py"
python3 "$repo_dir/tools/tests/test_s3_jpeg_performance.py"
python3 "$repo_dir/tools/tests/test_s3_h5_cadence.py"
python3 "$repo_dir/tools/tests/test_s3_main_key.py"
python3 "$repo_dir/tools/tests/test_s3_voip_audio.py"
python3 "$repo_dir/tools/tests/test_firmware_versions.py"
bash "$repo_dir/tools/run_host_tests.sh"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_video_regions.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_media_performance_log.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_call_cleanup.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_full_frame_uplink.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_video_pack.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/test_voip_profile.py"
python3 "$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools/verify_downlink_capture.py"
python3 "$repo_dir/tools/run_c_sanitizers.py"
python3 "$repo_dir/tools/product_coverage.py"
python3 "$repo_dir/tools/build.py" --validate
git -C "$repo_dir" diff --check

echo "XiaoTai repository checks passed"
