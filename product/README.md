# XiaoTai shared product core

This directory contains product behavior that is independent of a chip SDK,
RTOS, board pin map, display controller, audio driver, or TiRTC port.

Current shared modules:

- `xiaotai_runtime`: single-session ownership, generations, deadlines, incoming
  call admission, and remote-view policy.
- `xiaotai_call_protocol`: bounded device-call and WeChat-VoIP JSON wire codec.
- `xiaotai_call_state`: retained device/VoIP credentials, outbound callback
  correlation, stale-callback suppression, and call-identity matching.
- `xiaotai_contacts`: bounded contact caches, channel replacement,
  platform-ordered `contacts[0]` and first-of-channel selection, and AI/name
  intent resolution.
- `xiaotai_ai_view`: AI caption grouping, UTF-8 truncation, emotion fallback,
  and presentation throttling independent of a display implementation.
- `xiaotai_ai_protocol`: AI JSON-RPC request encoding, command routing, and
  negotiated audio-profile validation.
- `xiaotai_video_pacer`: source-to-uplink frame pacing that preserves keyframes.
- `xiaotai_room`: assignment, token, presence lease, join protocol, participant
  snapshot, and push-to-talk state behind an explicit platform port with typed
  diagnostics supplied to the platform adapter.
- `xiaotai_signal`: MQTT platform notification decoding into borrowed,
  typed views shared by every chip adapter.
- `xiaotai_media_contract`: canonical H5, AI, device-call, WeChat-VoIP and
  room stream IDs, codecs, sample rates and frame sizes.
- `xiaotai_intent`: chip-neutral logical input intents. Board button, voice and
  touch adapters translate physical events into this vocabulary before they
  reach product coordination.
- `interaction`: capability-based product interaction profiles for touch,
  display-with-buttons, and headless products.

Chip projects link the sources under `src` and provide platform adapters around
them. Do not add SDK headers, board conditionals, sockets, tasks, mutexes, or
media drivers here. Add portable host tests under `tests` whenever shared
behavior changes; each consuming chip project must also pass its native build
and contract checks before the behavior is considered integrated.

Run the repository-level product and board checks with:

```bash
python3 -m unittest discover -s tools/tests -v
```

Platform-independent behavior must be added here first. Chip projects retain
only SDK lifecycle, transport, media, hardware, tasking, and rendering adapters;
they may not fork these product contracts locally.
