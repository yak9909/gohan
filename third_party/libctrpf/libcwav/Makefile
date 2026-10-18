# 同梱版: ソースを変更せず、生成物をプロジェクトの build/ に配置する。
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM を設定してください)
endif
include $(DEVKITARM)/3ds_rules
SOURCE_ROOT := $(patsubst %/,%,$(dir $(abspath $(firstword $(MAKEFILE_LIST)))))
BUILD_ROOT := $(abspath $(SOURCE_ROOT)/../../../build/libctrpf/libcwav)
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
ifeq ($(AUDIO_STAGE),)
.DEFAULT_GOAL := all
.PHONY: all clean install uninstall
all:
	@mkdir -p $(BUILD_ROOT)
	@$(MAKE) --no-print-directory -C $(BUILD_ROOT) -f $(SOURCE_ROOT)/Makefile AUDIO_STAGE=1
clean:
	@rm -rf $(BUILD_ROOT)
install uninstall:
	@echo "同梱ライブラリのインストールとアンインストールは無効です。"
	@false
else
.DEFAULT_GOAL := $(BUILD_ROOT)/libcwav.a
VPATH := $(SOURCE_ROOT)/source
INCLUDE := -I$(SOURCE_ROOT)/include -I$(CTRULIB)/include -I$(SOURCE_ROOT)/libncsnd/include
CFLAGS := -g -Wall -O2 -mword-relocations -fomit-frame-pointer -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__ -DCWAV_DISABLE_DSP
ASFLAGS := -g $(ARCH)
CFILES := $(notdir $(wildcard $(SOURCE_ROOT)/source/*.c))
OFILES := $(CFILES:.c=.o)
DEPSDIR := $(CURDIR)
$(BUILD_ROOT)/libcwav.a: $(OFILES)
-include $(OFILES:.o=.d)
endif
