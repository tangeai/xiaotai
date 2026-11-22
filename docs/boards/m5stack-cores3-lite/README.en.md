# M5Stack CoreS3-Lite

[Back to boards](../README.en.md) · [Firmware and release notes](../../downloads.en.md#m5stack-cores3-lite)

[中文](README.md) · [English](README.en.md)

Board ID: `m5stack-cores3-lite`. See [Downloads](../../downloads.en.md#m5stack-cores3-lite) for the supported ILI9342C firmware. Short-phrase recognition, AI interruption, and double-talk still need regression testing; see [Validation status and limitations](#validation-status-and-limitations).

## Flashing

The filename uses the distribution version; compare embedded firmware identity with the release notes. For local flashing, download one complete BIN. Screenshot versions are examples.

### Confirm the board

This project supports **CoreS3-Lite K128-Lite / ILI9342C** only. The download page provides one complete image; there is no display choice or build configuration for other panels.

### Flash with the browser

Connect Type-C to the computer. Hold the bottom reset button for about 3 seconds, release when the green LED lights, and wait until it goes out to enter download mode. Detection must show **ESP32-S3**. Use a USB data cable and close any serial monitor using the port.

The merged image overwrites NVS, including saved Wi-Fi, binding and EP settings. Set up Wi-Fi and bind again after flashing.

1. Open the [XiaoTai Web Flasher](https://xiaotai.chat/web-flasher/) in desktop Chrome or Edge. Under **芯片平台** (chip platform), select **乐鑫 Espressif（ESP32 系列）**. Click **连接设备** (connect device) and select the board’s serial port in the browser prompt.
2. Select **小钛固件** (published firmware), choose **M5Stack开发板 · ESP32-S3(CoreS3-Lite)** under **开发板** (board), then select the release under **固件版本** (version). For a downloaded image, select **本地 BIN** (local BIN) and open the [release file](../../downloads.en.md#m5stack-cores3-lite) ending in `m5stack-cores3-lite_ili9342c_merged.bin`.
3. Confirm the complete image starts at `0x0` and click **开始烧录** (start flashing). Keep the board connected until **烧录过程** (flash log) reports completion.
4. After reboot, open **串口日志** (serial log) at 115200 baud. Check `m5stack_cores3_lite_xiaotai` and the embedded version against the release notes before Wi-Fi setup.

![XiaoTai Web Flasher with m5stack-cores3-lite selected](../../images/flashing/xiaotai-m5stack-cores3-lite.png)

Connector and download-mode details are in the [official guide](https://docs.m5stack.com/en/core/CoreS3-Lite).

The complete image is `v<release-version>_m5stack-cores3-lite_ili9342c_merged.bin`. Flash only this file at `0x0`.

Compare its SHA-256 with the release notes. Use `Get-FileHash -Algorithm SHA256 "BIN-path"` on Windows, `sha256sum "BIN-path"` on Linux, or `shasum -a 256 "BIN-path"` on macOS.

## First use

Confirm the complete `_merged.bin` was flashed at `0x0` and the device rebooted, then configure Wi-Fi and bind.

1. **Wi-Fi:** without saved credentials, the screen prompts for setup. Connect a phone/computer to the open hotspot beginning `XiaoTai-`; “no internet” is normal. Select 2.4 GHz Wi-Fi in the setup page, enter the password, and submit. If no page opens, visit `http://192.168.6.1`. A reappearing hotspot indicates failure; check password and signal.
2. **Binding:** after connection, the display shows a six-digit code. Restore the phone/computer's internet connection, open the [XiaoTai device page](https://xiaotai.chat/devices), sign up or sign in, and enter the code under Add device. Use a refreshed code if expired. The home screen and an entry in the device list indicate success.
3. **Try features and recover:** use the touch menu for AI, contacts, group intercom, and settings. To change network or rebind, confirm Reset device in settings, then set up and bind again. Do not erase the entire Flash.

Wi-Fi and binding credentials are stored in NVS; source code contains no account or password.

## Other flashing methods

### Build and flash from source

Follow [Building from source](../../../README.en.md#build-from-source) to obtain the repository and install this board's ESP-IDF. From the root:

```bash
. tools/activate_esp_idf.sh
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342c
cd firmware/esp-idf/esp32s3/m5stack-cores3-lite
idf.py -p <serial-port> flash monitor
```

Default and explicit builds both select ILI9342C. Replace `<serial-port>` with the actual port; `idf.py` uses the project manifest and addresses. Ordinary upgrades do not use `erase-flash`. The screenshot is an example, not proof these commands ran on the current board:

![CoreS3-Lite build and flash commands](../../images/flashing/project-m5stack-cores3-lite.png)

## Specifications

| Item | Configuration |
|---|---|
| Chip / framework | ESP32-S3 / **ESP-IDF v5.5.5** |
| Storage | 16 MB Flash / 8 MB Quad PSRAM, configured at 80 MHz |
| Display / touch | 320×240 ILI9342C / FT6336U |
| Camera | GC0308 with onboard 24 MHz clock |
| Audio | ES7210 dual-microphone input and electrical playback reference; AW88298 + 1 W speaker |
| Power | AXP2101 + AW9523B, 200 mAh battery (official rating, capacity unmeasured) |
| Interaction | `touch-full` / `compact`, 16-pixel Chinese font |

## Audio and video parameters

These are current-source parameters shared by both screen variants. See [Downloads](../../downloads.en.md#m5stack-cores3-lite) for available firmware and its feature scope. All audio is mono, with 20 ms packets.

| Scenario | Direction and stream ID | Codec and sample rate | Packet or video parameters |
|---|---|---|---|
| AI conversation | Uplink/downlink: 1 | Uplink A-law / 16 kHz; downlink Opus / 16 kHz | 320-byte uplink; variable Opus size |
| H5 two-way audio | Uplink 10; downlink 14 | A-law / 8 kHz both ways | 160 bytes |
| Device calls | Bidirectional audio 10 | A-law / 8 kHz both ways | 160 bytes |
| WeChat voice | Bidirectional audio 0 | A-law / 8 kHz both ways | 160 bytes |
| H5 live video | Uplink 11 | MJPEG | 320×240, target maximum 8 fps; actual frame rate requires measurement |

Device and WeChat calls are audio-only. WeChat reports `up_video_mt=none`, `down_video_mt=none`, and `no_video=true`, using A-law / 8 kHz on audio stream 0. Home quick-call submits `wx_room_type=voice`; contact details offer voice calls only.

S3 policy constrains runtime, UI, camera tasks, and SDK send entry points: accidental video requests or old subscriptions must not start VoIP video uplink. LCKFB S3 also uses voice-only calls; P4 retains its capabilities. Room PTT uses the shared room runtime.

With the camera at the top, `stream/call/voip.camera_rotation` and device-call request/answer metadata report numeric `180`. H5 receivers rotate raw MJPEG accordingly; firmware does not additionally rotate camera pixels.

Orientation metadata retained for voice calls does not imply video support: M5 device/WeChat calls send no video, while H5 retains it. Capabilities are resubmitted after upgrade and connection; close old live-view pages and reopen to verify orientation.

Hardware capture and playback stay at 16 kHz. I2S0 four-slot TDM RX and two-slot standard I2S TX are created together, sharing 64fs BCLK and 256fs MCLK; AW88298 BCLK configuration is read back after writing.

Following Espressif's CoreS3 BSP, AW88298 uses 15 dB PA gain compensation for correct volume attenuation mapping. The same setting may sound quieter than early adaptations; sound quality and double-talk still need hardware confirmation.

The first two ES7210 wire slots are MIC1 speech and MIC3 electrical speaker reference. Board code packs them as MR for shared ESP-SR AEC. Schematics prove wiring only; echo performance, clipping, double-talk, and acoustic delay require measurement.

See the [audio review (Chinese)](../../../boards/m5stack/cores3-lite/OFFICIAL_AUDIO_REVIEW.md) for official-source checks, gain quantization, and evidence limits.

If continuous conversation requires repetition or is hard to interrupt during playback, run `voice-diag 30` then `status` to capture playback, clipping, uplink, and I2S overflow in the same window. See [capture diagnostics (Chinese)](../../development/MEDIA_PIPELINE_DIAGNOSTICS.md#cores3-持续对话采音观测).

These commands read statistics without changing audio settings.

Shared ESP internal configuration `CONFIG_STARTER_AI_UPLINK_G711A_16K=y` fixes the AI uplink format. AI start_session uses the official protocol name `alaw` and accepts response alias `g711a`.

This jointly controls input declaration/validation, pre-recorded/live PCM, encoder selection, and TiRTC A-law / 16 kHz frame flags. Output declaration and decoding remain Opus / 16 kHz.

AEC stays at 16 kHz; AI directly encodes processed 16 kHz PCM, while H5 and calls retain 8 kHz paths. Server rejection of this input/output combination causes negotiation failure and termination, not a successful acoustic test.

LCKFB S3, Waveshare P4, and BK7258 use the same AI network formats while retaining their board DSP pipelines.

## Environment and dependencies

The application toolchain is **ESP-IDF v5.5.5**, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`. Run `. tools/activate_esp_idf.sh` from the root and verify `idf.py --version`. Install missing tools with `bash tools/setup_esp_idf.sh esp32s3`. `>=5.5,<5.6` is a compatibility range, not the selected version.

TiRTC SDK 2.6.3 comes from shared `platforms/esp-idf/third_party/tirtc/esp32s3` and follows its ABI build contract. Dependencies include LVGL 8.3.11, esp_lvgl_port 2.7.2, esp32-camera 2.1.7, esp-sr 2.4.7, esp_audio_codec 2.4.0, and esp_lcd_ili9341 2.1.0. `dependencies.lock` records resolved versions and hashes.

Builds do not read `.references` or user-supplied old projects.

## Schematics and hardware sources

Official page: [CoreS3-Lite (Chinese)](https://docs.m5stack.com/zh_CN/core/CoreS3-Lite).

For [schematic v1.0](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf), datasheets, BSP, and download SHA-256, see [Sources and reference decisions (Chinese)](../../../boards/m5stack/cores3-lite/SOURCES.md).

The current official page's audio GPIO13/14 directions conflict with the schematic. Implementation uses RX14/TX13, matching the schematic and official BSP. Both C and E panels exist; identify physical hardware before selecting firmware.

## Development

Board code is in `boards/m5stack/cores3-lite/`: `board_power.c` handles power/reset, `board_adapter.c` audio/camera, and `board_display.c` display/touch. `hardware-ir.json` stores sources, pins, and evidence grades.

S3 media orchestration is in `platforms/esp-idf/components/starter_media_s3/`; runtime, UI, setup, and other features use shared components directly.

The old `m5stack-cores3-se-xiaotai` supplies reference power sequencing, codec initialization, AW88298 clock readback, and ILI9342C/FT6336 initialization because sources confirmed matching parts/wiring. Its business state machine, UI, AGC, and ISR queues were not ported.

See [Reference notes (Chinese)](../../../boards/m5stack/cores3-lite/SOURCES.md) for files and reasons.

GPIO0 is audio MCLK, not a product button. Power/RST retains hardware functions; touch emits business intents. Only MQTT notifications and entry to the group-intercom page refresh room assignment; no startup or periodic queries are added.

Board `LCD_SWAP_XY` and `LCD_MIRROR_X/Y` define orientation. With the camera at the top, CoreS3 uses swap=false, mirror_x=true, mirror_y=true, rotating native landscape 180°. Panel, touch, and shared LVGL registration use the same values so UI initialization does not overwrite native 320×240 addressing.

## Building

Private component definition `XIAOTAI_UI_TEXT_PSRAM=1` places AI caption history (2048 bytes) and display text (2304 bytes) in static PSRAM BSS, reducing internal static allocation by 4352 bytes.

It uses [ESP-IDF v5.5.5 external BSS](https://github.com/espressif/esp-idf/blob/v5.5.5/docs/en/api-guides/external-ram.rst), retaining fixed capacity, boot-time clearing, and UI-lifetime storage without per-conversation allocation/free.

Effective configuration must enable `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` or compilation fails. LCKFB S3/P4 do not enable this definition. DMA, locks, task stacks, and LVGL object pools retain their allocation policies.

Reduced static usage does not prove equivalent contiguous-heap gains or faster AEC; measure the same build.

From the root, select one screen only:

```bash
. tools/activate_esp_idf.sh
idf.py --version
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342c

```

Omitting `--variant` selects the sole `ili9342c` option. The native project also rejects other panel configurations. Native output is `firmware/esp-idf/esp32s3/m5stack-cores3-lite/build/`; the application is `m5stack_cores3_lite_xiaotai.bin` / `.elf`. Use that directory's `flasher_args.json` for addresses.

Independent `BUILD_NUMBER` increments on every build, including failures, without changing product version.

Native CMake checks effective PSRAM configuration and rejects Octal or disabled PSRAM. Changing `sdkconfig.defaults` does not overwrite an existing Octal `sdkconfig`. In the project directory, run `idf.py menuconfig`, open `Component config → ESP PSRAM`, select Quad, save, and rebuild through the unified entry above.

Retain other local settings; NVS erase is unnecessary.

## Testing

```bash
python3 tools/tests/test_cores3_board.py
python3 tools/tests/test_cores3_memory_config.py
python3 tools/tests/test_s3_display_geometry.py
python3 tools/build.py --validate
python3 -m unittest discover -s tools/tests -v
bash tools/check.sh
```

Host tests cover variants/versions, real power-interface bit preservation and failure propagation, microphone/reference slot packing, both panel command sets, and GPIO0 exclusivity. PSRAM regressions check new-project defaults and execute real CMake checks, rejecting old Octal, disabled PSRAM, and conflicting settings.

Shared S3 regressions cover existing sessions, audio, playback tails, and UI. Code reuse does not establish this board's network or acoustic validation.

## Troubleshooting

`octal_psram: PSRAM chip is not connected, or wrong PSRAM line mode` followed by `Failed to init external RAM` means external RAM failed before `app_main`. Check effective `CONFIG_SPIRAM_MODE_QUAD=y` and disabled Octal. RST or network reset cannot fix the flashed bus mode; rebuild and flash.

Keep PSRAM initialization and memory testing enabled rather than ignoring errors.

For a dark screen, check C/E variant, PMIC backlight, and LCD reset before SPI; do not use LCKFB backlight GPIO42. For misplaced text or partial-screen output, check `cores3_display` startup logs for `native=320x240 UI=320x240 swap_xy=0 mirror=1/1` and match the physical panel.

For touch, check I²C0 SDA12/SCL11 and address 0x38; native landscape coordinates are not swapped. For camera initialization, check GC0308 PID, external clock, and AW9523/AXP power; do not silently substitute another model.

For silence, check RX14/TX13, codec addresses, and AW88298 register 0x06 64fs readback, then test speech-only, speaker-only, and double-talk slots. GPIO0 must not be a button input.

## Validation status and limitations

The 1.1.0 test firmware adds AI audio analysis: triggered raw MIC1 16 kHz PCM retains up to 5 seconds of pre-recording and records 10 more seconds for upload. The home AI icon toggles recognition; its background shows 60-second cooldown, status shows recording/upload, and results scroll twice with spoken output.

Recognition pauses during calls, live view, AI conversations, and room activity. MIC3 playback reference is excluded; thresholds and acoustics still require M5 hardware validation.

The historical ILI9342C release is **v1.0.1 · build.48**. The current beta still needs hardware acceptance; historical results do not validate it.

See [Release records (Chinese)](FREEZE.md) for features/fixes and [Validation (Chinese)](../../../boards/m5stack/cores3-lite/VALIDATION.md) for build identity, scope, and historical logs.

- Capture, AI interruption, and double-talk need ongoing testing. Unstable recognition of the short Chinese phrase “好的” and possible loss of initial speech remain unresolved.
- This version does not offer experimental recording export to TF cards. Diagnose with `voice-diag`, `status`, and `face-animation`.
- AEC and UI processing headroom during playback remains tight. Weak networks, repeated switching, and long runs need continued regression. See [Audio performance review (Chinese)](AUDIO_PERFORMANCE_REVIEW.md).
- Only the ILI9342C complete image is provided; see Downloads for availability.
- SD card, IMU, magnetometer, proximity sensor, and RTC are not integrated into XiaoTai features.

## Serial time-server configuration

`ntp-show` displays the saved primary NTP override (`default` means no override).
Use `ntp-set time.example.com` or `ntp-set 192.168.1.10` to save it;
`ntp-reset` restores the default.

Successful set/reset restarts the device
and interrupts active calls. A save failure does not restart it.

Accepts a DNS hostname or IPv4 address, up to 253 characters; no URL, port, path,
spaces or IPv6. NTP uses UDP/123. Existing backup servers remain available, so
this is a primary-server preference, not an exclusive time source.

Factory reset
clears the override. Confirm synchronization after reboot before testing calls.

## Serial recovery during startup failures

Recovery configuration commands open before media, UI, and networking initialization. They do not depend on
Wi-Fi connectivity, binding, time synchronization, authentication, or platform availability.

Use `wifi-set`, `wifi-clear`, `tirtc-clear`, `ntp-show/set/reset`, `ep-show/set/reset`, and `restart`
with the arguments documented for your board. Prefix BK commands with `ap_cmd`; enter ESP32 commands directly.

Until the local runtime is ready, `status` reports recovery mode; calls, media, and voice diagnostics return
`Local runtime not ready`. Follow the command's restart message after a successful configuration change.

A failed write must not report success or reboot the device.

This entry cannot repair an unavailable serial driver, a system hang, or damaged Flash. If storage initialization
fails, diagnostics and restart remain available, but saving configuration may fail.

Validate unconfigured Wi-Fi, an unbound device, unreachable NTP, authentication failure, media initialization failure,
and network reconnection.

In each case query, change, reboot, and verify persistence. Host fault injection does not
replace serial validation on hardware running the exact image.

Use decimal NTP IPv4 addresses without leading zeroes, such as `10.0.0.1`; `010.0.0.1` is rejected.

Successful `tirtc-set` / `tirtc-clear` blocks background binding writes until reboot; retry if storage is busy.
Credentials use a versioned record, with legacy reads supported. Old keys are retired before publishing a new identity.
Upgrading does not require rebinding.

After changing credentials, clear binding in the current firmware before downgrading to firmware without the new record format.
Rebind after downgrading to avoid loading stale legacy credentials.

Wi-Fi SSID and password are saved as one record, with legacy reads supported.
A failed save cannot combine a new SSID with an old password. A save attempt may migrate the format even if the final update fails;
downgrading to firmware without this format requires Wi-Fi setup again.

Volume, speaker/microphone mute, and other preferences also use one record with legacy reads supported.
After migration, older firmware uses its defaults; check sound and mute settings again.

After `wifi-set` / `wifi-clear` succeeds, background provisioning cannot overwrite that recovery before restart.
If the portal saves configuration but cannot schedule a restart, it asks for a manual restart.
Run `restart` or power-cycle the device.

Factory reset blocks background configuration writes. Retry later if storage is busy or clearing fails.

If asynchronous TiRTC startup exceeds 30 seconds, recovery commands remain available while business commands are disabled.
Inspect or update configuration, then run `restart`. The firmware does not repeatedly initialize an SDK whose lifecycle state is unknown.
