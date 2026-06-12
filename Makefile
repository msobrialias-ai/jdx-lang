SHELL := /bin/bash

TARGET        ?= native
BUILD         ?= release
PROJECT       ?= jdx
TEST_NAME     ?= unit_tests

ENABLE_FAST_MATH ?= 0
ENABLE_LTO       ?= 1

SRC_DIR       := src
TEST_DIR      := tests

# ------------------------------------------------------------
# Target aliases
# ------------------------------------------------------------
ifeq ($(TARGET),x64)
TARGET := linux-x86_64
endif
ifeq ($(TARGET),amd64)
TARGET := linux-x86_64
endif
ifeq ($(TARGET),x86_64)
TARGET := linux-x86_64
endif

ifeq ($(TARGET),x86)
TARGET := linux-i686
endif
ifeq ($(TARGET),i386)
TARGET := linux-i686
endif
ifeq ($(TARGET),i686)
TARGET := linux-i686
endif

ifeq ($(TARGET),arm64)
TARGET := linux-arm64
endif
ifeq ($(TARGET),aarch64)
TARGET := linux-arm64
endif

ifeq ($(TARGET),armhf)
TARGET := linux-armhf
endif
ifeq ($(TARGET),armv7)
TARGET := linux-armhf
endif
ifeq ($(TARGET),arm32)
TARGET := linux-armhf
endif

ifeq ($(TARGET),riscv64)
TARGET := linux-riscv64
endif

ifeq ($(TARGET),powerpc64)
TARGET := linux-powerpc64
endif
ifeq ($(TARGET),android)
TARGET := android-arm
endif

# ------------------------------------------------------------
# Paths
# ------------------------------------------------------------
OUT_DIR      := build/$(TARGET)/$(BUILD)

APP_MAIN     := $(SRC_DIR)/main.cpp
TEST_SRC     := $(TEST_DIR)/unit_tests.cpp
GENERATED_HDR := generated/EmbeddedModules.hpp

APP_SRC      := $(shell find $(SRC_DIR) -type f -name '*.cpp' ! -name 'main.cpp' | sort)

APP_OBJ      := $(patsubst %.cpp,$(OUT_DIR)/%.o,$(APP_SRC))
APP_MAIN_OBJ := $(patsubst %.cpp,$(OUT_DIR)/%.o,$(APP_MAIN))
TEST_OBJ     := $(patsubst %.cpp,$(OUT_DIR)/%.o,$(TEST_SRC))

APP_BIN_NAME := $(PROJECT)
TEST_BIN_NAME := $(TEST_NAME)

APP_BIN      := $(OUT_DIR)/$(APP_BIN_NAME)
TEST_BIN     := $(OUT_DIR)/$(TEST_BIN_NAME)

# ------------------------------------------------------------
# Toolchain selection
# ------------------------------------------------------------
CROSS_PREFIX ?=
ARCH_FLAGS   :=
EXTRA_DEFS   :=

ifeq ($(TARGET),native)
	CROSS_PREFIX :=
endif

ifeq ($(TARGET),linux-x86_64)
	CROSS_PREFIX :=
endif

ifeq ($(TARGET),linux-i686)
	CROSS_PREFIX :=
	ARCH_FLAGS += -m32
endif

ifeq ($(TARGET),linux-arm64)
	ifeq ($(CROSS_PREFIX),)
		CROSS_PREFIX := aarch64-linux-gnu-
	endif
endif

ifeq ($(TARGET),linux-armhf)
	ifeq ($(CROSS_PREFIX),)
		CROSS_PREFIX := arm-linux-gnueabihf-
	endif
endif

ifeq ($(TARGET),linux-riscv64)
	ifeq ($(CROSS_PREFIX),)
		CROSS_PREFIX := riscv64-linux-gnu-
	endif
endif

ifeq ($(TARGET),linux-riscv32)
  ifeq ($(CROSS_PREFIX),)
    CROSS_PREFIX := riscv32-linux-gnu-
  endif
endif

ifeq ($(TARGET),linux-powerpc64)
  ifeq ($(CROSS_PREFIX),)
    CROSS_PREFIX := powerpc64-linux-gnu-
  endif
endif

ifeq ($(TARGET),android-arm) 
  ifeq ($(CROSS_PREFIX),)
    CROSS_PREFIX := arm-linux-androideabi-
  endif
endif

