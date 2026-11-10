# BK7258 Serial Commands

[中文](BK7258_SERIAL_COMMANDS.md) · [English](BK7258_SERIAL_COMMANDS.en.md)

Applies to [LCKFB BK7258](../boards/lckfb-bk7258/README.en.md) and [MGKFB AI Toy Development Board BK7258](../boards/mgkfb-bk7258/README.en.md). Both boards share the command implementation, but firmware determines supported features.

MGKFB supports AI, WeChat calls, and browser live view, but not device-to-device calls or group intercom. This guide follows current source; use `ap_cmd help` to check flashed firmware, as older releases may lack newer commands. ESP32 commands and serial entry points are outside this guide.

## Connect and enter commands

Connect serial as described in the board guide and use **115200 baud, 8 data bits, no parity, 1 stop bit**. After flashing, close the download connection and open a serial terminal. Enter one command per line and press Enter.

The usual serial connection exposes the communication-core (CP) shell; XiaoTai runs on the application core (AP). Prefix application commands with **`ap_cmd` followed by a space**:

```text
ap_cmd help
ap_cmd status
ap_cmd call-status
```

If entering `status` returns `cmd NOT found: status`, check the prefix. `help` lists CP commands; `ap_cmd help` lists AP commands, including SDK commands. This page documents only XiaoTai application commands.

`<...>` denotes a value to replace; `[...]` denotes an optional argument. Do not type the brackets. Quote names containing spaces with double quotes. `requested` means the request was submitted; subsequent logs and status establish completion. For `rejected`, check connectivity, binding, and the current session.

## Status and logging

| Command | Purpose |
|---|---|
| `ap_cmd status` | Network, saved Wi-Fi, setup hotspot, IP, binding, TiRTC, current session, and memory |
| `ap_cmd call-status` | Call type, incoming/outgoing direction, state, session generation, and timeout deadline |
| `ap_cmd log-level` | Current application and TiRTC log levels |
| `ap_cmd log-level app <0..4>` | Application level: 0 off, 1 error, 2 warning, 3 information, 4 debug |
| `ap_cmd log-level tirtc <0..17>` | TiRTC SDK level; SDK-defined values do not follow the application's 0..4 meanings |

In `status`, `owner` identifies the current activity, `pending` the pending call, and `state` the runtime state number. `internal_free` / `internal_min` are current free internal SRAM / minimum since startup; `psram_free` / `psram_min` are the corresponding external PSRAM values, all in bytes.

`bound=1` means local credentials are saved; also check platform status to confirm service availability.

Normally keep information logging:

```text
ap_cmd log-level app 3
```

Use `ap_cmd log-level app 4` temporarily for diagnosis. Levels are not persisted and revert to firmware defaults after reboot. This command cannot enable SDK logs removed at compile time. Remove passwords, tokens, and other secrets before sharing logs.

## Wi-Fi, binding, and reboot

| Command | Effect |
|---|---|
| `ap_cmd wifi-change` | Disconnect Wi-Fi and open the setup hotspot, retaining saved network information |
| `ap_cmd wifi-set <SSID> <password>` | Save Wi-Fi and reboot; use `""` for an open network's password |
| `ap_cmd wifi-clear` | Clear saved Wi-Fi and reboot into setup |
| `ap_cmd tirtc-clear` | Clear local binding credentials and reboot into binding; retain Wi-Fi |
| `ap_cmd restart` | Reboot while retaining saved settings |

End AI, live view, or calls before using `wifi-change`. Connect a phone to the device hotspot, open `http://192.168.6.1`, and enter network information. Alternatively save it over serial:

```text
ap_cmd wifi-set "Home WiFi" "your-password"
```

Replace the example with the actual name and password. The terminal may retain input history. `wificlear` is a compatibility alias for `wifi-clear`, used as `ap_cmd wificlear`.

## AI and calls

The device must be online and bound before starting AI or making a call. Calling a contact also requires contact synchronization. Answer and hangup commands act on the current incoming or active call.

| Command | Purpose | Boards |
|---|---|---|
| `ap_cmd ai-start` | Start an AI conversation | Both |
| `ap_cmd ai-stop` | End the AI conversation | Both |
| `ap_cmd wechat-call ["contact-name"]` | Call a named WeChat contact; if omitted, choose the first WeChat contact in platform order | Both |
| `ap_cmd call-answer` | Answer the pending call | Both; MGKFB supports WeChat incoming calls only |
| `ap_cmd wechat-answer` | Answer a WeChat call only | Both |
| `ap_cmd call-hangup` | End the call or cancel dialing | Both |
| `ap_cmd device-call "contact-name"` | Call a named device contact | LCKFB |
| `ap_cmd device-answer` | Answer a device call only | LCKFB |

For example, call the WeChat contact named “哥哥”:

```text
ap_cmd wechat-call "哥哥"
ap_cmd call-status
```

## Group intercom (LCKFB only)

MGKFB does not support group intercom, even if commands appear in help.

| Command | Purpose |
|---|---|
| `ap_cmd room-status` | Assignment, connection, membership, talk state, and member count |
| `ap_cmd room-sync` | Request a refresh of platform room assignment |
| `ap_cmd room-join <6-digit-room-number> [4-digit-password]` | Request to join a room |
| `ap_cmd room-talk-start` | Start talking |
| `ap_cmd room-talk-stop` | Stop talking and resume listening |
| `ap_cmd room-leave` | Leave the room |

These are serial maintenance commands, not additional automatic refresh triggers for the product UI. Pair `room-talk-start` with `room-talk-stop`; after starting, explicitly send the stop command.

## Change the service-discovery address

Normal Wi-Fi setup does not require this. Use it for self-hosted platforms or specific test environments:

| Command | Effect |
|---|---|
| `ap_cmd ep-show` | Show the saved service-discovery base URL; `default` means the firmware default |
| `ap_cmd ep-set <URL>` | Save a new base URL and reboot |
| `ap_cmd ep-reset` | Remove the override, restore the firmware default, and reboot |

For a base URL with `/pre`:

```text
ap_cmd ep-show
ap_cmd ep-set http://ep-open.tangeopen.com/pre
```

After reboot, the device requests `http://ep-open.tangeopen.com/pre/services`. The URL must begin with `http://` or `https://`; ports and paths are allowed, but queries (`?`) and fragments (`#`) are not. TiRTC uses the response's `tirtc-srv` field; `ep-set` sets the discovery entry point, not TiRTC directly.

Use `ap_cmd ep-reset` to restore the default.

## Implementation and validation entry points

Command registration, argument validation, and return text are in [`xiaotai_console.c`](../../platforms/beken/runtime/src/xiaotai_console.c); request handling is in [`xiaotai_app.c`](../../platforms/beken/runtime/src/xiaotai_app.c). Both boards link the same implementation.

Refer to each guide for GPIO, serial wiring, and supported features.
