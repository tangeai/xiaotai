# Agent guidance

When changing behavior or fixing a defect, read
[`tests/README.md`](tests/README.md) and use a red-green
loop through the affected public interface. Add or strengthen a regression test
that fails before the implementation change, register it in `tools/check.sh`,
and run `bash tools/check.sh` before completion. For documentation-only changes
or mechanical refactors that preserve behavior, state which existing tests
cover the change instead of adding a tautological test. Report the test evidence
and any remaining hardware verification in the final handoff.

When adding a board, changing project layout, or editing build dispatch, read
[the architecture](ARCHITECTURE.md) first. Register hardware in
`boards/<vendor>/<board>/board.json`, then run:

```bash
python3 tools/build.py --validate
python3 -m unittest discover -s tools/tests -v
```

Keep ESP-IDF and Beken as separate SDK projects. Reuse product behavior through
stable C interfaces and platform adapters; keep GPIO, buses, DMA, clocks, power
sequencing, and peripherals in board/platform code. During migration, preserve
the existing project directories and use `tools/build.py` as the only new
cross-platform dispatch interface.

Treat a board manifest as build metadata, not hardware proof. Hardware claims
belong in the referenced Hardware IR or probe report and retain their evidence
grade. A build selects exactly one board and one variant.

When changing UI, buttons, voice controls, or user-visible flow, read
[`PRODUCT_INTERACTION_PROFILES.md`](docs/product/PRODUCT_INTERACTION_PROFILES.md)
and [`product/interaction/README.md`](product/interaction/README.md). Keep
business state in the product runtime; interaction adapters emit intents and
render its view model. Select explicit interaction and layout profiles in each
board variant. Quick call always uses the platform-ordered `contacts[0]`.

When changing room-assignment triggers, read the room-assignment section in
[`PRODUCT_INTERACTION_PROFILES.md`](docs/product/PRODUCT_INTERACTION_PROFILES.md#64-房间分配刷新).
MQTT and an explicit user entry are the only assignment refresh intents: the
Room page for `touch-full`, or long PTT while disconnected for
`display-key`/`headless-key`.
