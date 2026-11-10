# Development Pitfalls and Regression Entry Points

[中文](pitfalls.md) · [English](pitfalls.en.md)

Review this page before changing UI, audio, sessions, or packaging. It retains lasting constraints and regression entry points. Build identities, original symptoms, and integration timelines are in the [historical regression archive (Chinese)](../../tests/DEVELOPMENT_REGRESSION_NOTES.md).

Host tests do not replace board-level touch, direct memory access (DMA), or listening tests.

## Where knowledge belongs

[Contributing](../../CONTRIBUTING.en.md) and [Architecture](../../ARCHITECTURE.en.md) define development and release rules; [Testing](../../tests/README.en.md) defines validation methods. Extend existing entries and link tests rather than creating duplicate rule sources.

Run commands below from the repository root; use `bash tools/check.sh` for the full check.

## BK idle reboot: atomic operations depend on memory regions

Correct C11 atomics on a host do not guarantee safe operation in every target SRAM region. BK `platforms/beken/runtime/src/xiaotai_tirtc.c` places atomic close-handover state in the pinned SDK's `.sram_spinlock_section` to meet exclusive-monitor memory restrictions.

Retain section-placement checks using the target compiler.

Heartbeat timeout is a protective action; the last HTTP, audio, or RTC log cannot identify the root cause by itself. Do not hide scheduling stalls by extending timeouts. Chip memory behavior and prolonged concurrent media use still require hardware testing.

Regressions: `test_bk_room_close_callback.py`, `test_bk_event_reentry.py`, and `test_bk_close_reconnect.py`, all in `tools/tests/`.

## Asynchronous room operations need timeouts and stale-reply isolation

Before submitting a request, `product/src/xiaotai_room.c` saves an independent request ID, connection generation, and deadline; the adapter returns that original identity. Allocation or queue failure must not cause an indefinite wait, and late replies must not complete newer requests.

Connection success is not join success; connection, join, and lease timeouts need separate handling.

Room-close notifications must match both room ID and assignment version. Duplicate success replies must not restart media, and stale assignments must not revive closed rooms.

