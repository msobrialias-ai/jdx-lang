# ============================================================
# JDX Build System
# ============================================================

TARGET ?= native

SRC := $(shell find src -type f -name '*.cpp')
OBJ := $(SRC:.cpp=.o)
DEP := $(OBJ:.o=.d)

BUILD ?= release

# ------------------------------------------------------------
# Toolchain selection
# ------------------------------------------------------------

ifeq ($(TARGET),native)
	CROSS :=
	BIN := jdx
endif

ifeq ($(TARGET),windows-x64)
	CROSS := x86_64-w64-mingw32-
	BIN := jdx.exe
endif

ifeq ($(TARGET),windows-x86)
	CROSS := i686-w64-mingw32-
	BIN := jdx.exe
endif

ifeq ($(TARGET),linux-arm64)
	CROSS := aarch64-linux-gnu-
	BIN := jdx
endif

ifeq ($(TARGET),linux-armhf)
	CROSS := arm-linux-gnueabihf-
	BIN := jdx
endif

CXX   := $(CROSS)g++
STRIP := $(CROSS)strip

# ------------------------------------------------------------
# Build flags
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
	-Isrc \
	-MMD \
	-MP

ifeq ($(BUILD),debug)

	CXXFLAGS := \
	$(COMMON_FLAGS) \
	$(WARNINGS) \
	-g3 \
	-O0

else

	CXXFLAGS := \
	$(COMMON_FLAGS) \
	$(WARNINGS) \
	-Werror \
	-O3 \
	-flto \
		-fomit-frame-pointer \
	-ffunction-sections \
	-fdata-sections \
	-DNDEBUG

	LDFLAGS += \
	-flto \
	-Wl,--gc-sections

endif

# ------------------------------------------------------------
# Build
# ------------------------------------------------------------

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) $(LDFLAGS) -o $@
ifeq ($(BUILD),release)
	$(STRIP) $@ || true
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ------------------------------------------------------------
# Toolchain checks
# ------------------------------------------------------------

check:
	@command -v $(CXX) >/dev/null 2>&1 || \
	( echo "Compiler $(CXX) not found"; exit 1 )

# ------------------------------------------------------------
# Cleaning
# ------------------------------------------------------------

clean:
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f jdx
	rm -f jdx.exe

distclean: clean

-include $(DEP)

.PHONY: all clean distclean check
