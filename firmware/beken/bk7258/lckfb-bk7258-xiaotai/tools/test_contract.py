#!/usr/bin/env python3
from pathlib import Path
import ast
import hashlib
import json
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[5]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


manifest = (ROOT / "third_party/tirtc/manifest/build-info.env").read_text()
for fact in (
    "SDK_VERSION=v2.5.0",
    "TARGET=beken-bk7258",
    "VARIANT=mini",
    "ABI=eabi-hard-float",
    "BUILD_TOOLCHAIN_VERSION=10.3.1",
    "BUILD_PLATFORM_SDK_VERSION=3.1.1.8-official-1cfd56af",
    "SSL=disabled",
    "DTLS=disabled",
    "TLS_PROVIDER=platform-sdk",
    "TLS_COMPONENT=psa_mbedtls",
    "MBEDTLS_VERSION=3.5.2",
    "BUNDLED_MBEDTLS=no",
):
    require(fact in manifest, f"missing locked TiRTC fact: {fact}")

archive = ROOT / "third_party/tirtc/lib/libTiRTC.a"
require(archive.stat().st_size > 100_000,
        "TiRTC archive is missing or unexpectedly small")
require(hashlib.sha256(archive.read_bytes()).hexdigest() ==
        "47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171",
        "TiRTC archive identity changed")

config = (ROOT / "ap/config/bk7258_ap/config").read_text()
config_sha256 = hashlib.sha256(config.encode()).hexdigest()
root_cmake = (ROOT / "CMakeLists.txt").read_text()
makefile = (ROOT / "Makefile").read_text()
build_script = (ROOT / "tools/build.sh").read_text()
sdk_camera_patch = (
    ROOT / "tools/patches/bk-avdk-dvp-sccb-diagnostics.patch"
).read_text()
sdk_lwip_patch = (
    ROOT / "tools/patches/bk-avdk-lwip-tcp-pcb-12.patch"
).read_text()
sdk_captive_dns_patch = (
    ROOT / "tools/patches/bk-avdk-captive-dns.patch"
).read_text()
sdk_gc0308_patch = (
    ROOT / "tools/patches/bk-avdk-gc0308-20fps.patch"
).read_text()
sdk_sensitive_log_patch = (
    ROOT / "tools/patches/bk-avdk-redact-sensitive-logs.patch"
).read_text()
sdk_touch_release_patch_path = (
    ROOT / "tools/patches/bk-avdk-touch-read-failure-release.patch"
)
require(sdk_touch_release_patch_path.exists(),
        "BK touch driver must distinguish controller read failures from an empty app queue")
sdk_touch_release_patch = sdk_touch_release_patch_path.read_text()
sdk_wifi_skb_patch_path = (
    ROOT / "tools/patches/bk-avdk-wifi-skb-dequeue-guard.patch"
)
require(sdk_wifi_skb_patch_path.exists(),
        "BK build must carry the CP Wi-Fi broken skb-list guard")
sdk_wifi_skb_patch = sdk_wifi_skb_patch_path.read_text()
require("if (skb && (!skb->next || !skb->prev))" in sdk_wifi_skb_patch and
        "list->next = (struct sk_buff *)list;" in sdk_wifi_skb_patch and
        "list->prev = (struct sk_buff *)list;" in sdk_wifi_skb_patch and
        "list->qlen = 0;" in sdk_wifi_skb_patch and
        "return skb;" in sdk_wifi_skb_patch,
        "CP Wi-Fi dequeue must not dereference a broken skb chain")
setup_script = (ROOT / "tools/setup_build_env.sh").read_text()
doctor_script = (ROOT / "tools/doctor.py").read_text()
identity_doc = (ROOT / "BUILD_IDENTITY.md").read_text()
provenance_doc = (ROOT / "SOURCE_PROVENANCE.md").read_text()
official_sdk_commit = "1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7"
require(all(official_sdk_commit in text for text in
            (setup_script, doctor_script, identity_doc, provenance_doc)) and
        "bk_avdk_smp-release-v3.1.1.8" in makefile and
        "bk_avdk_smp-release-v3.1.1.8" in build_script and
        "Refusing mixed-SDK build" in build_script and
        "CMAKE_TOOLCHAIN_FILE:FILEPATH=$sdk_dir/" in build_script and
        "beken-armino_SOURCE_DIR:STATIC=$sdk_dir/" in build_script and
        "3.1.1.8-20260605" not in makefile and
        "3.1.1.8-20260605" not in build_script,
        "build entry points must pin the official Gitee release/v3.1.1.8 commit")
require("97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3" in
        setup_script and
        "Refusing unverified toolchain archive" in setup_script,
        "BK setup must verify the pinned Arm GCC archive before extraction")
require("CONFIG_TANGE_CLOUD" not in config,
        "official Gitee baseline must not depend on the private Tange component")
require("CONFIG_CLI=y" in config and
        "CONFIG_CLI_CODE_SIZE_OPTIMIZE_ENABLE=y" in config and
        "CONFIG_MAX_COMMANDS=64" in config and
        "# CONFIG_WIFI_CLI_ENABLE is not set" in config and
        "# CONFIG_VFS_TEST is not set" in config and
        "# CONFIG_INTERRUPT_TEST is not set" in config and
        "# CONFIG_AON_PMU_TEST is not set" in config,
        "product serial commands require the BK CLI parser, with unused built-ins disabled")
require(sdk_camera_patch.count("+\tbk_gpio_pull_up(HWD_GPIO_I2C_SDA);") == 2 and
        sdk_camera_patch.count("+\tbk_gpio_pull_up(HWD_GPIO_I2C_SCL);") == 2 and
        "+\treturn I2cMemRead(id, mem_param) ? BK_OK : BK_FAIL;" in
        sdk_camera_patch and
        "+\treturn I2cMemWrite(id, mem_param) ? BK_OK : BK_FAIL;" in
        sdk_camera_patch and
        "config->i2c_config.scl_pin" in sdk_camera_patch and
        "config->i2c_config.sda_pin" in sdk_camera_patch,
        "SCCB patch must use pull-ups, propagate failures, and report active pins")
require('"$project_dir/tools/patches/bk-avdk-lwip-tcp-pcb-12.patch"' in
        build_script and
        "-#define MAX_SOCKETS_TCP 8" in sdk_lwip_patch and
        "+#define MAX_SOCKETS_TCP 12" in sdk_lwip_patch,
        "official SDK builds must reproducibly raise TCP PCB capacity to 12")
require('"$project_dir/tools/patches/bk-avdk-captive-dns.patch"' in
        build_script and
        "DNS_TYPE_A" in sdk_captive_dns_patch and
        "dhcps.router_ip" in sdk_captive_dns_patch and
        "hdr->answer_rrs = htons(1U)" in sdk_captive_dns_patch and
        "rcode = ERROR_REFUSED" in sdk_captive_dns_patch and
        "+\thdr->flags.num = htons(0x8000U" in sdk_captive_dns_patch,
        "official SDK DHCP DNS must answer captive-portal A queries")
require('"$project_dir/tools/patches/bk-avdk-gc0308-20fps.patch"' in
        build_script and
        "-    .clk = MCLK_24M" in sdk_gc0308_patch and
        "+    .clk = MCLK_20M" in sdk_gc0308_patch and
        "-    .def_fps = FPS25" in sdk_gc0308_patch and
        "+    .def_fps = FPS20" in sdk_gc0308_patch,
        "official SDK builds must reproducibly select the GC0308 20 fps profile")
require('"$project_dir/tools/patches/bk-avdk-redact-sensitive-logs.patch"' in
        build_script and
        'password_len=%u' in sdk_sensitive_log_patch and
        '+    WDRV_LOGD("sta config, ssid=%s password=%s' not in
        sdk_sensitive_log_patch and
        'value redacted' in sdk_sensitive_log_patch and
        'BK_LOGD(TAG,"%.*s' in sdk_sensitive_log_patch,
        "official SDK builds must redact Wi-Fi passwords and HTTP headers")
require('"$project_dir/tools/patches/bk-avdk-touch-read-failure-release.patch"' in
        build_script and
        "bool touch_active = false;" in sdk_touch_release_patch and
        "if (touch_active)" in sdk_touch_release_patch and
        "tp_data[0].event = TP_EVENT_TYPE_UP;" in sdk_touch_release_patch and
        "tp_data[0].x_coordinate = UINT16_MAX;" in sdk_touch_release_patch and
        "tp_data[0].y_coordinate = UINT16_MAX;" in sdk_touch_release_patch and
        "bk_tp_read_info_callback(&tp_data[0]);" in sdk_touch_release_patch and
        "touch_active = false;" in sdk_touch_release_patch,
        "actual FT6336 read failures must synthesize one release without treating an empty app queue as failure")
require("CONFIG_WIFI_SOFTAP=y" in config,
        "SoftAP provisioning must be enabled")
require("CONFIG_CJSON_USE=y" in config,
        "bounded provisioning JSON parser must be enabled")
require("CONFIG_DVP_CTRL_POWER_GPIO_ID=9" in config,
        "carrier-board camera power must use schematic DVP_PWR_CTL/P9")
require("CONFIG_WEBCLIENT=y" in config,
        "platform service discovery requires WebClient")
require("# CONFIG_HTTPS is not set" in config,
        "the unrelated PSA HTTPS component must remain disabled")
require("CONFIG_PSA_MBEDTLS=y" in config,
        "TiRTC crypto dependencies must resolve through the BK SDK component")
require("CONFIG_FULL_MBEDTLS=y" in config and "CONFIG_WEBCLIENT_TLS=y" in config,
        "the selected TiRTC/BK transport build requires full Mbed TLS support")
require("CONFIG_LOG_LEVEL=4" in config,
        "development builds must default to DEBUG for complete network diagnostics")
ap_cmake = (ROOT / "ap/CMakeLists.txt").read_text()
require("third_party/mbedtls" not in ap_cmake and
        "tirtc_mbedtls" not in ap_cmake and
        "hmac_sha_256" in ap_cmake and
        "psa_mbedtls" in ap_cmake and
        not (ROOT / "third_party/mbedtls").exists() and
        '"mbedtls/' not in (ROOT / "ap/src/xiaotai_platform_client.c").read_text(),
        "application transport must not compile or link a separate Mbed TLS")
require(all(component in ap_cmake for component in
            ("bk_vfs", "bk_rtos", "psa_mbedtls", "zlib")) and
        '"-Wl,--whole-archive"' in ap_cmake and
        '"-Wl,--no-whole-archive"' in ap_cmake,
        "TiRTC must link whole-archive with all package-declared BK dependencies")
require("libpsa_mbedtls.a(md5.c.obj)" in build_script and
        "T mbedtls_md5" in build_script and
        "mbedtls_x509write_crt_init" in build_script,
        "final-image gate must verify the platform crypto provider and reject the old X.509 writer path")
require("# CONFIG_OTA_FUNCTION is not set" in config,
        "OTA runtime must stay disabled until the product enables it")
lwip_values = {
    key: int(value)
    for key, value in re.findall(
        r"^(CONFIG_LWIP_(?:MEM_SIZE|MEM_MAX_RX_SIZE|MEM_MAX_TX_SIZE|TCP_SND_BUF|TCP_WND))=(\d+)$",
        config,
        re.MULTILINE,
    )
}
require(lwip_values.get("CONFIG_LWIP_MEM_SIZE") == 80 * 1024,
        "AP lwIP heap must preserve internal SRAM for TiRTC sessions")
require(lwip_values.get("CONFIG_LWIP_MEM_MAX_RX_SIZE", 0) <=
        lwip_values["CONFIG_LWIP_MEM_SIZE"] and
        lwip_values.get("CONFIG_LWIP_MEM_MAX_TX_SIZE", 0) <=
        lwip_values["CONFIG_LWIP_MEM_SIZE"],
        "lwIP RX/TX limits must fit the configured packet heap")
