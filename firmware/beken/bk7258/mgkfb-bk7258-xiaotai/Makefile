# Keep the product project separate from the vendor SDK. Override SDK_DIR when
# the SDK is stored elsewhere. See docs/boards/mgkfb-bk7258/README.md at
# repository root for the pinned baseline and native build prerequisites.
XIAOTAI_REPO_ROOT ?= $(shell dir='$(CURDIR)'; while [ "$$dir" != / ] && [ ! -f "$$dir/boards/schema/board.schema.json" ]; do dir=$$(dirname "$$dir"); done; printf '%s' "$$dir")
SDK_DIR ?= $(XIAOTAI_REPO_ROOT)/.tools/bk_avdk_smp-release-v3.1.1.10
XIAOTAI_TOOLCHAIN_DIR ?= $(XIAOTAI_REPO_ROOT)/.tools/gcc-arm-none-eabi-10.3-2021.10/bin
export XIAOTAI_TOOLCHAIN_DIR

PROJECT_MAKE_FILE := $(SDK_DIR)/tools/build_tools/build_files/project_main.mk
ifeq ($(wildcard $(PROJECT_MAKE_FILE)),)
    $(error "$(PROJECT_MAKE_FILE) not found; see docs/boards/mgkfb-bk7258/README.md and set SDK_DIR to official BK-AVDK SMP release/v3.1.1.10")
endif

include $(PROJECT_MAKE_FILE)
