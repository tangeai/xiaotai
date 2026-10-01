# BK-AVDK loads this file after the SoC defaults. Keep the official toolchain
# version while allowing it to live outside the SDK's hard-coded /opt path.
ifneq ($(strip $(XIAOTAI_TOOLCHAIN_DIR)),)
COMPILER_TOOLCHAIN_PATH := $(XIAOTAI_TOOLCHAIN_DIR)
endif