require(lwip_values.get("CONFIG_LWIP_TCP_SND_BUF") == 22 * 1460 and
        lwip_values.get("CONFIG_LWIP_TCP_WND") == 22 * 1460,
        "single-session TCP windows must retain the validated 22-MSS budget")

psram_total = 0
for line in (ROOT / "partitions/bk7258/ram_regions.csv").read_text().splitlines():
    stripped = line.split("#", 1)[0].strip()
    if not stripped or stripped.startswith("Name,"):
        continue
    columns = [column.strip() for column in stripped.split(",")]
    if len(columns) >= 4 and columns[1] == "PSRAM":
        psram_total += int(columns[3], 0)
require(psram_total == 16 * 1024 * 1024,
        f"PSRAM map covers {psram_total} bytes instead of 16 MiB")

ap_sources = "\n".join(
    path.read_text(errors="ignore")
    for path in (ROOT / "ap").rglob("*")
    if path.is_file() and path.suffix in {".c", ".h", ".txt"}
)
cp_sources = "\n".join(
    path.read_text(errors="ignore")
    for path in (ROOT / "cp").rglob("*")
    if path.is_file() and path.suffix in {".c", ".h", ".txt"}
)
probe_source = (ROOT / "ap/src/tirtc_bk_probe.c").read_text()
audio_probe_source = (ROOT / "ap/src/xiaotai_probe.c").read_text()
require(f'#define BOARD_CONFIG_SHA256 "{config_sha256}"' in probe_source,
        "boot probe board_config_sha256 must match the compiled AP config")
require("out->playback_reference = true;" in audio_probe_source and
        "out->aec_available = true;" in audio_probe_source and
        "out->aec_test = TIRTC_BK_TEST_NOT_RUN;" in audio_probe_source,
        "boot probe must advertise compiled AEC without claiming acoustic validation")
require("TiRtcInit" in ap_sources and "TiRtcStart" in ap_sources,
        "AP does not own the TiRTC lifecycle")
app_source = (ROOT / "ap/src/xiaotai_app.c").read_text()
ai_view_source = (REPO_ROOT / "product/src/xiaotai_ai_view.c").read_text()
call_protocol_source = (
    REPO_ROOT / "product/src/xiaotai_call_protocol.c"
).read_text()
room_source = (REPO_ROOT / "product/src/xiaotai_room.c").read_text()
signal_source = (REPO_ROOT / "product/src/xiaotai_signal.c").read_text()
tirtc_source = (ROOT / "ap/src/xiaotai_tirtc.c").read_text()
platform_source = (ROOT / "ap/src/xiaotai_platform_client.c").read_text()
audio_source = (ROOT / "ap/src/xiaotai_audio.c").read_text()
audio_agc_source = (ROOT / "ap/src/xiaotai_audio_agc.c").read_text()
audio_policy_source = (ROOT / "ap/src/xiaotai_audio_policy.c").read_text()
storage_source = (ROOT / "ap/src/xiaotai_storage.c").read_text()
board_audio_source = (REPO_ROOT / "boards/lckfb/bk7258/board_audio.c").read_text()
board_audio_adapter = (
    REPO_ROOT / "platforms/common/include/xiaotai_board_audio.h"
).read_text()
network_source = (ROOT / "ap/src/xiaotai_network.c").read_text()
metrics_source = (ROOT / "ap/src/xiaotai_metrics.c").read_text()
ui_assets_source = (ROOT / "ap/assets/xiaotai_ui_assets.c").read_text()
ui_hit_test_source = (ROOT / "ap/src/xiaotai_ui_hit_test.c").read_text()
phone_asset_source = (ROOT / "ap/assets/xiaotai_phone_shortcut.c").read_text()
require("#define CONTROL_TASK_STACK_SIZE (12U * 1024U)" in app_source and
        "control_task, CONTROL_TASK_STACK_SIZE" in app_source,
        "control task must retain headroom for WebClient call chains")
require(tirtc_source.count(
            "xiaotai_audio_set_uplink_enabled(true); /* call-active */") == 3,
        "device-call and VoIP activation must explicitly open the uplink gate")
device_hangup_end = app_source.index(
    'BK_LOGI(TAG, "KEY requested device call hangup')
device_hangup_start = app_source.rindex("} else if (active) {", 0,
                                        device_hangup_end)
device_hangup_branch = app_source[device_hangup_start:device_hangup_end]
device_disconnect = device_hangup_branch.find("xiaotai_tirtc_disconnect()")
device_hangup_request = device_hangup_branch.find(
    "xiaotai_platform_service_request(")
require(device_disconnect >= 0 and device_hangup_request >= 0 and
        device_disconnect < device_hangup_request,
        "local device-call transport teardown must be queued before the HTTP hangup can trigger a competing remote close")
require("xiaotai_runtime_begin_ending" in app_source and
        "SESSION_END_TIMEOUT_MS" in app_source and
        "state == XIAOTAI_STATE_CALL_ENDING" in app_source and
        '"call ending timeout forced local completion' in app_source,
        "local call hangup must have a bounded fallback when disconnect callback is missing")
disconnect_start = tirtc_source.index(
    "if (event.type == MEDIA_EVENT_DISCONNECT)")
disconnect_end = tirtc_source.index(
    "if (event.type == MEDIA_EVENT_CONNECTED)", disconnect_start)
disconnect_branch = tirtc_source[disconnect_start:disconnect_end]
require("xiaotai_audio_stop()" in disconnect_branch and
        "(void)TiRtcDisconnect" in disconnect_branch and
        disconnect_branch.find("xiaotai_audio_stop()") <
        disconnect_branch.find("(void)TiRtcDisconnect"),
        "active capture must be joined before TiRTC destroys its connection")
send_audio_start = tirtc_source.index("static int send_encoded_audio")
send_audio_end = tirtc_source.index("static int send_h264", send_audio_start)
require("s_active_disconnect_queued" in
        tirtc_source[send_audio_start:send_audio_end],
        "uplink must reject new SDK sends as soon as disconnect is queued")
require("xiaotai_audio_agc_process" in audio_source and
        audio_source.find("xiaotai_audio_agc_process") <
        audio_source.find("linear2alaw", audio_source.find("static void capture_task")) and
        "bk_aud_agc_process" in audio_agc_source and
        "bk_aud_adc_agc_config" not in audio_source + audio_agc_source and
        "src/xiaotai_audio_agc.c" in ap_cmake,
        "8 kHz uplink must use BK software AGC before A-law without hardware AGC")
require("xiaotai_audio_adc_gain_for_sensitivity" in audio_policy_source and
        "0x2dU" in audio_policy_source and "0x35U" in audio_policy_source and
        "set_capture_gain" in board_audio_adapter and
        "record_config.adc_gain = (int)s_adc_gain" in board_audio_source and
        "audio_play_set_adc_gain" in board_audio_source and
        "xiaotai_audio_set_microphone_sensitivity" in app_source and
        "xiaotai_audio_commit_sensitivity" in app_source and
        'xiaotai_ui_show_status("MIC ERROR")' in app_source and
        "microphone_sensitivity" in storage_source and
        "xiaotai_product_settings_v1_t" in storage_source,
        "microphone sensitivity must persist, migrate v1 settings, and control the BK7258 ADC front end")
require('"device-call uplink active role=callee stream=%u subscription-gate=0' in
        tirtc_source and
        '"device-call uplink active role=caller stream=%u subscription-gate=0' in
        tirtc_source and
        tirtc_source.count("STREAM_UP_AUDIO_ID);") >= 2,
        "device-call caller/callee activation needs evidence for stream 10 without a subscription gate")
require("XIAOTAI_UI_ACTION_ROOM_TALK_START" in app_source and
        "XIAOTAI_ROOM_ACTION_TALK_STOP" in app_source and
        ".input_event = (uint8_t)touch" in app_source,
        "compact touch room UI must preserve DOWN/UP for hold-to-talk")
touch_start = app_source.index("static void handle_touch(")
touch_end = app_source.index("\nstatic ", touch_start + 1)
touch_handler = app_source[touch_start:touch_end]
require(touch_handler.find("s_room_touch_talking") <
        touch_handler.find("if (incoming_waiting)"),
        "room touch release must stop PTT before an incoming-call foreground consumes the gesture")
require("s_room_key_talking" in app_source and
        "s_room_suppress_short" not in app_source and
        "A room short press exits local media" in app_source,
        "room key must start PTT on long hold, stop on release, and exit on short press")
require("xiaotai_call_decode_info" in call_protocol_source and
        "xiaotai_call_decode_outbound" in call_protocol_source and
        "xiaotai_call_encode_voip_reject" in call_protocol_source and
        "xiaotai_call_encode_device_info" in app_source and
        "xiaotai_call_encode_device_end" in app_source,
        "call wire protocol must remain behind the bounded codec module")
require("service_request_work_t *work = psram_malloc(sizeof(*work))" in
        platform_source and
        "authorization = psram_malloc(MQTT_TOKEN_MAX + 16U)" in platform_source and
        "char authorization[MQTT_TOKEN_MAX + 16U]" not in platform_source,
        "service credentials and URL buffers must use PSRAM, not internal heap or the control stack")
require("static beken_mutex_t s_http_mutex" in platform_source and
        "http_json_request_unlocked" in platform_source and
        "reusing prepared platform session for MQTT" in platform_source,
        "application HTTP must be serialized and reuse the prepared first session")
require("room->port.transport_busy(room->port.context)" in room_source and
        ".transport_busy = room_transport_busy" in app_source,
        "background room reconciliation must not overlap unrelated RTC sessions")
platform_prepare_at = app_source.find(
    "xiaotai_platform_report_capabilities(&credentials)"
)
require(platform_prepare_at >= 0 and
        app_source.find("rc = xiaotai_tirtc_start(&s_tirtc_config);",
                        platform_prepare_at) > platform_prepare_at,
        "clock sync and capability reporting must precede TiRTC startup")
require("#define TIRTC_LOG_LEVEL_MAX 17" in tirtc_source and
        "#define XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT (-1)" in
        (ROOT / "ap/include/xiaotai_tirtc.h").read_text() and
        "static void tirtc_sdk_log_callback" in tirtc_source and
        "BK_LOG_RAW(\"%s\", chunk);" in tirtc_source and
        "TiRtcLogSetCallback(tirtc_sdk_log_callback);" in tirtc_source and
        "static int s_log_level = XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT;" in tirtc_source and
        "xiaotai_tirtc_set_log_level(s_log_level)" not in tirtc_source and
        "log level=SDK default" in tirtc_source and
        "level > TIRTC_LOG_LEVEL_MAX" in tirtc_source and
        "TiRtcLogSetLevel(level);" in tirtc_source and
        "TiRtcLogSetLevel(17);" not in tirtc_source and
        "TiRtcLogSetLevel(11);" not in tirtc_source and
        "TiRtcLogSetLevel(5);" not in tirtc_source and
        "tirtc 0..17" in
        (ROOT / "ap/src/xiaotai_console.c").read_text(),
        "TiRTC must stream SDK diagnostics to UART, preserve the SDK default level at boot, and expose runtime override")
require("HTTP_JSON_POST_EMPTY" in platform_source and
        "webclient_post(session, url, NULL, 0)" in platform_source and
        "webclient_handle_response(session)" in platform_source and
        'http_json_request_ex(url, HTTP_JSON_POST_EMPTY, NULL, NULL' in
        platform_source,
        "token exchange must preserve the product's empty-body POST contract")
require('http_json_request_ex(url, "{}"' not in platform_source,
        "token exchange must not replace the empty request body with JSON")
