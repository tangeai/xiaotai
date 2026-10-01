#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
szpi_tools="$repo_dir/firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/tools"
atk_tests="$repo_dir/firmware/esp-idf/esp32s3/atk-dnesp32s3/tests"
bk7258_tools="$repo_dir/firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools"
p4_43_tools="$repo_dir/firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/tools"

python3 -m unittest discover -s "$repo_dir/tools/tests" -p 'test_*.py'

# Board suites are intentionally invoked from here instead of relying on the
# repository-level discovery.  Board directories contain standalone contract
# scripts as well as unittest modules, and both forms fail the command when a
# product or driver invariant regresses.
python3 -m unittest discover -s "$bk7258_tools" -p 'test_*.py'
bash "$bk7258_tools/test_audio_policy.sh"
bash "$bk7258_tools/test_audio_agc.sh"
bash "$bk7258_tools/test_audio_ns.sh"
bash "$bk7258_tools/test_log_levels.sh"
bash "$bk7258_tools/test_network_state.sh"
bash "$bk7258_tools/test_pcm_stream.sh"
bash "$bk7258_tools/test_storage.sh"
bash "$bk7258_tools/test_touch_transform.sh"
bash "$bk7258_tools/test_touch_policy.sh"
bash "$bk7258_tools/test_ui_hit_test.sh"
bash "$bk7258_tools/test_video_pacer.sh"

python3 -m unittest discover -s "$p4_43_tools" -p 'test_*.py'

python3 "$szpi_tools/test_voice_reliability.py"
python3 "$szpi_tools/test_release_assets.py"
python3 "$szpi_tools/test_captive_portal_contract.py"
python3 "$szpi_tools/test_station_identity.py"
python3 "$szpi_tools/test_voip_task_cleanup.py"
python3 "$szpi_tools/test_contact_entry.py"
python3 "$szpi_tools/test_screen_event_lifetime.py"
bash "$szpi_tools/test_audio_resampler.sh"
bash "$szpi_tools/test_camera_sensor_policy.sh"
bash "$szpi_tools/test_jpeg_collector.sh"
bash "$atk_tests/run_host_tests.sh"
