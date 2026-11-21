# BK audio lifecycle fixtures

Source: Beken bk_avdk_smp, release/v3.1.1.10, commit
`398cab28b938e69ba2071a597daf00845dd080b6`.
Repository: https://gitee.com/bekencorp/bk_avdk_smp.git

These Apache-2.0 sources retain their copyright headers and license.
They are test inputs, not a second SDK or firmware build target.
Line endings and trailing empty lines are normalized. The speaker diagnostics/debug-level and abort-write
patches and the microphone abort-read patch from `platforms/beken/tools/patches/`
are already applied, matching the input to the lifecycle patch in `build.sh`.

`test_bk_sdk_audio_lifecycle.py` applies the lifecycle patch to a temporary copy
and replays the resulting production functions with host fakes. Hardware DMA,
RTOS scheduling and physical audio still require native builds and board tests.