require("device token rejected http=%d code=%d message-bytes=%u" in
        platform_source,
        "token rejection diagnostics must retain HTTP and platform codes")
require("int _gettimeofday(" in ap_sources and
        "bk_rtc_gettimeofday(tv, timezone)" in ap_sources and
        "xiaotai_time_now()" in platform_source,
        "newlib time() must be bridged to the synchronized AON RTC")
require("static bool s_clock_synchronized" in platform_source and
        "if (s_clock_synchronized) return BK_OK;" in platform_source and
        "if (xiaotai_time_now() > 1700000000) return BK_OK;" not in
        platform_source and
        "device token request timestamp=%s mac=%s" in platform_source,
        "signed requests must follow a network clock sync in the current boot")
clocked_discovery = platform_source.split(
    "static int discover_services_with_network_time(", 1
)[1].split("static int hmac_signature(", 1)[0]
require(clocked_discovery.index("synchronize_clock()") <
        clocked_discovery.index("discover_services(services)"),
        "platform preparation must synchronize time before service discovery")
for start, end in (
    ("int xiaotai_platform_report_capabilities(", "static int mqtt_wait_connack("),
    ("int xiaotai_platform_run(", "int xiaotai_platform_bind("),
    ("int xiaotai_platform_bind(", "bool xiaotai_platform_binding_active("),
):
    section = platform_source.split(start, 1)[1].split(end, 1)[0]
    require("discover_services_with_network_time(&services)" in section and
            "discover_services(&services)" not in section,
            f"{start} must use clocked service discovery")
require('"%02X:%02X:%02X:%02X:%02X:%02X"' in platform_source and
        "char mac_text[18]" in platform_source,
        "binding and token exchange must preserve the established MAC text")
require("int xiaotai_platform_client_id(" in platform_source and
        ".client_id = s_tirtc_client_id" in app_source and
        ".client_id = credentials.client_id" not in app_source,
        "TiRTC CLIENT_ID must follow the C reference and use the hardware MAC")
require("TiRtcSetOption(option, value, (uint32_t)strlen(value))" in
        tirtc_source and
        "strlen(value) + 1U" not in tirtc_source and
        "TiRTC start args device_id=%s" in ap_sources,
        "TiRTC string option lengths must match the successful C reference")
require("xiaotai_log_set_level" in ap_sources and
        "xiaotai_tirtc_set_log_level" in ap_sources and
        'strcmp(argv[1], "tirtc")' in ap_sources and
        'strcmp(argv[1], "app")' in ap_sources,
        "serial logging control must keep TiRTC and XiaoTai levels separate")
require("BK_LOGD(TAG," in platform_source and
        "HTTP request method=%s url=%s authenticated=%d body-bytes=%u" in
        platform_source and
        "HTTP response status=%d bytes=%u" in platform_source and
        "MQTT CONNECT client_id=%s username=%s authenticated=%d" in
        platform_source and
        "MQTT PUBLISH topic=%s packet_id=%u qos=%u payload-bytes=%u" in
        platform_source and
        "MQTT RECEIVE topic=%.*s packet_id=%u qos=%u payload-bytes=%u" in
        platform_source and
        "BK_LOGD(TAG,\n            \"TiRtcWhipConnect args mode=ai" in
        ap_sources and
        "callback=on_ai_connect generation=%u" in ap_sources and
        "TiRtcWhipConnect returned rc=%d generation=%u" in ap_sources and
        "TiRtcWhipConnect callback mode=ai error=%d connection=%p generation=%u" in
        ap_sources and
        "TiRtcWhipConnect callback mode=voip error=%d connection=%p generation=%u" in
        ap_sources and
        "TiRtcWhipConnect callback mode=room error=%d connection=%p generation=%u" in
        ap_sources and
        "TiRtcConnect callback error=%d connection=%p generation=%u" in ap_sources,
        "DEBUG diagnostics must expose metadata without credentials")
require(not re.search(
            r"BK_LOG[DIWE]\([^;]*(?:authorization=%s|password=%s|token=%s|"
            r"payload=%|body=%|service_desc=%s)",
            ap_sources,
        ),
        "serial logs must not expose network credentials or payloads")
require('\\\"aspect_ratio\\\":\\\"4:3\\\"' in platform_source and
        '\\\"object_fit\\\":\\\"contain\\\"' in platform_source and
        '\\\"width\\\":1280' not in platform_source and
        '\\\"height\\\":720' not in platform_source,
        "stream profile must use fields accepted by the unified profile API")
require("device profile rejected http=%d code=%d message-bytes=%u" in
        platform_source,
        "profile rejection diagnostics must retain HTTP and platform codes")
require('XIAOTAI_PLATFORM_DISCOVERY_URL "http://ep-open.tangeopen.com/services"' in
        (ROOT / "ap/include/xiaotai_platform_client.h").read_text() and
        'copy_json_string(root, "tirtc-srv"' in platform_source and
        'has_prefix(services->device, "http://")' in platform_source and
        'has_prefix(services->tirtc, "http://")' in platform_source and
        'snprintf(output, capacity, "%s", s_service_cache.tirtc)' in platform_source and
        'has_prefix(services->mqtt, "mqtt://")' in platform_source and
        'service discovery rejected unsupported endpoint' in
        platform_source and
        '.service_endpoint = s_tirtc_endpoint' in app_source,
        "open-platform discovery must use the requested plain transports")
video_source = (ROOT / "ap/src/xiaotai_video.c").read_text()
main_source = (ROOT / "ap/ap_main.c").read_text()
ui_source = (ROOT / "ap/src/xiaotai_ui.c").read_text()
board_root = REPO_ROOT / "boards/lckfb/bk7258"
board_camera_source = (board_root / "board_camera.c").read_text()
board_display_source = (board_root / "board_display.c").read_text()
board_display_header = (board_root / "board_display.h").read_text()
board_touch_source = (board_root / "board_touch.c").read_text()
require(
    board_display_source.find("{0x21, {0x00}, 0}")
    < board_display_source.find("{0x29, {0x00}, 0}"),
    "LCKFB ST7789V2 profile must enable INVON before DISPON",
)
require("xiaotai_video_init()" in main_source and
        "xiaotai_board_camera_reserve(&s_camera_callbacks)" in video_source and
        "DVP H264 controller reserved bytes=%u" in board_camera_source and
        "bk_camera_delete(s_camera)" not in board_camera_source,
        "native H264 line storage must be reserved before network startup")
require("#define VIDEO_SENSOR_FPS 20U" in video_source and
        "#define VIDEO_TARGET_FPS 20U" in video_source and
        "config.config.fps = FPS20" in board_camera_source and
        "bk_h264_updata_encode_fps(VIDEO_TARGET_FPS)" in video_source and
        "xiaotai_video_pacer_should_send" not in video_source and
        "frame_buffer_encode_free(frame);" in video_source and
        "H264 uplink actual=%u.%02u fps target=%u bitrate=%u kbps" in video_source,
        "GC0308 H264 timing metadata must match its 20 fps capture and encoded P frames must never be discarded")
require("CONFIG_H264_QUALITY_LEVEL=2" in config and
        "CONFIG_H264_P_FRAME_CNT=29" in config,
        "VGA uplink must preserve the hardware-verified middle quality and 30-frame GOP")
require("atomic_load_explicit(&s_uplink_enabled" in audio_source and
        "mode == CONNECTION_STREAM" in tirtc_source and
        "xiaotai_audio_set_uplink_enabled(false)" in tirtc_source,
        "STREAM capture must not call TiRTC before audio uplink subscription")
require('xiaotai_ui_show_status("REMOTE")' in ap_sources and
        'xiaotai_ui_show_status("DEVICE CALL")' in ap_sources and
        'xiaotai_ui_show_status("VOIP CALL")' in ap_sources and
        '"短按按键开始对话"' in ui_source and
        '"远程查看中"' in ui_source,
        "320x240 UI must represent implemented product session states")
network_state_source = (
    ROOT / "ap/src/xiaotai_network_state.c"
).read_text()
require("xiaotai_network_link_changed(s_ready, true)" in network_source and
        "xiaotai_network_link_changed(s_ready, false)" in network_source and
        "if (changed)" in network_source and
        "return was_ready != now_ready;" in network_state_source,
        "duplicate Wi-Fi link notifications must not overwrite the online UI")
require("xiaotai_runtime_home_allowed(&s_runtime)" in app_source and
        "xiaotai_ui_incoming_action(event->x, event->y)" in app_source and
        "request_accept_pending();" in app_source and
        "request_reject_pending();" in app_source and
        'mixed_text(frame, 64, 198, "拒绝"' in ui_source and
        'mixed_text(frame, 224, 198, "接听"' in ui_source,
        "pending calls must keep the foreground UI and expose touch actions")
require("xiaotai_ui_active_call_action(event->x, event->y)" in app_source and
        "XIAOTAI_UI_ACTION_CALL_MIC_TOGGLE" in app_source and
        "XIAOTAI_UI_ACTION_CALL_HANGUP" in app_source and
        "xiaotai_audio_set_microphone_muted(s_call_microphone_muted)" in
        app_source and
        "xiaotai_ui_show_call_active(false, s_call_microphone_muted)" in
        app_source and
        "xiaotai_ui_show_call_active(true, s_call_microphone_muted)" in
        app_source and
        'mixed_text(frame, 224, 198, "挂断"' in ui_source and
        's_call_microphone_muted ? "麦克风已关" : "麦克风已开"' in ui_source,
        "active touch calls must expose session mute and hangup controls")
require('strcmp(method->valuestring, "caption")' in ai_view_source and
        'strcmp(method->valuestring, "round_start")' in ai_view_source and
        'strcmp(method->valuestring, "round_end")' in ai_view_source and
        "caption_type" in ai_view_source and "utterance_id" in ai_view_source and
        "xiaotai_ui_show_ai" in app_source and
        '"正在聆听"' in ui_source and '"AI正在思考"' in ui_source and
        '"AI回复中"' in ui_source and "ai_face" in ui_source and
        'caption_from_ai ? "AI：" : "你："' in ui_source and
        "caption_text(frame, caption, caption_from_ai)" in ui_source and
        '"CAPTION RECEIVED"' not in ui_source,
        "AI captions, phases and emotion expressions must reach the 320x240 UI")
require("xiaotai_ai_emotion_count()" in app_source and
        "xiaotai_ai_emotion_at(index)" in app_source and
        "xiaotai_ai_emotion_at(index)" in ui_source and
        "xiaotai_ai_emotion_label_zh(emotion)" in ui_source and
        'emotion_is(emotion, "neutral")' in ui_source and
        'emotion_is(emotion, "silly")' in ui_source and
        'emotion_is(emotion, "sleepy")' in ui_source and
        'emotion_is(emotion, "thinking")' in ui_source and
        'emotion_is(emotion, "loving")' in ui_source,
        "the expression browser and renderer must share all 21 platform AI emotion tags")
require("SESSION_INCOMING_TIMEOUT_MS 45000U" in app_source and
        "SESSION_CONNECT_TIMEOUT_MS 15000U" in app_source and
        "xiaotai_runtime_expire" in app_source and
        'xiaotai_ui_show_status("TIMEOUT")' in app_source,
        "pending and asynchronous sessions require monotonic deadlines")
