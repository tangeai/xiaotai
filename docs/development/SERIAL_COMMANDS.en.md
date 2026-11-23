# ESP32 and BK7258 serial commands

[中文](SERIAL_COMMANDS.md) · [English](SERIAL_COMMANDS.en.md)

The two ESP32-S3 boards, Waveshare ESP32-P4 and both BK7258 boards share the service command names below starting with v1.2.0-beta.3. Use the flashed image’s help to check availability.

## Connect and enter commands

Follow the board guide and close the flasher before opening the port. Use 115200 baud, 8 data bits, no parity and one stop bit for a UART connection. Send one command per line.

| Terminal | Discover commands | Example |
|---|---|---|
| ESP32 application console, prompt `starter>` | `help` | `status` |
| BK7258 CP shell, forwarding to the AP | `ap_cmd help` | `ap_cmd status` |

Examples below use the application command. **On BK prefix each line with `ap_cmd `.** Bare `help` at the BK CP lists CP commands.

Replace angle-bracketed values and omit brackets around optional arguments. Use double quotes around names containing spaces. A submitted request completes asynchronously. Check status and subsequent logs.

## Status and logging

| Command | Purpose |
|---|---|
| `version` | Show firmware identity and the linked TiRTC SDK version. |
| `status` | Network, TiRTC and current session; individual fields depend on platform |
| `call-status` | Call type, state and generation; BK also reports its deadline |
| `log-level` | Current application and TiRTC log levels |
| `log-level app <0..4>` | Application level: 0 off, 1 error, 2 warning, 3 information, 4 debug |
| `log-level tirtc <0..17>` | TiRTC SDK level; SDK-defined values do not follow the application's 0..4 meanings |

On BK, in `status`, `owner` identifies the current activity, `pending` the pending call, and `state` the runtime state number. `internal_free` / `internal_min` are current free internal SRAM / minimum since startup; `psram_free` / `psram_min` are the corresponding external PSRAM values, all in bytes.

`bound=1` means local credentials are saved; also check platform status to confirm service availability.

Normally keep information logging:

```text
log-level app 3
```

On BK, run `log-level app 4` before capturing periodic `METRICS` and video frame-rate statistics.

Use `log-level app 4` temporarily for diagnosis. Levels are not persisted and revert to firmware defaults after reboot. This command cannot enable SDK logs removed at compile time. Remove passwords, tokens, and other secrets before sharing logs.

## Wi-Fi, binding, and reboot

| Command | Effect |
|---|---|
| `wifi-change` | Disconnect Wi-Fi and open the setup hotspot, retaining saved network information |
| `wifi-set <SSID> <password>` | Save Wi-Fi and reboot; use `""` for an open network's password |
| `wifi-clear` | Clear saved Wi-Fi and reboot into setup |
| `tirtc-set <device_id> <device_secret> [client_id]` | Preload credentials for controlled integration and restart. Protect terminal history. |
| `tirtc-clear` | Clear local binding credentials and reboot into binding; retain Wi-Fi |
| `restart` | Reboot while retaining saved settings |

End AI, live view, or calls before using `wifi-change`. Connect a phone to the device hotspot, open `http://192.168.6.1`, and enter network information. Alternatively save it over serial:

```text
wifi-set "Home WiFi" "your-password"
```

Replace the example with the actual name and password. The terminal may retain input history. `wificlear` is a compatibility alias for `wifi-clear`, entered with the usual board prefix.

## AI and calls

The device must be online and bound before starting AI or making a call. Calling a contact also requires contact synchronization. Answer and hangup commands act on the current incoming or active call.

| Command | Purpose | Boards |
|---|---|---|
| `ai-start` | Start an AI conversation | All five |
| `ai-stop` | End the AI conversation | All five |
| `wechat-call ["contact-name"]` | Call a named WeChat contact; if omitted, choose the first WeChat contact in platform order | All five |
| `call-answer` | Answer the pending call | All five; MGKFB supports WeChat incoming calls only |
| `wechat-answer` | Answer a WeChat call only | All five |
| `call-hangup` | End the call or cancel dialing | All five |
| `device-call "contact-name"` | Call a named device contact | ESP32 and LCKFB BK |
| `device-answer` | Answer a device call only | ESP32 and LCKFB BK |

For example, call the WeChat contact named “哥哥”:

```text
wechat-call "哥哥"
call-status
```

## Group intercom (ESP32 and LCKFB BK)

On ESP touch boards, joining updates the assignment. Open the Room page and confirm the media connection before using push-to-talk.

MGKFB does not support group intercom, even if commands appear in help.