Recovery must not add background assignment polling; refresh entry points still follow the [Interaction contract (Chinese)](../product/PRODUCT_INTERACTION_PROFILES.md#64-房间分配刷新).

Regressions: `test_room_async_recovery.py`, `test_esp_signal_recovery.py`, and `test_product_core.py` in `tools/tests/`. Real weak-network and SDK scheduling behavior still need coverage.

## ESP MQTT online status requires subscription confirmation

`platform_client.c` must receive successful SUBACKs for both command and notification subscriptions before reporting online. Disconnect and reconnect if submission fails or a subscription is rejected. Clear subscription state on disconnect; duplicate or mismatched acknowledgments must not cause premature online status.

An established MQTT connection alone does not prove platform commands can be received.

Regression: `python3 tools/tests/test_esp_signal_recovery.py`. Hardware testing must also cover recovery after a real broker rejects subscriptions.

## Packaging must verify binary identity and preserve verification status

`tools/esp_image_identity.py` checks the target chip, version, image boundaries, checksum, and ELF digest. `tools/package_esp_validation.py` performs these checks before packaging. Unified and standalone entry points must pass the same verification status.

Matching image identity does not prove the source commit. Current ESP packages leave `source_commit` empty and record packaging-time HEAD separately as `packaging_source_commit`. Do not attribute old images to the current checkout. A successful build does not prove hardware-in-the-loop (HIL) validation.

Deliver original paths, versions, and SHA-256 checksums.

Regressions: `python3 tools/tests/test_esp_package_identity.py` and `python3 tools/tests/test_validation_packaging.py`.

## Room digits must not share action IDs with ordinary buttons

ESP UI adapter `starter_product.c` uses separate ID ranges for ordinary actions, digits, and contacts. Retain `_Static_assert` checks when adding actions. If specific digits consistently invoke specific wrong actions, check ID collisions first.

Tests must capture IDs from actual rendered buttons and pass them to production dispatch, rather than inventing an independent ID set. Regression: `python3 tools/tests/test_room_keypad.py`. After layout changes, verify physical touch hit areas too.

## Rejoining a room must initialize the new lease

When the ESP join callback accepts the current success reply, initialize the lease and renewal time before submitting the presence request. Waiting for asynchronous HTTP success can let an old lease close the new connection prematurely.

Clear leases on exit; duplicate replies must not extend them, and stale-generation or invalid replies must not start them.

Regression: `python3 tools/tests/test_room_lease_lifecycle.py`. Hardware testing must cover repeated creation, exit, and rejoin.

## Top-layer prompts must be centered within their parent

Ordinary page offsets must not be applied directly to prompts on `lv_layer_top()`. Center the text and prompt separately. Use `LV_ALIGN_TOP_MID` for the prompt and scale vertical spacing by display height.

Regression: `python3 tools/tests/test_tip_layout.py`. Check occlusion, rotation, and touch regions on hardware across layouts.

## AI audio must absorb bursts and drain playback before exit

Network arrival cadence is not speaker playback cadence. AI queues must absorb bursts without speeding playback to remove backlog. Finite queues cannot guarantee zero loss in every weak-network or overload scenario.

On server completion or remote closure, drain received audio according to exit policy. Completion checks must include the receive queue, frames being decoded or written, and the board's playback tail. An empty queue or successful write does not mean the speaker has finished.

User cancellation, drain timeout, and stale-session cleanup follow separate policies; neither unconditional waiting nor unconditional dropping is correct.

Record received, decoded, written, overflowed, pending, and active frames, PCM duration, and playback tail together. The difference between cumulative audio duration and total conversation time includes conversational silence and cannot establish playback speed. The device cannot play audio the server never sent.

Regressions: `test_p4_ai_playback.py`, `test_bk_ai_playback.py`, and `test_bk_disconnect_playback.py` in `tools/tests/`. Per-board listening tests must cover normal completion, remote closure, user cancellation, and repeated retries.

## Disconnect reasons and warnings require bounded conclusions

Sending a heartbeat does not mean it timed out. A generic transport code alone does not establish a server idle timeout. A close callback after normal completion does not prove truncated audio; inspect the end command and final drain statistics.

BK `Task exits abnormally!` may come from an SDK task-return path and does not itself prove deadlock. Log the SDK disconnect call, callback entry, and worker completion to locate the stalled stage. Temporary logs can alter timing, so occasional success is not proof of a fix.

Validate ringtone, wake-buffer, and call-playback paths separately.

## BK group-intercom release events must be delivered reliably

A full control queue must not lose touch release. Store the release generation independently and let the control task emit the stop intent. Read actual touch state while pressed and return to interrupt waiting after release. Queue emptiness or hold duration cannot establish release.

Entry points are `touch_event`, `service_room_touch_release`, and SDK `tp_process_task`.

Regressions: `test_bk_ptt_release_delivery.py` and `test_bk_touch_release_poll.py` in `tools/tests/`. On hardware, verify uplink stops after long presses, rapid presses, release outside the button, and concurrent platform notifications.

## ESP32: do not erase NVS directly from a PSRAM task stack

Flash writes or erases may disable caches; a PSRAM stack cannot directly execute that operation. Increasing the stack or suppressing assertions does not fix it. `runtime_config_clear_tirtc()` uses an internal-RAM-stack task and waits for erase and commit completion.

Task creation, erase, and commit failures must return errors. Clear binding credentials only, retaining network and product settings.

Regression: `python3 tools/tests/test_unbind_flash_stack.py`. On hardware, unbind while idle, in AI, and during a call; verify the binding page after reboot, retained networking, and successful rebinding.

## Binding ACK arrives while verification-code speech continues

A matching binding acknowledgment (ACK) must cancel verification-code playback. Check cancellation before and after each playback and during intervals. Report binding success only after credentials are saved, and allow a visible prompt before a controlled reboot.

Asynchronous cancellation must not block message handling, and old playback generations must not cancel newer speech.

Regression: `python3 tools/tests/test_binding_completion.py`. On hardware, test binding during speech, the success prompt, rebinding, and returning online.

## ESP32-S3: write counts do not prove playback completion

The shared runtime needs S3 to populate queue state, worker-held frames, and DMA tail state. Increment ownership before publishing a frame to the queue; failed enqueue, consumption, and queue clearing each return their own ownership. A generation change must not simply zero counters.

Protect 64-bit totals and tail timestamps with synchronization. DMA capacity estimates are not actual playback levels; verify with recordings or listening.

Regressions: `test_s3_playback_drain.py` and `test_s3_opus_media.py` in `tools/tests/`.

## P4/BK: separate atomic fields do not form a consistent drain snapshot

Reading atomic fields separately can combine values into a nonexistent “queue empty, tail expired” state. Writers publish the tail deadline before releasing frame ownership. Readers read pending/active ownership before the tail deadline, retaining acquire/release ordering.

Do not rely on C function-argument evaluation order.

Use the P4/BK AI playback and disconnect regressions, exercising actual production status entry points. Passing host tests still requires per-board listening validation.

## ESP32-S3/P4: wake acknowledgment and connection need separate audio gates

Network requests may proceed asynchronously with the local acknowledgment, but media startup must await both platform acceptance and local input readiness. Close buffer admission during the acknowledgment, retain a tail-isolation interval afterward, and accept only complete capture frames after the boundary.

AEC or a single queue clear cannot by itself exclude late frames.

Admission belongs to the current token, or session-request identity. Cancellation, mute, rejected playback, and expired queue items must all complete cleanup. An old token's completion must not open the next session's microphone; acknowledgment timeout must not simply permit audio.

Entry points are `starter_media_prepare_ai_preroll`, `call_ring_task`, and `service_ai_media_admission`.

Regressions: `tools/tests/test_wake_ack_overlap.py`, `tools/tests/test_wake_audio_boundary.py`, and `tools/test_voice_reliability.py` in the native S3 project. On hardware, test fast and slow networks, maximum volume and reverberation, the first word after acknowledgment, and repeated cancellation/retry.

## Maintenance

New entries explain symptoms, confirmed causes or unverified hypotheses, implementation constraints, production entry points, and regressions. Reproduce failure before fixing code, and register regressions in `tools/check.sh`.

Archive historical evidence separately; do not generalize one board or build's results to all platforms and versions.