recovery_source = (ROOT / "ap/src/xiaotai_tirtc_recovery.c").read_text()
recovery_doc = (ROOT / "docs/TIRTC_RUNTIME_RECOVERY.md").read_text()
require("OUTGOING_CLEANUP_GRACE_MS 3000U" in tirtc_source and
        "OUTGOING_RETAINED_HEAP_LIMIT (12U * 1024U)" in tirtc_source and
        "s_outgoing_heap_baseline" in tirtc_source and
        "s_cleanup_probe_active" in tirtc_source and
        "release_outgoing_connect();" in tirtc_source and
        "RECOVERY_STAGE_TIMEOUT_MS 5000U" in recovery_source and
        "xiaotai_tirtc_finalize_stop" in app_source and
        "xiaotai_tirtc_start(&s_tirtc_config)" in app_source and
        "xiaotai_tirtc_recovery_resources_restored()" in app_source and
        'reboot_after_recovery_failure("heap-not-restored"' in app_source and
        "正常运行时只执行" in recovery_doc and
        "它不是普通会话生命周期" in recovery_doc,
        "TiRTC null-handle cleanup must prefer reuse and bound poisoned-runtime recovery")
contacts_source = (REPO_ROOT / "product/src/xiaotai_contacts.c").read_text()
ai_protocol_source = (REPO_ROOT / "product/src/xiaotai_ai_protocol.c").read_text()
call_state_header = (REPO_ROOT / "product/include/xiaotai_call_state.h").read_text()
call_state_source = (REPO_ROOT / "product/src/xiaotai_call_state.c").read_text()
platform_source = (ROOT / "ap/src/xiaotai_platform_client.c").read_text()
require("xiaotai_platform_service_request" in app_source and
        "xiaotai_tirtc_service_request(" not in app_source and
        app_source.count("xiaotai_tirtc_service_request_json(") == 1 and
        'xiaotai_tirtc_service_request_json("/v1/wxvoip/reject"' in app_source and
        'copy_json_string(root, "ai-srv"' in platform_source and
        'copy_json_string(root, "call-srv"' in platform_source and
        'copy_json_string(root, "voip-srv"' in platform_source,
        "product APIs must use discovered services except the TiRTC WeChat rejection contract")
require(app_source.count('"/v1/call/device/contacts"') == 1 and
        '"/v1/voip/device/contacts"' not in app_source and
        '"/v1/voip/device/callers"' not in app_source and
        'strcmp(method->valuestring, "call_intent")' in ai_protocol_source and
        'strcmp(method->valuestring, "device_action")' in ai_protocol_source and
        "xiaotai_ai_encode_call_action_result" in ai_protocol_source and
        "xiaotai_ai_encode_call_action_result" in app_source and
        "xiaotai_contacts_resolve_call_intent_json" in app_source and
        "xiaotai_contacts_first" in app_source and
        "xiaotai_contacts_replace_all_json" in contacts_source and
        "xiaotai_contacts_replace_all_json(&s_contacts" in app_source and
        "xiaotai_contacts_resolve_call_intent_json" in contacts_source and
        '"wx_model_id"' in contacts_source and
        '"wxa_user_openid"' in contacts_source and
        "XIAOTAI_CONTACT_MATCH_AMBIGUOUS" in contacts_source and
        "normalize_name" in contacts_source and
        "remove_channel" in contacts_source and
        "xiaotai_contacts_t channel" not in contacts_source,
        "AI contact calls require merged device/WeChat snapshots and unique match")
ai_call_handoff = app_source[
    app_source.index("static void handle_ai_call_intent"):
    app_source.index("static void handle_dial_contact")
]
require("response, (uint32_t)response_length) < 0)" in ai_call_handoff and
        "response, (uint32_t)response_length) != BK_OK" not in ai_call_handoff,
        "AI call handoff must accept TiRtcSendCommand positive byte counts")
require("xiaotai_contacts_grouped_indices" in contacts_source and
        "s_contact_display_indices" in app_source and
        'wechat[index] ? "微信" : "设备"' in ui_source,
        "contact pages must show all contacts grouped and labeled by device/WeChat source")
require('"/v1/call/request"' in app_source and
        '"/v1/voip/device/call"' in app_source and
        "xiaotai_call_encode_voip_dial" in app_source and
        '"wx_version_type\\\":0' in call_protocol_source and
        '"calling_timeout_sec\\\":30' in call_protocol_source and
        '"wx_caller_camera_status\\\":1' in call_protocol_source and
        '"wx_listener_camera_status\\\":1' in call_protocol_source and
        "xiaotai_tirtc_expect_outbound_call" in ap_sources and
        "MEDIA_EVENT_CALL_ACCEPT" in ap_sources and
        'signal->wx_call_id' in app_source and
        "SESSION_OUTGOING_TIMEOUT_MS 30000U" in app_source,
        "outbound device and WeChat calls must preserve confirmation semantics")
require("set_poll_timeout_option" in ap_sources and
        "if (applied_ms > 0)" in ap_sources,
        "TGTRP poll timeout's positive applied value must be accepted")
require(".max_connections = 1" in app_source and
        ".max_send_buffer = 128U * 1024U" in app_source and
        'query_string_value(s_ai_peer_id, "role_id"' in app_source and
        "AI credentials accepted peer-scheme=" in app_source and
        "AI WHIP connect failed rc=" in ap_sources and
        "AI WHIP memory before submit heap=" in ap_sources,
        "AI WHIP setup must match the one-session ESP reference contract and preserve DTLS headroom")
tirtc_source = (ROOT / "ap/src/xiaotai_tirtc.c").read_text()
audio_callback = re.search(
    r"static void on_audio\(tirtc_conn_t connection,.*?\n}\n\nstatic int on_subscribe_audio",
    tirtc_source,
    re.S,
)
require(audio_callback is not None and
        "frame->stream_id !=" not in audio_callback.group(0),
        "downlink playback must not reject the peer's independent stream id")
require("OPUS_CAPTURE_TASK_STACK (40U * 1024U)" in audio_source and
        "OPUS_PLAYBACK_TASK_STACK (40U * 1024U)" in audio_source and
        "OPUS_PACKET_SLOTS 32U" in audio_source and
        "capture_stack" in audio_source and "playback_stack" in audio_source,
        "Opus encode/decode tasks must retain their measured PSRAM stack headroom")
require("#include <modules/aec.h>" in audio_source and
        "aec_size(AEC_DELAY_SAMPLE_POINTS_MAX)" in audio_source and
        "s_aec = psram_malloc(s_aec_context_bytes)" in audio_source and
        "s_aec_reference_ring = psram_malloc(" in audio_source and
        "frame_samples != sample_rate_hz / 50U" in audio_source and
        "aec_reference_push(pcm, samples);" in audio_source and
        audio_source.find("aec_reference_push(pcm, samples);") >
        audio_source.find("s_board_audio->write_pcm(") and
        "aec_process_capture(pcm, s_frame_samples);" in
        audio_source and
        audio_source.find("aec_process_capture(pcm, s_frame_samples);") <
        audio_source.find("opus_encode(s_opus_encoder, pcm") and
        "aec_proc(s_aec, s_aec_ref, s_aec_mic, s_aec_out);" in audio_source and
        "AEC_DELAY_POINTS 211U" in audio_source and
        "aec_destroy();" in audio_source and
        "ref-under=%u ref-over=%u" in audio_source,
        "full-duplex audio must AEC 20 ms microphone blocks against the post-volume DAC reference")
require("xiaotai_audio_frame_samples_for_rate(required_rate)" in audio_source and
        "xiaotai_audio_ns_process(&s_uplink_ns, pcm" in audio_source and
        "xiaotai_audio_agc_process(&s_uplink_agc, pcm" in audio_source and
        audio_source.find("aec_process_capture(pcm, s_frame_samples)") <
        audio_source.find("xiaotai_audio_ns_process(&s_uplink_ns, pcm") <
        audio_source.find("xiaotai_audio_agc_process(&s_uplink_agc, pcm") and
        audio_source.find("xiaotai_audio_agc_process(&s_uplink_agc, pcm") <
        audio_source.find("opus_encode(s_opus_encoder, pcm") and
        audio_source.find("xiaotai_audio_agc_process(&s_uplink_agc, pcm") <
        audio_source.find("linear2alaw((int)pcm[i])") and
        "SDK AGC level in=%u out=%u limited=%u failures=%u" in audio_source,
        "BK SDK NS ready rate=16000 frame=320 suppression=-25dB" in audio_source and
        "AI Opus must use observable BK SDK NS after AEC and before AGC/encoding; both uplinks use AGC")
require("ECHO_DIAG AEC" in audio_source and
        "ECHO_DIAG RX" in tirtc_source and
        "ECHO_DIAG RX-SUM" in tirtc_source and
        "expected_downlink_audio_stream" in tirtc_source,
        "echo diagnosis must correlate routed downlink streams with AEC reference health")
require("#define STREAM_UP_AUDIO_ID 10U" in tirtc_source and
        "#define STREAM_DOWN_AUDIO_ID 14U" in tirtc_source and
        "#define VOIP_AUDIO_ID 0U" in tirtc_source and
        "static int subscribe_stream_talkback_audio(tirtc_conn_t connection)" in
        tirtc_source and
        "TiRtcSubscribeAudio(connection, STREAM_DOWN_AUDIO_ID)" in
        tirtc_source,
        "media modes must use the documented directional audio stream IDs")
require("bool subscription_required = mode == CONNECTION_STREAM" in
        tirtc_source and
        "mode == CONNECTION_AI" in tirtc_source and
        "mode == CONNECTION_AI;" in tirtc_source and
        "mode == CONNECTION_AI ||\n                                 mode == CONNECTION_ROOM" not in
        tirtc_source and
        "mode == CONNECTION_VOIP ? VOIP_AUDIO_ID" in tirtc_source and
        "TiRtcSubscribeAudio(event.connection, VOIP_AUDIO_ID)" in
        tirtc_source and
        "VoIP uplink enabled by call-active gate" in tirtc_source,
        "group-room PTT and WeChat VoIP must publish after activation without waiting for an optional subscription callback")
require("s_outgoing_connect_inflight" in tirtc_source and
        "s_outgoing_cleanup_connection" in tirtc_source and
        "OUTGOING_CONNECT_MIN_HEAP (64U * 1024U)" in tirtc_source and
        "queue_outgoing_cleanup(connection, requested, \"failed-ai\")" in
        tirtc_source and
        "outgoing cleanup complete handle=" in tirtc_source and
        "xiaotai_tirtc_busy()" in app_source and
        "if (!claim_outgoing_connect()) return TIRTC_E_BUSY;" in
        tirtc_source,
        "outgoing TiRTC failures must drain their handles before reentry")
require("s_active_disconnect_queued" in tirtc_source and
        "queue_active_disconnect_once" in tirtc_source and
        tirtc_source.count("queue_active_disconnect_once(connection") >= 4 and
        "atomic_store_explicit(&s_active_disconnect_queued, false" in
        tirtc_source,
        "active connections must queue at most one TiRtcDisconnect when remote hangup and SDK error race")
require("#define AI_AUDIO_ID 1U" in tirtc_source and
        "TiRtcSubscribeAudio(connection, AI_AUDIO_ID)" in tirtc_source and
        "normalize_tirtc_result" in tirtc_source and
        "return normalize_tirtc_result(rc);" in tirtc_source and
        "audio subscription stream=%u supported=%d active=%d" in
        tirtc_source and
        "CONTROL_AI_CONNECTED" in app_source and
        "xiaotai_tirtc_ai_prepare_media()" in app_source and
        "AI media prewarmed before start_session" in app_source and
        "rtos_delay_milliseconds(300)" in app_source and
        "TIRTC_AUDIO_OPUS" in tirtc_source and
        "TIRTC_AUDIOSAMPLE_16K16B1C" in tirtc_source and
        "XIAOTAI_AUDIO_CODEC_OPUS_16K" in tirtc_source,
        "AI must use stream 1, 20 ms packets, acknowledge early subscriptions and defer start_session")
