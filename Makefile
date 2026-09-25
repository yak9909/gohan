.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR 		?= 	$(CURDIR)
include $(DEVKITARM)/3ds_rules

# ★libctrpf は gohan に同梱した v0.8.0 を改造して使う（devkitPro の $(DEVKITPRO)/libctrpf は使わない）。
#   出所は libctrpf/Makefile の先頭。ビルド済みの lib/libctrpf.a は Git に入れず、ここから作る。
CTRPFLIB	:=	$(TOPDIR)/libctrpf

# ★3gx の名前は gohan で固定する。フォルダ名に依存させない
#   （フォルダ名は CTRPluginFramework-BlankTemplate-0.8.0 のままなので、
#     $(notdir $(CURDIR)) にすると出力が別名になる）
TARGET		:= 	gohan
PLGINFO 	:= 	CTRPluginFramework.plgInfo

BUILD		:= 	Build
INCLUDES	:= 	Includes Includes/Gui Includes/Fonts Includes/ChatKanji Includes/Cheats Includes/Debug
SOURCES 	:= 	Sources Sources/Gui Sources/Fonts Sources/ChatKanji Sources/Cheats Sources/Debug

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS		:=	$(ARCH) -Os -mword-relocations \
				-fomit-frame-pointer -ffunction-sections -fno-strict-aliasing

CFLAGS		+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS		:=	$(ARCH)
LDFLAGS		:= -T $(TOPDIR)/3gx.ld $(ARCH) -Os -Wl,--gc-sections,--strip-discarded,--strip-debug

LIBS		:= -lctrpf -lctru
LIBDIRS		:= 	$(CTRPFLIB) $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES			:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES			:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD 		:= 	$(CXX)
export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I $(CURDIR)/$(dir) ) \
					$(foreach dir,$(LIBDIRS),-I $(dir)/include) \
					-I $(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L $(dir)/lib)

.PHONY: $(BUILD) clean all libctrpf clean-all

#---------------------------------------------------------------------------------
all: $(BUILD)

# 同梱 libctrpf を先に作る（ソースを直していなければ中身は作り直さない。版の文字列を持つ 1 ファイルだけは毎回作り直す）
libctrpf:
	@$(MAKE) --no-print-directory -C $(CTRPFLIB) lib/libctrpf.a

$(BUILD): libctrpf
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ... 
	@rm -fr $(BUILD) $(OUTPUT).3gx $(OUTPUT).elf

re: clean all

# 同梱 libctrpf の生成物も消す（ソースは消さない）
clean-all: clean
	@$(MAKE) --no-print-directory -C $(CTRPFLIB) clean

#---------------------------------------------------------------------------------

else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
# ★テンプレートの誤り: ここが $(OUTPUT).3gx : $(OFILES) になっていた。
#   devkitARM の base_rules の %.elf ルールは前提条件を持たないので、
#   .elf が一度できると二度とリンクされず、3gxtool だけが古い .elf から
#   .3gx を作り直す。ソースを直しても .3gx が変わらない、という症状になる。
#   .elf を .o に依存させるのが正しい。
#   ★.3gx を先に書くこと。最初のルールが既定ターゲットになるので、
#     .elf を先に書くと make が .elf までしか作らない。
$(OUTPUT).3gx : $(OUTPUT).elf
$(OUTPUT).elf : $(OFILES) $(CTRPFLIB)/lib/libctrpf.a

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS: %.elf
%.3gx: %.elf
#---------------------------------------------------------------------------------
	@echo creating $(notdir $@)
	@3gxtool -s $(word 1, $^) $(TOPDIR)/$(PLGINFO) $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