CC      ?= $(CROSS_PREFIX)gcc
CXX     ?= $(CROSS_PREFIX)g++
AR      ?= $(CROSS_PREFIX)ar
STRIP   ?= $(CROSS_PREFIX)strip
OBJDUMP ?= $(CROSS_PREFIX)objdump

# ------------------------------------------------------------
# Flags
# ------------------------------------------------------------
WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wconversion \
	-Wshadow \
	-Wformat=2 \
	-Wnull-dereference

COMMON_FLAGS := \
	-std=c++20 \
	-I. \
	-Isrc \
	-Itests \
	-MMD \
	-MP

CPPFLAGS += $(EXTRA_DEFS)
CXXFLAGS  += $(COMMON_FLAGS) $(WARNINGS) $(ARCH_FLAGS)

ifeq ($(BUILD),debug)
	CXXFLAGS += -g3 -O0
else
	ifeq ($(ENABLE_LTO),1)
		CXXFLAGS += -O2 -flto
		LDFLAGS  += -flto
	else
		CXXFLAGS += -O2
	endif

	CXXFLAGS += -ffunction-sections -fdata-sections -DNDEBUG
	LDFLAGS  += -Wl,--gc-sections
endif

ifeq ($(ENABLE_FAST_MATH),1)
	CXXFLAGS += -ffast-math
endif

ifeq ($(TARGET),linux-i686)
	LDFLAGS += -m32
endif

LDLIBS ?=

# ------------------------------------------------------------
# Phony targets
# ------------------------------------------------------------
.PHONY: all build test check info clean distclean help

all: build
build: $(APP_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

check:
	@command -v $(CXX) >/dev/null 2>&1 || { echo "Compiler not found: $(CXX)"; exit 1; }
	@command -v $(STRIP) >/dev/null 2>&1 || { echo "Strip tool not found: $(STRIP)"; exit 1; }

info:
	@echo "TARGET        = $(TARGET)"
	@echo "BUILD         = $(BUILD)"
	@echo "CROSS_PREFIX  = $(CROSS_PREFIX)"
	@echo "CXX           = $(CXX)"
	@echo "STRIP         = $(STRIP)"
	@echo "OUT_DIR       = $(OUT_DIR)"
	@echo "ARCH_FLAGS    = $(ARCH_FLAGS)"
	@echo "ENABLE_LTO    = $(ENABLE_LTO)"
	@echo "FAST_MATH     = $(ENABLE_FAST_MATH)"

help:
	@echo "Usage:"
	@echo "  make"
	@echo "  make test"
	@echo "  make TARGET=native"
	@echo "  make TARGET=linux-x86_64"
	@echo "  make TARGET=linux-i686"
	@echo "  make TARGET=linux-arm64"
	@echo "  make TARGET=linux-armhf"
	@echo "  make TARGET=linux-riscv64"
	@echo "  makw TARGET=android-arm"
	@echo "  make BUILD=debug"
	@echo "  make ENABLE_FAST_MATH=1"
	@echo "  make ENABLE_LTO=0"

# ------------------------------------------------------------
# Link rules
# ------------------------------------------------------------
$(APP_OBJ): $(GENERATED_HDR)
$(APP_MAIN_OBJ): $(GENERATED_HDR)
$(TEST_OBJ): $(GENERATED_HDR)

$(GENERATED_HDR): tools/generate_embed_modules.py $(shell find src/modules -type f -name '*.jdx' | sort)
	@mkdir -p $(dir $@)
	python3 tools/generate_embed_modules.py

$(APP_BIN): $(APP_OBJ) $(APP_MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(APP_OBJ) $(APP_MAIN_OBJ) $(LDFLAGS) $(LDLIBS) -o $@
ifeq ($(BUILD),release)
	$(STRIP) $@ || true
endif

$(TEST_BIN): $(APP_OBJ) $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(APP_OBJ) $(TEST_OBJ) $(LDFLAGS) $(LDLIBS) -o $@
ifeq ($(BUILD),release)
	$(STRIP) $@ || true
endif

# ------------------------------------------------------------
# Compile rules
# ------------------------------------------------------------
$(OUT_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
clean:
	rm -rf build

distclean: clean

# ------------------------------------------------------------
# Dependency includes
# ------------------------------------------------------------
-include $(APP_OBJ:.o=.d)
-include $(APP_MAIN_OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)