require("static char s_ai_peer_id[512]" in app_source and
        "static char s_ai_token[1024]" in app_source and
        "memcpy(s_ai_peer_id, peer->valuestring, peer_length + 1U)" in
        app_source and
        "normalized whips->whip" not in app_source and
        "xiaotai_tirtc_ai_connect(s_ai_peer_id" in app_source and
        "memcpy(s_ai_token, token->valuestring, token_length + 1U)" in
        app_source,
        "asynchronous AI WHIP credentials must outlive the parsed JSON response")
ai_end_start = app_source.index(
    "if (message == XIAOTAI_AI_MESSAGE_END) {")
ai_end_end = app_source.index(
    "if (message != XIAOTAI_AI_MESSAGE_START_ACCEPTED)", ai_end_start)
ai_end_handler = app_source[ai_end_start:ai_end_end]
require("AI end_session command received generation=%u bytes=%u" in
        ai_end_handler and
        "CONTROL_AI_END" in ai_end_handler and
        "xiaotai_tirtc_disconnect" not in ai_end_handler and
        "xiaotai_ai_end_drain_begin" in app_source and
        "xiaotai_audio_playback_is_drained" in app_source and
        "AI_END_PLAYBACK_QUIET_MS 120U" in app_source and
        "AI_END_FINAL_AUDIO_ARRIVAL_MS 1500U" in app_source and
        "AI_END_DRAIN_TIMEOUT_MS 5000U" in app_source and
        "arrival-grace=%u" in app_source and
        "AI final playback %s; disconnecting" in app_source and
        "xiaotai_ai_remote_close_is_normal" in app_source and
        "AI active session completed by remote close" in app_source and
        "s_playback_tail_ms" in audio_source and
        "xiaotai_audio_playback_tail_after_write" in audio_source and
        "s_playback_in_progress" in audio_source,
        "AI end_session must gate uplink and drain software plus DMA-estimated playback before bounded disconnect")
ai_close_start = tirtc_source.index("if (event.type == MEDIA_EVENT_CLOSED) {")
ai_close_end = tirtc_source.index("BK_LOGI(TAG, \"connection closed mode=", ai_close_start)
ai_close_branch = tirtc_source[ai_close_start:ai_close_end]
require("event.mode == CONNECTION_AI" in ai_close_branch and
        "drain_ai_playback_after_transport_close" in ai_close_branch and
        ai_close_branch.find("drain_ai_playback_after_transport_close") <
        ai_close_branch.find("xiaotai_audio_stop"),
        "AI remote close must drain locally buffered playback before audio teardown")
require('"/v1/call/device/info"' in ap_sources and
        "xiaotai_tirtc_call_connect" in ap_sources and
        "TiRtcConnect(peer_id, token" in ap_sources and
        "TiRtcSubscribeAudio(event.connection, STREAM_UP_AUDIO_ID)" in ap_sources and
        "TiRtcSendCommand(event.connection, 0x2000U" in ap_sources,
        "device call answer flow must exchange token, connect, subscribe and confirm")
require('"/v1/wxvoip/reject"' in ap_sources and
        "xiaotai_tirtc_voip_connect" in ap_sources and
        "TiRtcWhipConnect(peer_id, token, on_voip_connect" in ap_sources and
        "command == 0x2000U" in ap_sources and
        "MEDIA_EVENT_VOIP_ACCEPT" in ap_sources and
        "xiaotai_tirtc_send_command(0x2001U" in ap_sources,
        "WeChat VoIP must reject, connect, await accept and hang up by protocol")
require("XIAOTAI_VOIP_DESCRIPTOR_SIZE 1024U" in call_state_header and
        "xiaotai_call_state_classify_voip" in app_source and
        "outbound_openid" in call_state_source and
        'signal->wx_from' in app_source and
        "outbound_mismatch ? 5 : 7" in app_source and
        '"home call dialing first WeChat contact name=%s' in app_source and
        "xiaotai_contacts_first_of_type" in app_source and
        "VOIP_AUDIO_ID 0U" in tirtc_source,
        "WeChat VoIP must retain long descriptors, correlate outbound callbacks and remain reachable through the home quick-call action")
quick_start = app_source.index("static void quick_call_first_contact(void)\n{")
quick_end = app_source.index("\nstatic void", quick_start + 1)
quick_call = app_source[quick_start:quick_end]
home_quick_start = app_source.index(
    "static void quick_call_first_wechat_contact(void)\n{")
home_quick_end = app_source.index("\nstatic void", home_quick_start + 1)
home_quick_call = app_source[home_quick_start:home_quick_end]
require("xiaotai_contacts_first" in quick_call and
        "xiaotai_contacts_first_of_type" in home_quick_call and
        "XIAOTAI_CONTACT_VOIP" in home_quick_call and
        "show_wechat_qr_waiting(true)" in home_quick_call and
        "dial_contact(&contact)" in quick_call and
        "dial_contact(&contact)" in home_quick_call and
        "XIAOTAI_INTENT_HOME_WECHAT_CALL" in app_source and
        'owner == XIAOTAI_OWNER_DEVICE_CALL ?\n        "/v1/call/request" : "/v1/voip/device/call"' in app_source and
        "response=%.160s" in app_source,
        "home quick call must select the first WeChat contact, show QR when none exists, and expose bounded service errors")
require("char peer_id[sizeof(s_calls.voip.peer_id)]" not in app_source and
        "char token[sizeof(s_calls.voip.token)]" not in app_source and
        len(re.findall(
            r"xiaotai_tirtc_voip_connect\(\s*"
            r"s_calls\.voip\.peer_id,\s*s_calls\.voip\.token,",
            app_source,
        )) >= 2,
        "WeChat WHIP credentials must stay in persistent storage and must not consume the control-task stack")
require("TiRtc" not in cp_sources, "CP must not own TiRTC product lifecycle")
require(not re.search(r"\bTci(?:Init|Start|Send|Connect)", ap_sources),
        "legacy Tci product API leaked into the new port")
require('"%s/v1/device/tts?code=%s"' in platform_source and
        '"Bearer %s"' in platform_source and
        '"Accept: audio/pcm\\r\\n"' in platform_source and
        'strstr(content_type, "audio/pcm")' in platform_source and
        'webclient_header_fields_get(session, "Transfer-Encoding:"' in platform_source and
        'strcmp(transfer_encoding, "chunked")' in platform_source and
        "TTS_PCM_MAX_BYTES" in platform_source and
        "xiaotai_pcm_stream_collect" in platform_source and
        (ROOT / "tools/test_pcm_stream.sh").exists() and
        platform_source.find("xiaotai_ui_show_verification_code(") <
        platform_source.find("download_verification_tts(services, report") and
        "no local fallback" in platform_source and
        "for (unsigned repeat = 0; repeat < 3U" in audio_source and
        "psram_free(s_prompt_pcm)" in audio_source and
        "binding_digits" not in ap_sources and
        not (ROOT / "ap/assets/binding_digits.c").exists(),
        "binding voice must use authenticated server PCM three times without a local fallback")
require("xiaotai_ui_show_wechat_qr();" in app_source and
        "platform contact list is empty" in app_source and
        "UI_PAGE_WECHAT_QR" in app_source and
        "s_ui_page = UI_PAGE_WECHAT_QR" in app_source and
        "if (s_ui_page == UI_PAGE_WECHAT_QR" in app_source and
        "WeChat QR dismissed by KEY" in app_source and
        "XIAOTAI_WX_QR_MODULES 37U" in
        (ROOT / "ap/assets/xiaotai_ui_assets.h").read_text() and
        "xiaotai_wx_qr_bits" in ui_assets_source and
        'centered_cjk_text(frame, 191, "微信扫码添加联系人"' in ui_source,
        "missing fixed mini-program QR fallback for empty quick-call contacts")
require('mixed_text(frame, 12, 8, "小钛"' in ui_source and
        'page_frame("功能菜单")' in ui_source and
        '"AI聊天", "通讯录", "多人对讲", "表情包"' in ui_source and
        'page_frame("设备设置")' in ui_source and
        'page_frame("网络状态")' in ui_source and
        '"XIAOTAI"' not in ui_source and
        "basic_cjk_characters" in
        (ROOT / "tools/generate_ui_assets.py").read_text() and
        "while (low < high)" in ui_source,
        "product UI must be Chinese and support bounded live Chinese captions")
require('strcmp(status, "CONTACT OFFLINE") == 0' in ui_source and
        'return "呼叫对象不在线";' in ui_source and
        'return "请稍后再试";' in ui_source,
        "offline call rejection must be shown as a business result, not an error")
require("phone_shortcut(frame);" in ui_source and
        "XIAOTAI_UI_HOME_QUICK_CALL_ICON_X 276U" in
        (ROOT / "ap/include/xiaotai_ui_hit_test.h").read_text() and
        "XIAOTAI_UI_HOME_QUICK_CALL_ICON_Y 150U" in
        (ROOT / "ap/include/xiaotai_ui_hit_test.h").read_text() and
        "Source SHA-256: 6c0bc141933f0dadb442b905dcd30bf4acc174ac99fbbb53a94f1e44f34b8706" in
        phone_asset_source and
        "XIAOTAI_UI_ACTION_HOME_QUICK_CALL" in app_source and
        "handle_intent(XIAOTAI_INTENT_HOME_WECHAT_CALL);" in app_source and
        "XIAOTAI_UI_ACTION_VOLUME_DOWN" in app_source and
        "XIAOTAI_UI_ACTION_VOLUME_UP" in app_source and
        "XIAOTAI_UI_HOME_MENU_HIT_Y" in ui_hit_test_source and
        "XIAOTAI_UI_HOME_QUICK_CALL_HIT_Y" in ui_hit_test_source and
        "x >= 160U && x < 215U" in ui_hit_test_source and
        "x >= 265U" in ui_hit_test_source and
        (ROOT / "tools/test_ui_hit_test.sh").exists(),
        "touch UI must expose distinct, tested home quick-call and volume controls")
ui_hit_header = (ROOT / "ap/include/xiaotai_ui_hit_test.h").read_text()
require("XIAOTAI_UI_ROOM_LEAVE_X 8U" in ui_hit_header and
        "XIAOTAI_UI_ROOM_TALK_X 164U" in ui_hit_header and
        "XIAOTAI_UI_ROOM_BUTTON_WIDTH 148U" in ui_hit_header and
        "XIAOTAI_UI_ROOM_END" not in ui_hit_header and
        '"结束"' not in ui_source and
        "action == XIAOTAI_UI_ACTION_ROOM_LEAVE" in ui_hit_test_source and
        "pressed ? XIAOTAI_UI_ACTION_ROOM_LEAVE" in ui_hit_test_source and
        "XIAOTAI_UI_ACTION_ROOM_PAGE_PREV" in ui_hit_test_source and
        "XIAOTAI_UI_ACTION_ROOM_PAGE_NEXT" in ui_hit_test_source and
        "s_ui_page = UI_PAGE_HOME;" in app_source and
        '"room back to home rc=%d' in app_source,
        "touch room UI must use two rectangular controls, down-triggered leave, home back and pagination")
require("bk_wifi_scan_start(NULL)" in network_source and
        "bk_wifi_scan_get_result(&result)" in network_source and
        "bk_wifi_scan_free_result(&result)" in network_source and
        '"POST /api/scan "' in network_source and
        '"GET /api/networks "' in network_source and
        "SCAN_AP_MAX 20U" in network_source and
        "qsort(selected, count" in network_source and
        "xiaotai_ui_show_network_setup(s_ap_ssid)" in network_source,
        "SoftAP portal must expose a bounded, sorted device-side Wi-Fi scan")
