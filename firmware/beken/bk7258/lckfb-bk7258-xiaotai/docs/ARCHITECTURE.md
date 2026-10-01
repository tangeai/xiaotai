# BK7258 XiaoTai product architecture

This project follows the Beken AVDK task/queue model but does not treat SDK
examples as product architecture.  The product control path is:

```text
bounded callback Router -> session Arbiter -> state-owning Coordinator
                                      |
                    TiRTC / cloud / media / UI / board Adapters
```

SDK, MQTT, button and touch callbacks copy bounded events and return.  The
`xiaotai_control` task is the only product-state Coordinator.  Blocking I/O and
realtime audio/video work may use worker tasks, but feature state machines must
not acquire their own product-state threads.

## Session policy

Incoming signaling and media ownership are separate.  One device or WeChat
call may occupy the pending slot while STREAM, AI or a group Room continues.
The incoming notification wakes the display but does not stop that activity.
Only explicit answer performs an ordered stop, waits for release, promotes the
pending call to media owner, and connects it.  A second incoming call is busy.

Media priority is:

```text
accepted device call = accepted WeChat call = group Room
    > AI conversation
    > H5 remote STREAM
```

The BK7258 build exposes one TiRTC/media slot.  STREAM admission therefore
requires no AI, device call, WeChat call or Room connection.  This is an
explicit product/resource rule, not a limitation inferred for every Beken SoC.

## Module seams

- `xiaotai_runtime`: shared product-core session Arbiter from the repository-level
  `product` directory. It owns generation/deadline and pending-incoming policy;
  its Interface is the host-test surface.
- `xiaotai_app`: Router/Coordinator that serializes external events and
  executes cross-module transitions; it does not implement Room protocol.
- `xiaotai_room`: shared deep Room Module owning assignment reconciliation,
  token and lease lifecycle, join protocol, PTT and disconnect recovery. Its
  Port owns all clock, random, cloud, transport, audio and session-admission
  integration and is replaced with fakes by the host lifecycle test.
- `xiaotai_ai_view`: shared product-core bounded AI caption/emotion state,
  including UTF-8 text and partial-caption throttling. The display adapter
  consumes its phase without owning the state model.
- `xiaotai_ai_protocol`: shared product-core AI JSON-RPC encoder/decoder and
  negotiated audio-profile validator.
- `xiaotai_contacts`: shared product-core bounded device/WeChat contact cache
  and deterministic first-contact/AI matching policy.
- `xiaotai_video_pacer`: shared product-core keyframe-preserving uplink pacing
  policy; capture and encoder control remain in the Beken media adapter.
- `xiaotai_call_protocol`: shared product-core device-call and WeChat-VoIP wire
  codec from the repository-level `product` directory. It owns
  bounded response decoding and JSON request construction so credentials and
  protocol field names do not leak across the Coordinator.
- `xiaotai_call_state`: shared product-core retained call credentials,
  outbound callback correlation, stale-callback suppression and identity
  matching. The BK Coordinator supplies time and executes network/media work.
- `xiaotai_signal`: shared product-core platform-notification decoder. Its
  visitor Interface presents typed, callback-scoped string views so each chip
  project shares event classification without retaining a JSON tree.
- `xiaotai_tirtc`: Adapter for the single process-wide TiRTC lifecycle.
- `xiaotai_platform_client`: cloud HTTP/MQTT Adapter.
- `xiaotai_audio` / `xiaotai_video`: bounded media engines.
- `xiaotai_ui`: display renderer and current product presentation.
- `xiaotai_network`: STA/SoftAP provisioning lifecycle.

Device/WeChat lifecycle remains coordinated with the shared Arbiter because it
must atomically switch AI, Room and STREAM ownership.  Remaining UI navigation
state may be moved behind a presenter seam without adding feature tasks or
changing the verified STREAM media path.

## Lifecycle and resource rules

Every long-lived Module must converge on `init/start/stop/deinit` with reverse
rollback after partial startup.  Internal/DMA-only SRAM is reserved for Beken
drivers, Wi-Fi/TLS/TiRTC and DMA descriptors.  Eligible framebuffers, frames,
large bounded payloads and non-realtime stacks use PSRAM.  Flash, internal SRAM
and PSRAM remain separate release gates.

Release evidence includes host contract tests, combined AP/CP build identity,
artifact-bound hardware tests, repeated remote-view cycles, session-switching
tests, and minimum/largest internal heap under worst-session load.

TiRTC is initialized once and shared by every normal business session. A normal
session must never stop or uninitialize the process-wide SDK. The sole exception
is a poisoned runtime: an asynchronous outgoing failure returns no connection
handle and retains internal heap after a bounded cleanup observation. In that
case the Coordinator serializes Stop, `SYS_STOPPED`, Uninit and restart while
network/MQTT remain alive. `SYS_STARTED` is followed by a heap comparison with
the pre-connect baseline; only a restored runtime returns to READY. A bounded
lifecycle failure or failed resource check falls back to a software reboot. The
state model, thresholds and hardware validation are specified in
[TiRTC single-instance recovery](TIRTC_RUNTIME_RECOVERY.md).
