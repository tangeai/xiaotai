# XiaoTai Testing and Hardware Validation

[中文](README.md) · [English](README.en.md)

This page describes test entry points, coverage requirements, and hardware acceptance. Historical failure cases, build identities, and validation scopes are in the [regression archive (Chinese)](REGRESSION_NOTES.md). Old records do not validate current firmware.

## Run host checks

From the repository root:

```bash
bash tools/check.sh
```

This runs product-core, platform, and board regressions, board-manifest validation, coverage checks, and patch whitespace checks. Product-core tests also use AddressSanitizer / UndefinedBehaviorSanitizer to check memory errors and undefined behavior. To repeat only host tests, use `bash tools/run_host_tests.sh`.

Local reports go in untracked `tests/.local/`. The coverage report is `tests/.local/reports/product-core-coverage.json`, with these minimum thresholds:

| Scope | Line coverage | Branch coverage |
|---|---|---|
| Product core overall | 88% | 57% |
| Session runtime | 95% | 65% |
| Call state | 95% | 60% |
| Group intercom | 80% | 50% |
| Media contract | 100% | 100% |

Coverage prevents regressions but does not replace feature acceptance. Threshold enforcement is defined in `tools/product_coverage.py`.

## Test-driven development

For behavior changes or defect fixes:

1. Add or strengthen a regression through the affected public interface and confirm it fails on the original implementation.
2. Change the implementation and confirm that test passes, also covering relevant errors, timeouts, and stale events.
3. Register new tests in `tools/check.sh` and run all checks.
4. For firmware changes, run the native target build. For hardware or product behavior changes, complete the corresponding hardware acceptance.

Prefer `product/include/` interfaces for shared business logic, with cases in `product/tests/`. Platform-internal tests supplement drivers, concurrency, callbacks, and conditional compilation; they do not replace product-interface tests.

Documentation and behavior-preserving mechanical refactors do not need tests that merely restate the implementation; identify existing coverage instead.

For board-manifest, repository-layout, or build-dispatch changes, also run:

```bash
python3 tools/build.py --validate
python3 -m unittest discover -s tools/tests -v
```

## Product acceptance matrix

Test only capabilities explicitly supported by the target board. Codecs, sample rates, resolutions, and frame rates follow the [Media contract (Chinese)](../docs/product/MEDIA_CONTRACT.md) and board guide. Each hardware result records the board, variant, firmware version, hash, steps, and observations.

| Feature | Required host checks | Required hardware checks |
|---|---|---|
| Wi-Fi setup and binding | Success, failure, retry, credential persistence, and stale events | Phone setup, verification-code binding, reboot recovery, and failure prompts |
| Browser live view and two-way audio | Media admission, stream IDs, release after disconnect | Audio in both directions, continuous video, reconnection, and echo |
| AI conversation | Negotiated formats, media after server acceptance, isolation of old events | Multiple conversation rounds, user/remote termination, restart, and audio feedback |
| Device and WeChat calls | Contact selection, pending calls, busy, media handover on answer, and timeout | Outgoing/incoming calls, rejection, hangup, bidirectional media, and repeated calls |
| Group intercom | Separate assignment and connection, refresh triggers, talk and release | Join, listen, hold to talk, release to stop, page departure, and room exit |
| Audio recognition | Triggers, quota, preemption, result and playback isolation | Recording/upload, result display and playback, repeated triggers, and failure recovery |

First-use steps for users are in the board guides. This matrix is for development and release acceptance; users do not need to perform internal diagnostics.

## Driver tests

Board audio, camera, display, and touch drivers need three kinds of evidence:

1. Interface contract tests: invalid input, frame ownership, start/stop order, and exit from blocking reads/writes.
2. Host simulations: at least 100 consecutive start/stop cycles, including applicable 8 kHz / 16 kHz switches.
3. Hardware-in-the-loop (HIL): real capture, playback, display, and transport, recording first frame, throughput, drops, remaining stack, and resource release.

For audio defects, record direction, stream ID, codec, sample rate, first-frame count, and return codes. For video, record resolution, codec, actual frame rate, and drops. Host simulations do not validate physical peripherals, interrupts, or cross-core timing.

## Release gates

Follow [Firmware versions and releases](../CONTRIBUTING.en.md#firmware-versions-and-releases) for target checks, release builds, packaging, and hardware acceptance. Releasing one board does not require building every board; shared implementation changes must cover all affected boards.

A successful build proves only that the build combination works. If the exact image has not completed hardware acceptance, retain `BUILD_VERIFIED_HIL_PENDING` and do not publish using old validation results. Reports must distinguish host checks, native builds, and hardware validation, and list outstanding checks.