require("小钛联网助手" in network_source and
        "选择家里的 Wi-Fi" in network_source and
        "无互联网" in network_source and
        "输入这个 Wi-Fi 的密码" in network_source and
        "连接此 Wi-Fi" in network_source and
        "showStep(2" in network_source and
        "position:fixed" in network_source and
        "document.createElement('button')" in network_source and
        '"GET /api/setup "' in network_source and
        "保存并重启" not in network_source and
        'centered_mixed_text(frame, 48, "请在手机上继续"' in ui_source and
        '"手机提示无互联网是正常的"' in ui_source,
        "SoftAP portal must use the approved novice two-step provisioning flow")
require("Location: http://192.168.6.1/" in network_source and
        "generate_204" in network_source and
        "hotspot-detect.html" in network_source and
        "connecttest.txt" in network_source,
        "SoftAP must intercept DNS and redirect common OS captive probes")
page_match = re.search(
    r'static const char s_setup_page\[\]\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);',
    network_source,
    re.DOTALL,
)
require(page_match is not None, "embedded SoftAP page is missing")
page_literals = re.findall(r'"(?:\\.|[^"\\])*"', page_match.group(1))
setup_page = "".join(ast.literal_eval(value) for value in page_literals)
require("<section id=choose>" in setup_page and
        "<section id=password class=hidden>" in setup_page and
        "max-width:480px" in setup_page and
        "/api/setup" in setup_page,
        "embedded SoftAP page does not preserve the approved A layout")
script_match = re.search(r"<script>(.*?)</script>", setup_page, re.DOTALL)
require(script_match is not None, "embedded SoftAP JavaScript is missing")
with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8") as script_file:
    script_file.write(script_match.group(1))
    script_file.flush()
    syntax = subprocess.run(
        ["node", "--check", script_file.name],
        capture_output=True,
        text=True,
        check=False,
    )
require(syntax.returncode == 0,
        "embedded SoftAP JavaScript is invalid: " + syntax.stderr.strip())
require('"METRICS {' in metrics_source and
        "rtos_get_free_heap_size()" in metrics_source and
        "rtos_get_psram_free_heap_size()" in metrics_source and
        "uxTaskGetSystemState" in metrics_source and
        "CONFIG_CPU_CNT" in metrics_source and
        "lwip_stats.link.recv" in metrics_source and
        '"rx_packets\\":%u' in metrics_source and
        '"largest\\":null' in metrics_source and
        "xiaotai_metrics_start()" in main_source,
        "BK7258 runtime resource telemetry contract is incomplete")
stability_source = (ROOT / "tools/web_stability.py").read_text()
require("sync_playwright" in stability_source and
        "rng.uniform(5, 30)" in stability_source and
        "rng.uniform(60, 300)" in stability_source and
        "rng.uniform(600, 1800)" in stability_source and
        "context.close()" in stability_source and
        "c.width >= 320 && c.height >= 240" in stability_source and
        "json.dumps(record" in stability_source and
        "quote(device_id" in stability_source and
        'device.add_argument("--device-name")' in stability_source and
        'parser.add_argument("--dwell-plan"' in stability_source and
        '"first_frame_ms_p95"' in stability_source and
        '"watch_samples"' in stability_source and
        '"started_at": utc_now()' in stability_source and
        '"failure_phase"' in stability_source and
        '"failure_state"' in stability_source,
        "browser stability harness must randomize dwell and normal/abrupt disconnects")
ap_stability_source = (ROOT / "tools/ap_provisioning_stability.py").read_text()
require("parse_dns_a" in ap_stability_source and
        '"android", "connectivitycheck.gstatic.com", "/generate_204"' in
        ap_stability_source and
        '"ios", "captive.apple.com", "/hotspot-detect.html"' in
        ap_stability_source and
        '"windows", "www.msftconnecttest.com", "/connecttest.txt"' in
        ap_stability_source and
        'parser.add_argument("--include-scan"' in ap_stability_source and
        'parser.add_argument("--metrics-log"' in ap_stability_source and
        '"success_percent"' in ap_stability_source and
        '"cycle_ms_p95"' in ap_stability_source,
        "AP provisioning stability harness must cover DNS, OS probes and metrics")
runtime_log_source = (ROOT / "tools/analyze_runtime_log.py").read_text()
require("FATAL_MARKERS" in runtime_log_source and
        '"Task exits abnormally!" in line' in runtime_log_source and
        '"task_return_warnings"' in runtime_log_source and
        '"ref_under_percent"' in runtime_log_source and
        '"ready_resource_delta"' in runtime_log_source,
        "runtime log gate must separate CPU faults from returned-task warnings")
subprocess.run([
    sys.executable, str(ROOT / "tools/test_analyze_runtime_log.py"), "-q"
], cwd=ROOT / "tools", check=True)
touch_source = (ROOT / "ap/src/xiaotai_touch.c").read_text()
touch_policy_source = (ROOT / "ap/src/xiaotai_touch_policy.c").read_text()
require("CONFIG_SIM_I2C=y" in config and
        "CONFIG_SIM_I2C_HW_BOARD_V3=y" in config and
        "CONFIG_SIM_I2C1_SCL_GPIO=42" in config and
        "CONFIG_SIM_I2C1_SDA_GPIO=43" in config and
        "CONFIG_TP_FT6336=y" in config and
        "CONFIG_TP_RST_GPIO_ID=46" in config and
        "CONFIG_TP_INT_GPIO_ID=45" in config and
        "drv_tp_open" in board_touch_source and
        "bk_tp_get_device" in board_touch_source and
        "xiaotai_board_touch_open" in touch_source and
        "xiaotai_touch_native_to_landscape" in touch_policy_source and
        "xiaotai_touch_start" in app_source,
        "camera SCCB and FT6336 must share the mutex-protected GPIO42/43 bus")
require('"/v1/call/group/device/assignment"' in room_source and
        "ASSIGNMENT_SYNC_MS" not in room_source and
        "ASSIGNMENT_RETRY_MS" not in room_source and
        "assignment_refresh_requested" in room_source and
        "XIAOTAI_ROOM_ACTION_SYNC" in app_source and
        'strcmp(name, "room_assignment_changed") == 0' in signal_source and
        "XIAOTAI_SIGNAL_ROOM_ASSIGNMENT_CHANGED" in app_source and
        'strcmp(path, "/v1/call/group/device/assignment") != 0' in ap_sources and
        '"/v1/call/group/device/connect-token"' in room_source and
        '"/v1/call/group/device/presence"' in room_source and
        '"/v1/call/group/device/join"' in room_source and
        '"/v1/call/group/device/leave"' in room_source and
        "join_room" in room_source and
        "set_mic_state" in room_source and
        "leave_room" in room_source and
        "audio_profile_valid" in room_source and
        ".transport_start_media = room_transport_start_media" in app_source and
        "participant_mic_state_changed" in room_source and
        "xiaotai_tirtc_room_connect" in ap_sources and
        "ROOM_AUDIO_ID 1U" in ap_sources and
        "CONTROL_ROOM_DISCONNECTED" in app_source and
        "xiaotai_room_handle_response" in app_source and
        "xiaotai_room_handle_command" in app_source and
        "xiaotai_room_tick" in app_source,
        "group-room PTT requires assignment, token, presence and TiRTC media lifecycle")
joined_ptt = re.search(
    r"if \(s_ui_page == UI_PAGE_ROOM && xiaotai_room_joined\(&s_room\)\) \{"
    r"(?P<body>.*?)"
    r"\n    \}", app_source, re.DOTALL)
press_branch = None if joined_ptt is None else re.search(
    r"if \(intent == XIAOTAI_INTENT_PTT_PRESS\) \{(?P<body>.*?)"
    r"\n        \}", joined_ptt.group("body"), re.DOTALL)
require(press_branch is not None and
        "XIAOTAI_ROOM_ACTION_TALK_START" in press_branch.group("body"),
        "joined room PTT must enable uplink on button press, not after the long-press delay")
console_source = (ROOT / "ap/src/xiaotai_console.c").read_text()
for command in (
    '"status"', '"ai-start"', '"ai-stop"', '"wifi-set"',
    '"wifi-clear"', '"wificlear"', '"wifi-change"', '"tirtc-clear"',
    '"wechat-call"', '"device-call"', '"call-answer"',
    '"wechat-answer"', '"device-answer"', '"call-hangup"',
    '"call-status"', '"room-join"', '"room-leave"',
    '"room-talk-start"', '"room-talk-stop"', '"room-sync"',
    '"room-status"', '"restart"',
):
    require(command in console_source, f"missing serial command {command}")
require("xiaotai_app_request_room_join" in app_source and
        "xiaotai_app_request_room_leave" in app_source and
        "xiaotai_app_request_room_talk" in app_source and
        "xiaotai_app_room_snapshot" in app_source,
        "serial room controls must use the product control task and safe snapshot")
require("CONSOLE_CALL_HANGUP && is_call && channel_ok" in app_source and
        "state == XIAOTAI_STATE_CALL_INCOMING" in app_source and
        "build_voip_reject_request" in app_source and
        "reject_voip_request(voip_request)" in app_source,
        "call-hangup must reject pending device and WeChat calls")
for contract in (
    "xiaotai_storage_save_wifi",
    "xiaotai_storage_save_device",
    "xiaotai_storage_clear_device",
    "XiaoTai-%02X%02X",
    "POST /api/wifi",
    "WIFI_RETRIES",
    "auth_grant",
    "device/%s/cmd",
    "device/%s/ack",
    "XIAOTAI_BIND_TIMEOUT_SECONDS",
    "/v1/device/token",
    "/v1/device/profile",
    '"X-Device-Id"',
    '"X-Timestamp"',
    '"X-Nonce"',
    '"X-Mac"',
    '"X-Signature"',
    '\\\"stream\\\"',
    '\\\"call\\\"',
    '\\\"voip\\\"',
    '\\\"no_video\\\":true',
):
    require(contract in ap_sources, f"missing provisioning contract: {contract}")

profile_match = re.search(
    r"static const char s_device_profile\[\]\s*=\s*(.*?);",
    platform_source,
    re.S,
)
require(profile_match is not None, "unified device profile JSON is missing")
profile_text = "".join(
    ast.literal_eval(token)
    for token in re.findall(r'"(?:\\.|[^"\\])*"', profile_match.group(1))
)
profile = json.loads(profile_text)
board_manifest = json.loads(
    (REPO_ROOT / "boards/lckfb/bk7258/board.json").read_text()
)
version_match = re.search(r"`([^`]+)`", (ROOT / "VERSION.md").read_text())
require(version_match is not None, "VERSION.md must contain the firmware version")
require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+\+build\.[1-9][0-9]*",
                     version_match.group(1)) is not None,
        "BK7258 firmware version must use major.minor.patch+build.n")
require(profile["hardware"] == {
            "chip_model": "BK7258",
            "board_model": board_manifest["id"],
        },
        "device profile must report the BK7258 board ID")
require(profile["firmware_version"] == version_match.group(1),
        "device profile firmware version must match VERSION.md")
require(set(profile["profiles"]) == {"stream", "call", "voip"},
        "capability report must contain stream/call/voip")
stream_fields = {
    "up_audio_streamid", "up_video_streamid", "down_audio_streamid",
    "down_video_streamid",
    "up_audio_mt", "up_video_mt", "down_audio_mt", "down_video_mt",
    "audio_rate", "audio_channels", "camera_rotation", "hor_mirror",
    "vert_mirror", "no_video", "aspect_ratio", "object_fit",
}
require(set(profile["profiles"]["stream"]) <= stream_fields,
        "STREAM profile contains unsupported fields")
require(profile["profiles"]["stream"]["no_video"] is False,
        "STREAM must advertise the implemented native H264 path")
require(profile["profiles"]["stream"]["up_video_mt"] == ["h264"],
        "STREAM must advertise H264 only")
