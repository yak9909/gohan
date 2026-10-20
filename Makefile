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
GEN := $(BUILD)/gen
CPPFLAGS := -D__3DS__ -Isrc -I$(GEN) -isystem third_party/libctrpf/include -isystem $(CTRULIB)/include
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
# make が spec から作る C++（メニューの表）。
GEN_SOURCES := $(GEN)/menu_table.cpp
OBJECTS := $(addprefix $(OBJDIR)/,$(CPP_SOURCES:.cpp=.o) $(GEN_SOURCES:.cpp=.o) $(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o) $(ASM_RAW_SOURCES:.s=.o))
LIBRARY := build/libctrpf/libctrpf.a
FONT_HEADER := $(GEN)/font_atlas.hpp
FONT_INPUTS := third_party/fonts/misaki/misaki_gothic_2nd.bdf third_party/fonts/pixelmplus/mplus_j10r-iso-W5.bdf
FONT_CODEGEN := tools/gohan_tools/codegen/font_atlas.py
UI_HEADERS := $(GEN)/ui_values.hpp $(GEN)/ui_parts.hpp $(GEN)/ui_atlas.hpp
UI_CODEGEN := tools/gohan_tools/codegen/ui.py tools/gohan_tools/codegen/ui_shapes.py

.PHONY: all libctrpf clean FORCE host-test
all: out/gohan.3gx

libctrpf:
	@$(MAKE) --no-print-directory -C third_party/libctrpf release

# メニューの表と docs/menu.md を spec/menu.toml から作る。
$(GEN)/menu_ids.hpp $(GEN)/menu_table.cpp docs/menu.md &: spec/menu.toml tools/gohan_tools/codegen/menu.py
	@mkdir -p $(GEN)
	$(PYTHON) tools/gohan_tools/codegen/menu.py --spec spec/menu.toml --out $(GEN) --docs docs/menu.md

# サブ make 完了後に書庫の更新時刻を判定する。
$(LIBRARY): libctrpf
	@test -f $@

$(FONT_HEADER): $(FONT_CODEGEN) $(FONT_INPUTS) tools/gohan_tools/codegen/glyph_overrides.py tools/gohan_tools/codegen/misaki_characters.txt tools/gohan_tools/codegen/numeric_characters.txt
	$(PYTHON) $(FONT_CODEGEN) --output $@

$(UI_HEADERS) &: spec/ui.toml $(UI_CODEGEN) $(FONT_CODEGEN)
	$(PYTHON) tools/gohan_tools/codegen/ui.py --spec spec/ui.toml --out $(GEN)

$(OBJDIR)/src/ui/renderer/font_resource.o: $(FONT_HEADER)
$(OBJDIR)/src/ui/renderer/font_resource.o: $(UI_HEADERS)

$(OBJDIR)/%.o: %.cpp Makefile | libctrpf $(GEN)/menu_ids.hpp $(UI_HEADERS)
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
