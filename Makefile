.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM を devkitARM の場所に設定してください)
endif
include $(DEVKITARM)/3ds_rules

DEV ?= 0
LTO ?= 0
BUILD ?= build
ifeq ($(filter build build/%,$(BUILD)),)
$(error BUILD は build または build/ 以下にしてください)
endif
PYTHON ?= python
THREEGXTOOL ?= 3gxtool
# 設定を切り替えても古いオブジェクトを再利用しない。
OBJDIR := $(BUILD)/plugin-dev$(DEV)-lto$(LTO)
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -marm
CPPFLAGS := -D__3DS__ -Isrc -isystem third_party/libctrpf/include -isystem $(CTRULIB)/include
CFLAGS := $(ARCH) -Os -Wall -Wextra -ffunction-sections -fdata-sections -mword-relocations
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions
LDFLAGS := $(ARCH) -Os -T plugin/3gx.ld -Wl,--gc-sections,-Map,$(BUILD)/gohan.map
ifeq ($(DEV),1)
CPPFLAGS += -DGOHAN_DEV=1
endif
ifeq ($(LTO),1)
CFLAGS += -flto
CXXFLAGS += -flto
LDFLAGS += -flto
endif
CPP_SOURCES := $(shell find src -type f -name '*.cpp' | LC_ALL=C sort)
C_SOURCES := $(shell find src -type f -name '*.c' | LC_ALL=C sort)
ASM_SOURCES := $(shell find src -type f -name '*.S' | LC_ALL=C sort)
ASM_RAW_SOURCES := $(shell find src -type f -name '*.s' | LC_ALL=C sort)
OBJECTS := $(addprefix $(OBJDIR)/,$(CPP_SOURCES:.cpp=.o) $(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o) $(ASM_RAW_SOURCES:.s=.o))
LIBRARY := build/libctrpf/libctrpf.a

.PHONY: all libctrpf clean FORCE host-test
all: out/gohan.3gx

libctrpf:
	@$(MAKE) --no-print-directory -C third_party/libctrpf release

# サブ make 完了後に書庫の更新時刻を判定する。
$(LIBRARY): libctrpf
	@test -f $@

$(OBJDIR)/%.o: %.cpp Makefile | libctrpf
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.c Makefile | libctrpf
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.S Makefile | libctrpf
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ARCH) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.s Makefile | libctrpf
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ARCH) -x assembler-with-cpp -MMD -MP -c $< -o $@

# 共通の出力名なので、設定変更・入力の削除も含めてリンクし直す。
out/gohan.elf: $(OBJECTS) $(LIBRARY) plugin/3gx.ld Makefile FORCE
	@mkdir -p out $(BUILD)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBRARY) -L$(CTRULIB)/lib -lctru -lm -o $@

out/gohan.3gx: out/gohan.elf plugin/gohan.plgInfo tools/size_report.py
	$(THREEGXTOOL) -s $< plugin/gohan.plgInfo $@
	$(PYTHON) tools/size_report.py $< --size-tool $(PREFIX)size --last $(BUILD)/size_last.json

host-test:
	$(PYTHON) tests/host/run.py

# 削除は既定の生成先に限定する。
clean:
	rm -rf build out

FORCE:
-include $(OBJECTS:.o=.d)