require(profile["profiles"]["stream"]["aspect_ratio"] == "4:3" and
        profile["profiles"]["stream"]["object_fit"] == "contain",
        "STREAM must describe the diagnostic VGA camera geometry with accepted fields")
for name in ("call", "voip"):
    require(profile["profiles"][name]["no_video"] is True,
            f"{name} must stay audio-only before call video integration")
require((profile["profiles"]["stream"]["up_audio_streamid"],
         profile["profiles"]["stream"]["down_audio_streamid"]) == (10, 14) and
        (profile["profiles"]["stream"]["up_video_streamid"],
         profile["profiles"]["stream"]["down_video_streamid"]) == (11, 15),
        "STREAM profile must explicitly route H5 media to the firmware stream IDs")
require(profile["profiles"]["voip"]["down_audio_mt"] == "alaw",
        "VoIP downlink audio contract changed")
require("device profile rejected message=%s" in platform_source,
        "DEBUG diagnostics must expose the platform profile validation message")
for online_contract in (
    "xiaotai_platform_run",
    "device/sn_%s/cmd",
    "device/sn_%s/notify",
    "device/sn_%s/ack",
    "device/sn_%s/up",
    '\\\"type\\\":\\\"heartbeat\\\"',
    "mqtt_handle_online_publish",
):
    require(online_contract in ap_sources,
            f"missing permanent platform channel contract: {online_contract}")

for leaked_log_format in (
    "authorization=%s",
    "password=%s",
    "token=%s",
):
    require(leaked_log_format not in ap_sources,
            f"serial logs must not expose credentials: {leaked_log_format}")
require(app_source.count(
            'error == 0 ||\n                            '
            'error == TIRTC_E_CONN_REMOTECLOSE') >= 2 and
        'error == 0 || error == TIRTC_E_CONN_REMOTECLOSE' in app_source,
        "normal remote close must return device-call, WeChat VoIP and STREAM sessions to READY")
require("static bool send_all" in network_source and
        "SO_RCVTIMEO" in network_source and
        "SO_SNDTIMEO" in network_source,
        "the provisioning portal must handle short writes and half-open clients")

for media_contract in (
    "xiaotai_audio_start",
    "xiaotai_audio_play_alaw",
    "xiaotai_audio_play_opus",
    "linear2alaw",
    "alaw2linear",
    "opus_encoder_create",
    "opus_decoder_create",
    "TiRtcSendAudioStream",
    "TiRtcSubscribeAudio",
    "TIRTC_AUDIO_ALAW",
    "TIRTC_AUDIOSAMPLE_8K16B1C",
    "STREAM_UP_AUDIO_ID 10U",
    "STREAM_DOWN_AUDIO_ID 14U",
    "on_conn_accepted",
    "MEDIA_EVENT_CONNECTED",
    "STREAM video unavailable rc=%d; audio remains active",
    "xiaotai_tirtc_log_stream_media_state",
    "xiaotai_tirtc_recovery_required",
    "recycling poisoned TiRTC runtime after failed connection cleanup",
    "xiaotai_video_start",
    "TiRtcSendVideoStream",
    "on_subscribe_video",
    "STREAM_UP_VIDEO_ID 11U",
    "VIDEO_WIDTH 640U",
    "VIDEO_HEIGHT 480U",
):
    require(media_contract in ap_sources,
            f"missing STREAM audio contract: {media_contract}")

board_config_source = (
    REPO_ROOT / "boards/lckfb/bk7258/board_config.h"
).read_text()
board_audio_source = (
    REPO_ROOT / "boards/lckfb/bk7258/board_audio.c"
).read_text()
audio_pipeline_source = (ROOT / "ap/src/xiaotai_audio.c").read_text()
for board_operation in (
    "xiaotai_board_amplifier_idle(void)",
    "xiaotai_board_amplifier_set_playing(bool playing)",
    "amplifier_gpio(AMPLIFIER_PLAY_GPIO, !playing)",
):
    require(board_operation in board_audio_source,
            f"missing BK7258 amplifier board operation: {board_operation}")
for board_operation in (
    "AUDIO_RECORD_ONBOARD_MIC",
    "AUDIO_PLAY_ONBOARD_SPEAKER",
    "xiaotai_board_audio_adapter(void)",
    ".request_stop = audio_adapter_request_stop",
    ".stop = audio_adapter_stop",
    "xiaotai_board_prompt_audio_start(void)",
):
    require(board_operation in board_audio_source,
            f"missing BK7258 audio adapter operation: {board_operation}")
require("xiaotai_board_audio_adapter()" in audio_pipeline_source and
        "s_board_audio->read_pcm(" in audio_pipeline_source and
        "s_board_audio->write_pcm(" in audio_pipeline_source and
        "s_board_audio->request_stop(" in audio_pipeline_source and
        "xiaotai_board_prompt_audio_write(" in audio_pipeline_source,
        "audio pipeline must consume the common board audio lifecycle")
for sdk_audio_operation in (
    "audio_record_create(",
    "audio_record_read_data(",
    "audio_play_create(",
    "audio_play_write_data(",
    "amplifier_gpio(",
):
    require(sdk_audio_operation not in audio_pipeline_source,
            f"audio pipeline must not own board operation: {sdk_audio_operation}")
common_audio_contract = (
    REPO_ROOT / "platforms/common/include/xiaotai_board_audio.h"
).read_text()
require("int (*request_stop)(void *context);" in common_audio_contract and
        "int (*stop)(void *context);" in common_audio_contract and
        '"${XIAOTAI_COMMON_ROOT}/include"' in
        (ROOT / "ap/CMakeLists.txt").read_text(),
        "BK7258 must consume the common two-phase audio lifecycle")
require('"${XIAOTAI_BOARD_ROOT}/board_audio.c"' in (ROOT / "ap/CMakeLists.txt").read_text(),
        "AP build must link the selected board amplifier implementation")
board_button_source = (
    REPO_ROOT / "boards/lckfb/bk7258/board_button.c"
).read_text()
button_pipeline_source = (ROOT / "ap/src/xiaotai_button.c").read_text()
require("gpio_dev_unmap(AI_BUTTON)" in board_button_source and
        "bk_gpio_get_input(AI_BUTTON)" in board_button_source,
        "selected board must own button GPIO")
require("xiaotai_board_button_init()" in button_pipeline_source and
        "xiaotai_board_button_released()" in button_pipeline_source and
        "bk_gpio_get_input(" not in button_pipeline_source,
        "button event logic must consume the board input")
require('"${XIAOTAI_BOARD_ROOT}/board_button.c"' in (ROOT / "ap/CMakeLists.txt").read_text(),
        "AP build must link the selected board button implementation")
require(all(operation in board_camera_source for operation in (
            "bk_camera_dvp_ctlr_new",
            "config.config.pwdn_pin = CAMERA_PWDN_GPIO",
            "config.config.reset_pin = CAMERA_RESET_GPIO",
            "config.config.i2c_config.scl_pin = CAMERA_SCCB_SCL_GPIO",
            "config.config.i2c_config.sda_pin = CAMERA_SCCB_SDA_GPIO",
            '"camera control PWDN GPIO%u=%d RESET GPIO%u=%d\\n"',
        )) and
        "xiaotai_board_camera_start()" in video_source and
        "bk_camera_open(" not in video_source,
        "selected board must own camera controller, pins, power, and open/close")
require("xiaotai_board_touch_open()" in
        (ROOT / "ap/src/xiaotai_touch.c").read_text() and
        "drv_tp_open(" in board_touch_source and
        "drv_tp_read(" in board_touch_source and
        "drv_tp_" not in (ROOT / "ap/src/xiaotai_touch.c").read_text(),
        "selected board must own touch-controller I/O")
require("xiaotai_board_display_init()" in ui_source and
        "xiaotai_board_display_flush(" in ui_source and
        "bk_display_" not in ui_source and
        "bk_display_spi_new(" in board_display_source and
        "LCD_BACKLIGHT" in board_display_source,
        "selected board must own display controller, panel profile, and backlight")
for source_name in ("board_camera.c", "board_touch.c", "board_display.c"):
    require(f'"${{XIAOTAI_BOARD_ROOT}}/{source_name}"' in
            (ROOT / "ap/CMakeLists.txt").read_text(),
            f"AP build must link the selected board source: {source_name}")
for board_fact in (
    "AI_BUTTON GPIO_7",
    "AMPLIFIER_POWER_GPIO GPIO_8",
    "AMPLIFIER_PLAY_GPIO GPIO_23",
    "CAMERA_PWDN_GPIO GPIO_40",
    "CAMERA_RESET_GPIO GPIO_41",
    "CAMERA_SCCB_SCL_GPIO GPIO_42",
    "CAMERA_SCCB_SDA_GPIO GPIO_43",
    "XIAOTAI_LCD_DC_GPIO GPIO_5",
    "XIAOTAI_LCD_RESET_GPIO GPIO_12",
    "TOUCH_NATIVE_WIDTH 240",
    "TOUCH_NATIVE_HEIGHT 320",
):
    require(board_fact in board_config_source,
            f"missing BK7258 board config: {board_fact}")
require("XIAOTAI_BOARD_DISPLAY_WIDTH 320U" in board_display_header and
        "XIAOTAI_BOARD_DISPLAY_HEIGHT 240U" in board_display_header,
        "display adapter must expose its logical canvas without leaking GPIO config")

video_start_handler = re.search(
    r"if \(event\.type == MEDIA_EVENT_VIDEO_START\)(.*?)"
    r"if \(event\.type == MEDIA_EVENT_VIDEO_STOP\)",
    tirtc_source,
    re.S,
)
require(video_start_handler is not None,
        "STREAM video start handler is missing")
gpio_profile = (ROOT / "ap/config/bk7258_ap/usr_gpio_cfg.h").read_text()
require("{GPIO_9,  GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_23, GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_40, GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_41, GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_42, GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_43, GPIO_SECOND_FUNC_DISABLE, GPIO_DEV_INVALID" in gpio_profile and
        "{GPIO_28, GPIO_SECOND_FUNC_ENABLE, GPIO_DEV_I2S1_MCLK" in gpio_profile,
        "P9/P23/P40-P43 must be board controls while P28 remains the audio MCLK")
require("TiRtcDisconnect" not in video_start_handler.group(1),
        "camera failure must preserve remote STREAM audio")

makefile = (ROOT / "Makefile").read_text()
require("project_main.mk" in makefile, "project does not use BK-AVDK build entry")

acceptance = (ROOT / "HARDWARE_ACCEPTANCE.md").read_text()
require("xiaotai_tirtc_transport_quiet_elapsed" in app_source and
        "TIRTC_TRANSPORT_QUIET_MS" in app_source and
        "service_after_room_pending();" in app_source,
        "room-to-call handoff must wait for the TiRTC socket cleanup window")
require("xiaotai_ui_show_home" in app_source and
        "bk_wifi_sta_get_link_status" in app_source and
        "gmtime_r" in app_source and "wifi_rssi" in ui_source and
        '"短按对话"' in ui_source,
        "idle home must show synchronized time and real Wi-Fi signal strength")
