#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/3ds_rules

# 版は同梱ソースに合わせて固定し、ビルド時の取得や更新は行わない。
export CTRPF_REVISION := 0.8.0-gohan
export CTRPF_VERSION_MAJOR := 0
export CTRPF_VERSION_MINOR := 8
export CTRPF_VERSION_BUILD := 0
export COMMIT := a502818c
export COMPILE_DATE := $(shell date -u +"%Y-%m-%dT%H:%M:%S%Z")

ENABLE_LINK_TIME_OPTIMIZATIONS = 0

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
TARGET		:=	ctrpf
#BUILD		:=	build
SOURCES		:=	source \
				source/CTRPluginFramework \
				source/CTRPluginFramework/Graphics \
				source/CTRPluginFramework/Menu \
				source/CTRPluginFramework/Sound \
				source/CTRPluginFramework/System \
				source/CTRPluginFramework/Utils \
				source/CTRPluginFrameworkImpl \
				source/CTRPluginFrameworkImpl/ActionReplay \
				source/CTRPluginFrameworkImpl/Disassembler \
				source/CTRPluginFrameworkImpl/Graphics \
				source/CTRPluginFrameworkImpl/Graphics/Icons \
				source/CTRPluginFrameworkImpl/Menu \
				source/CTRPluginFrameworkImpl/Search \
				source/CTRPluginFrameworkImpl/Sound \
				source/CTRPluginFrameworkImpl/System \
				source/CTRPluginFrameworkImpl/System/Services \
				source/ctrulibExtension \
				source/ctrulibExtension/allocator \
				source/ctrulibExtension/gpu \
				source/ctrulibExtension/services \
				source/ctrulibExtension/system \
				source/ctrulibExtension/util/utf \
				source/ctrulibExtension/util/rbtree \
				source/lodepng


#DATA		:=	data
INCLUDES	:=	include

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

DEFINES	:=	-DCTRPF_VERSION_MAJOR="\"$(CTRPF_VERSION_MAJOR)\"" -DCTRPF_VERSION_MINOR="\"$(CTRPF_VERSION_MINOR)\"" \
			-DCTRPF_VERSION_BUILD="\"$(CTRPF_VERSION_BUILD)\"" -DCOMMIT_HASH="\"$(COMMIT)\"" -DCOMPILE_DATE="\"$(COMPILE_DATE)\""

GCC11_FALSEPOSITIVES := -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-overread
CFLAGS	:=	-Wall -Werror -Wno-psabi -mword-relocations \
			-ffunction-sections \
			-fdata-sections \
			$(GCC11_FALSEPOSITIVES) \
			$(ARCH) \
			$(BUILD_CFLAGS) $(G)

CFLAGS	+=	$(INCLUDE) -D__3DS__ $(DEFINES)

ASFLAGS	:=	$(G) $(ARCH) $(INCLUDE) -Os -Wl,$(WL)--gc-sections

ifneq ($(ENABLE_LINK_TIME_OPTIMIZATIONS), 0)
	CFLAGS += -flto=auto -ffat-lto-objects
	ASFLAGS += -flto=auto -ffat-lto-objects
endif

CXXFLAGS 	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++20

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBS	:= -lctru -lm -lcwav -lncsnd
SOURCE_ROOT := $(patsubst %/,%,$(dir $(abspath $(firstword $(MAKEFILE_LIST)))))
BUILD_ROOT := $(abspath $(SOURCE_ROOT)/../../build/libctrpf)
LIBDIRS := $(CTRULIB) $(SOURCE_ROOT)/libcwav $(SOURCE_ROOT)/libcwav/libncsnd

ifeq ($(CTRPF_STAGE),)
.DEFAULT_GOAL := all
.PHONY: all release debug clean install install-upstream
all: release
release debug:
	@$(MAKE) --no-print-directory -C $(SOURCE_ROOT)/libcwav/libncsnd
	@$(MAKE) --no-print-directory -C $(SOURCE_ROOT)/libcwav
	@mkdir -p $(BUILD_ROOT)/$@
	@$(MAKE) --no-print-directory -C $(BUILD_ROOT)/$@ -f $(SOURCE_ROOT)/Makefile CTRPF_STAGE=$@

clean:
	@rm -rf $(BUILD_ROOT)

install install-upstream:
	@echo "同梱ライブラリのインストールは無効です。build/libctrpf の出力を使ってください。"
	@false
else
VPATH := $(foreach dir,$(SOURCES),$(SOURCE_ROOT)/$(dir))
INCLUDE := -I$(SOURCE_ROOT)/include $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I.
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(SOURCE_ROOT)/$(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(SOURCE_ROOT)/$(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(SOURCE_ROOT)/$(dir)/*.s)))
OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
DEPSDIR := $(CURDIR)
CFLAGS += $(INCLUDE)
CXXFLAGS += $(INCLUDE)
ASFLAGS += $(INCLUDE)
ifeq ($(CTRPF_STAGE),release)
CFLAGS += -DNDEBUG=1 -Os -fomit-frame-pointer
CXXFLAGS += -DNDEBUG=1 -Os -fomit-frame-pointer
OUTPUT := $(BUILD_ROOT)/libctrpf.a
else
CFLAGS += -DDEBUG=1 -g
CXXFLAGS += -DDEBUG=1 -g
ASFLAGS += -g
OUTPUT := $(BUILD_ROOT)/libctrpfd.a
endif
.DEFAULT_GOAL := archive
.PHONY: archive
archive: $(OUTPUT)
# 別々の書庫も保持し、結合版には音声ライブラリの全オブジェクトを含める。
$(OUTPUT): $(OFILES) $(BUILD_ROOT)/libcwav/libcwav.a $(BUILD_ROOT)/libncsnd/libncsnd.a
	@rm -f libctrpf-core.a
	@$(AR) -rc libctrpf-core.a $(OFILES)
	@printf 'create ../$(notdir $(OUTPUT))\naddlib libctrpf-core.a\naddlib ../libcwav/libcwav.a\naddlib ../libncsnd/libncsnd.a\nsave\nend\n' | $(AR) -M
# 版と日時の埋め込みを次回更新する。
	@rm -f PluginMenuTools.o
-include $(OFILES:.o=.d)
endif