| Command | Purpose |
|---|---|
| `room-status` | Assignment, connection, membership, talk state, and member count |
| `room-sync` | Request a refresh of platform room assignment |
| `room-join <6-digit-room-number> [4-digit-password]` | Request to join a room |
| `room-talk-start` | Start talking |
| `room-talk-stop` | Stop talking and resume listening |
| `room-leave` | Leave the room |

These are serial maintenance commands, not additional automatic refresh triggers for the product UI. Pair `room-talk-start` with `room-talk-stop`; after starting, explicitly send the stop command.

## Change the service-discovery address

Normal Wi-Fi setup does not require this. Use it for self-hosted platforms or specific test environments:

| Command | Effect |
|---|---|
| `ep-show` | Show the saved service-discovery base URL; `default` means the firmware default |
| `ep-set <URL>` | Save a new base URL and reboot |
| `ep-reset` | Remove the override, restore the firmware default, and reboot |

For a base URL with `/pre`:

```text
ep-show
ep-set http://ep-open.tangeopen.com/pre
```

After reboot, the device requests `http://ep-open.tangeopen.com/pre/services`. The URL must begin with `http://` or `https://`; ports and paths are allowed, but queries (`?`) and fragments (`#`) are not. TiRTC uses the response's `tirtc-srv` field; `ep-set` sets the discovery entry point, not TiRTC directly.

Use `ep-reset` to restore the default.

## Time synchronization server

| Command | Effect |
| --- | --- |
| `ntp-show` | Show saved primary override; fallback output indicates a missing, unreadable, or corrupt override |
| `ntp-set time.example.com` | Save a hostname or IPv4 primary server and reboot |
| `ntp-reset` | Clear the override and reboot |

Input is at most 253 characters, without a URL scheme, port, path or whitespace.
IPv6 is not supported. Existing fallback servers remain; NTP uses UDP/123.
Successful changes interrupt active sessions; failed saves do not reboot.
Factory reset also clears this setting. Verify synchronization after reboot.

The default primary is `pool.ntp.org`, with `ntp.aliyun.com` as fallback.
`ntp-show` reports saved configuration, not whether synchronization succeeded.
Replace `time.example.com` with a reachable server. Example using a LAN server:

```text
ntp-set 192.168.1.10
# After reboot
ntp-show
# Restore defaults and reboot
ntp-reset
```

## Platform diagnostics

| Command | Scope and purpose |
|---|---|
| `task-diag` | ESP samples selected tasks for five seconds. BK reports its latest task, CPU and stack sample. |
| `voice-config <50..990>`, `voice-diag [5..30]`, `voice-test self-test` | ESP voice threshold, audio levels, and text-injection self-test. |
| `face-animation [on/off]` | ESP temporary face-animation control. Use on or off as one argument. |
| `media-diag`, `video-submit` | ESP development probes. Media accepts on/off/show/reset. Video accepts pause/resume/show and needs a connected P4 device call for control. |
| `audio-trace`, `audio-capture` | S3 debug images with packet trace enabled. Both accept arm/stop/status/dump/clear. Arm while idle; captures may contain private audio. |
| `camera-snapshot`, `video-capture`, `downlink-capture` | Waveshare P4 development capture when registered. Consult local help for capture formats. |

ESP app log control changes the ESP-IDF default filter. Its compiled maximum can limit debug output; the TiRTC level is set separately. BK metrics names and counters are platform-specific.

For one AI test, run `version`, `status`, `ai-start`, speak several short replies, then run `ai-stop` and `status`. Save the session-end summary.

## Implementation and validation entry points

Command registration, argument validation, and return text are in [`xiaotai_console.c`](../../platforms/beken/runtime/src/xiaotai_console.c); request handling is in [`xiaotai_app.c`](../../platforms/beken/runtime/src/xiaotai_app.c). Both BK boards link the same implementation.

Refer to each guide for GPIO, serial wiring, and supported features.

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

Use decimal IPv4 without leading zeroes: `10.0.0.1` is accepted; `010.0.0.1` is rejected.
NTP replies must match the requested peer and request, and report a synchronized server; invalid replies fall back to the next server.

After `tirtc-clear` succeeds, late background binding results cannot restore credentials before reboot.
If identity storage is busy, retry the command. If restart scheduling fails, use `restart`; a manual clear can also be retried.

After `wifi-set` / `wifi-clear` succeeds, background provisioning cannot overwrite the recovery before restart.
The portal asks you to restart before saving again. Storage failures ask for a retry instead of reporting an invalid password.

ESP registration and option checks are in [starter_console.c](../../platforms/esp-idf/components/starter_console/src/starter_console.c).