for marker in (
    "inbound STREAM accepted generation=",
    "audio subscription stream=10 supported=1 active=1",
    "video subscription stream=11 supported=1 active=1",
    "native H264 camera started 640x480 sensor=20 fps uplink=20 fps",
    "BK SDK AGC ready rate=8000 frame=160",
    "SDK AGC level in=",
    "first uplink frame sent bytes=160",
    "first downlink frame played bytes=320",
):
    require(marker in acceptance,
            f"hardware acceptance is missing log marker: {marker}")

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "tirtc_recovery_test"
    test_source = Path(tmp) / "tirtc_recovery_test.c"
    test_source.write_text(r'''
#include <assert.h>
#include "xiaotai_tirtc_recovery.h"
int main(void) {
    xiaotai_tirtc_recovery_t recovery;
    xiaotai_tirtc_recovery_init(&recovery);
    /* Exact first-failure values captured on BK7258: about 27 KiB retained. */
    assert(xiaotai_tirtc_recovery_heap_leaked(85488U, 57968U, 12U * 1024U));
    assert(!xiaotai_tirtc_recovery_heap_leaked(85488U, 80000U, 12U * 1024U));
    assert(!xiaotai_tirtc_recovery_heap_leaked(80000U, 85488U, 12U * 1024U));
    /* A closed callback precedes the SDK socket teardown.  A new connection
     * must not start inside the observed CP Wi-Fi cleanup race window. */
    assert(!xiaotai_tirtc_transport_quiet_elapsed(1000U, 1499U, 500U));
    assert(xiaotai_tirtc_transport_quiet_elapsed(1000U, 1500U, 500U));
    assert(!xiaotai_tirtc_transport_quiet_elapsed(0xffffff00U,
                                                   0x000000f3U, 500U));
    assert(xiaotai_tirtc_transport_quiet_elapsed(0xffffff00U,
                                                  0x000000f4U, 500U));
    assert(xiaotai_tirtc_recovery_step(&recovery, false,
        XIAOTAI_TIRTC_RECOVERY_SDK_READY, 100U) ==
        XIAOTAI_TIRTC_RECOVERY_NONE);
    xiaotai_tirtc_recovery_request(&recovery);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_READY, 100U) ==
        XIAOTAI_TIRTC_RECOVERY_STOP);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_STOPPING, 5099U) ==
        XIAOTAI_TIRTC_RECOVERY_NONE);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_STOPPED, 5100U) ==
        XIAOTAI_TIRTC_RECOVERY_RESTART);
    assert(xiaotai_tirtc_recovery_step(&recovery, false,
        XIAOTAI_TIRTC_RECOVERY_SDK_STARTING, 10099U) ==
        XIAOTAI_TIRTC_RECOVERY_NONE);
    assert(xiaotai_tirtc_recovery_step(&recovery, false,
        XIAOTAI_TIRTC_RECOVERY_SDK_READY, 10100U) ==
        XIAOTAI_TIRTC_RECOVERY_COMPLETE);

    xiaotai_tirtc_recovery_request(&recovery);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_READY, 0xfffffff0U) ==
        XIAOTAI_TIRTC_RECOVERY_STOP);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_STOPPING, 0x1377U) ==
        XIAOTAI_TIRTC_RECOVERY_NONE);
    assert(xiaotai_tirtc_recovery_step(&recovery, true,
        XIAOTAI_TIRTC_RECOVERY_SDK_STOPPING, 0x1378U) ==
        XIAOTAI_TIRTC_RECOVERY_REBOOT);
    return 0;
}
''')
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "ap/include"),
        str(ROOT / "ap/src/xiaotai_tirtc_recovery.c"),
        str(test_source), "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "runtime_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(REPO_ROOT / "product/include"),
        str(REPO_ROOT / "product/src/xiaotai_runtime.c"),
        str(REPO_ROOT / "product/tests/test_runtime.c"),
        "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "ai_view_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(REPO_ROOT / "product/include"),
        "-I", "/usr/include/cjson",
        str(REPO_ROOT / "product/src/xiaotai_ai_view.c"),
        str(REPO_ROOT / "product/tests/test_ai_view.c"),
        "-lcjson", "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "contacts_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(REPO_ROOT / "product/include"),
        "-I", "/usr/include/cjson",
        str(REPO_ROOT / "product/src/xiaotai_contacts.c"),
        str(REPO_ROOT / "product/tests/test_contacts.c"),
        "-lcjson", "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "video_pacer_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(REPO_ROOT / "product/include"),
        str(REPO_ROOT / "product/src/xiaotai_video_pacer.c"),
        str(REPO_ROOT / "product/tests/test_video_pacer.c"),
        "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    tmp_path = Path(tmp)
    for directory in ("common", "components", "driver", "os"):
        (tmp_path / directory).mkdir()
    (tmp_path / "common/bk_err.h").write_text(r'''
#define BK_OK 0
#define BK_FAIL (-1)
#define BK_ERR_PARAM (-2)
#define BK_ERR_NO_MEM (-3)
#define BK_ERR_BUSY (-4)
''')
    (tmp_path / "components/log.h").write_text(r'''
#define BK_LOGI(...)
#define BK_LOGW(...)
#define BK_LOGE(...)
''')
    (tmp_path / "driver/trng.h").write_text(r'''
#include <stddef.h>
int bk_fill_rand(void *buffer, size_t size);
''')
    (tmp_path / "os/os.h").write_text(r'''
#include <stdint.h>
uint32_t rtos_get_time(void);
''')
    (tmp_path / "xiaotai_audio.h").write_text(r'''
#include <stdbool.h>
void xiaotai_audio_set_uplink_enabled(bool enabled);
''')
    (tmp_path / "xiaotai_network.h").write_text(r'''
#include <stdbool.h>
bool xiaotai_network_ready(void);
''')
    (tmp_path / "xiaotai_platform_client.h").write_text(r'''
typedef void (*xiaotai_platform_response_fn)(const char *, void *);
int xiaotai_platform_service_request(const char *, const char *,
                                     xiaotai_platform_response_fn, void *);
''')
    (tmp_path / "xiaotai_tirtc.h").write_text(r'''
#include <stdbool.h>
#include <stdint.h>
bool xiaotai_tirtc_ready(void);
bool xiaotai_tirtc_busy(void);
bool xiaotai_tirtc_connected(void);
int xiaotai_tirtc_room_connect(const char *, const char *, uint32_t);
int xiaotai_tirtc_room_start_media(void);
int xiaotai_tirtc_send_command(uint32_t, const void *, uint32_t);
int xiaotai_tirtc_disconnect(void);
''')
    output = tmp_path / "room_test"
    test_source = tmp_path / "room_test.c"
    test_source.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "xiaotai_room.h"

static xiaotai_room_t *g_room;
static uint32_t g_now;
static bool g_connected;
static bool g_uplink = true;
static bool g_acquired;
static bool g_activated;
static bool g_released;
static bool g_closed;
static unsigned g_changed;
static unsigned g_assignment_requests;

static uint32_t now_ms(void *context) { (void)context; return g_now; }
static bool random_bytes(void *buffer, size_t size, void *context) {
    (void)context;
    memset(buffer, 0x5a, size);
    return true;
}
static bool service_ready(void *context) { (void)context; return true; }
static bool transport_busy(void *context) { (void)context; return false; }
static bool transport_connected(void *context) {
    (void)context; return g_connected;
}
static int transport_connect(const char *peer, const char *token,
                             uint32_t generation, void *context) {
    (void)context;
    assert(strcmp(peer, "peer") == 0);
    assert(strcmp(token, "token") == 0);
    assert(generation != 0);
    g_connected = true;
    return 0;
}
static int transport_start_media(void *context) { (void)context; return 0; }
static int transport_send(uint32_t command, const void *data,
                          uint32_t length, void *context) {
    (void)context;
    assert(command == XIAOTAI_ROOM_COMMAND);
    assert(data != 0 && length != 0);
    return 0;
}
static int transport_disconnect(void *context) {
    (void)context; g_connected = false; return 0;
}
static void set_uplink(bool enabled, void *context) {
    (void)context; g_uplink = enabled;
}

static void post(xiaotai_room_response_t type, uint32_t generation,
                 const char *text, size_t length, void *context) {
    (void)length; (void)context;
    xiaotai_room_handle_response(g_room, type, generation, text);
}
static int service_request(const char *path, const char *json,
                           xiaotai_room_service_response_fn callback,
                           void *callback_context, void *context) {
    (void)context;
    (void)json;
    if (strstr(path, "assignment") != 0) {
        ++g_assignment_requests;
        callback("{\"code\":0,\"data\":{\"desired_state\":\"joined\","
                 "\"assignment_version\":1,\"room_id\":\"room-1\","
                 "\"room_code\":\"123456\"}}", callback_context);
    } else if (strstr(path, "connect-token") != 0) {
        callback("{\"code\":0,\"data\":{\"peer_id\":\"peer\","
                 "\"token\":\"token\",\"heartbeat_seconds\":15,"
                 "\"lease_seconds\":45}}", callback_context);
    } else if (strstr(path, "presence") != 0) {
        callback("{\"code\":0,\"data\":{}}", callback_context);
    }
    return 0;
}
static bool acquire(uint32_t *generation, void *context) {
    (void)context; g_acquired = true; *generation = 9; return true;
}
static bool activate(uint32_t generation, void *context) {
    (void)context; assert(generation == 9); g_activated = true; return true;
}
static void release(uint32_t generation, void *context) {
    (void)context; assert(generation == 9); g_released = true;
}
static bool available(void *context) { (void)context; return true; }
static void changed(void *context) { (void)context; ++g_changed; }
static void closed(void *context) { (void)context; g_closed = true; }

int main(void) {
    xiaotai_room_t room;
    g_room = &room;
    xiaotai_room_port_t port = {
        .service_request = service_request, .now_ms = now_ms,
        .random_bytes = random_bytes, .service_ready = service_ready,
        .transport_busy = transport_busy,
        .transport_connected = transport_connected,
        .transport_connect = transport_connect,
        .transport_start_media = transport_start_media,
        .transport_send = transport_send,
        .transport_disconnect = transport_disconnect,
        .set_uplink_enabled = set_uplink,
        .post_response = post, .media_acquire = acquire,
        .media_activate = activate, .media_release = release,
        .media_available = available, .state_changed = changed,
        .transport_closed = closed,
    };
    xiaotai_room_init(&room, "device-1", &port);
    xiaotai_room_tick(&room);
    assert(g_assignment_requests == 0);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_SYNC, 0) == 0);
    xiaotai_room_tick(&room);
    assert(g_assignment_requests == 1);
    assert(room.assigned && room.connecting && g_acquired);
    xiaotai_room_handle_connected(&room, room.generation);
    xiaotai_room_handle_command(&room, room.generation,
        "{\"id\":1,\"result\":{\"session_id\":\"session\","
        "\"input_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,"
        "\"channels\":1},\"output_audio\":{\"codec\":\"g711a\","
        "\"sample_rate\":8000,\"channels\":1}}}");
    assert(room.joined && !room.connecting && g_activated && !g_uplink);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_TALK_START, 0) == 0);
    assert(room.ptt && g_uplink);
    xiaotai_room_snapshot_t snapshot;
    xiaotai_room_snapshot(&room, &snapshot);
    assert(snapshot.joined && snapshot.talking);
    xiaotai_room_handle_disconnected(&room, room.generation, 0);
    assert(!room.joined && g_released && g_closed && g_uplink);
    g_now += 600000;
    xiaotai_room_tick(&room);
    assert(g_assignment_requests == 1);
    xiaotai_room_assignment_changed(&room);
    xiaotai_room_tick(&room);
    assert(g_assignment_requests == 2);
    assert(g_changed != 0);
    return 0;
}
''')
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(tmp_path), "-I", str(REPO_ROOT / "product/include"),
        "-I", "/usr/include/cjson",
        str(REPO_ROOT / "product/src/xiaotai_room.c"),
        str(test_source), "-lcjson", "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "call_protocol_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(REPO_ROOT / "product/include"),
        "-I", "/usr/include/cjson",
        str(REPO_ROOT / "product/src/xiaotai_call_protocol.c"),
        str(REPO_ROOT / "product/tests/test_call_protocol.c"),
        "-lcjson", "-o", str(output),
    ], check=True)
    subprocess.run([str(output)], check=True)

print("BK7258 XiaoTai contract checks passed")
