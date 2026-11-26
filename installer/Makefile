#---------------------------------------------------------------------------------
# Pretendo Bridge Installer - based on devkitPro's 3DS application template.
#
# Needs devkitARM + libctru, and makerom + bannertool on the PATH for `make cia`.
# The six mode patches are embedded from data/*.ips;
# .github/scripts/build_installer.sh copies them there from ../ips/.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	pretendo-bridge-installer
BUILD		:=	build
SOURCES		:=	source source/builder source/builder/vendor
DATA		:=	data
INCLUDES	:=	source source/builder source/builder/vendor

APP_TITLE	:=	Pretendo Bridge Installer
APP_DESCRIPTION	:=	Badge Arcade Pretendo Bridge installer
APP_AUTHOR	:=	PaulLuisDerwort
APP_PRODUCT_CODE	:=	CTR-H-PBRG
APP_UNIQUE_ID	:=	0xBA9B1

VERSION_MAJOR	:=	1
VERSION_MINOR	:=	1
VERSION_MICRO	:=	0
# Non-empty VERSION_TAG publishes a pre-release (v<version>-<tag>); the FBI QR
# code keeps pointing to the latest full release.
VERSION_TAG	:=	beta4
VERSION		:=	$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)$(if $(VERSION_TAG),-$(VERSION_TAG),)

META		:=	meta
ICON		:=	meta/icon.png

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(VERSION)\"

CXXFLAGS	:= $(CFLAGS) -std=gnu++17 -fexceptions
CFLAGS	+=	-std=gnu99

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lctru -lm

LIBDIRS	:= $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.ips))) gm9_export.gm9

export LD	:=	$(CXX)

export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES		:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON	:=	$(TOPDIR)/$(ICON)
export _3DSXDEPS	:=	$(OUTPUT).smdh
export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean cia check-data

#---------------------------------------------------------------------------------
all: check-data $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

check-data:
	@for f in vanilla_home vanilla_game expanded_home expanded_game weekly_home weekly_game; do \
		[ -f $(DATA)/$$f.ips ] || { echo "Missing $(DATA)/$$f.ips (see .github/scripts/build_installer.sh)"; exit 1; }; \
	done

$(BUILD):
	@mkdir -p $@

#---------------------------------------------------------------------------------
# CIA packaging
#---------------------------------------------------------------------------------
cia: all
	@bannertool makebanner -i $(META)/banner.png -a $(META)/banner.wav -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(META)/icon.png -o $(BUILD)/icon.icn
	@makerom -f cia -o $(TARGET).cia -target t -exefslogo -elf $(TARGET).elf -rsf $(META)/app.rsf \
		-banner $(BUILD)/banner.bnr -icon $(BUILD)/icon.icn \
		-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" \
		-DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy \
		-DAPP_CATEGORY=Application -DAPP_USE_ON_SD=true -DAPP_MEMORY_TYPE=Application -DAPP_CPU_SPEED=268MHz -DAPP_ENABLE_L2_CACHE=false \
		-DAPP_ENCRYPTED=false -DAPP_VERSION_MAJOR=$(VERSION_MAJOR) \
		-major $(VERSION_MAJOR) -minor $(VERSION_MINOR) -micro $(VERSION_MICRO)
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# the embedded patches
#---------------------------------------------------------------------------------
%.ips.o	%_ips.h :	%.ips
	@echo $(notdir $<)
	@$(bin2o)

# the GodMode9 export script the app puts on the SD card
gm9_export.gm9.o	gm9_export_gm9.h :	$(TOPDIR)/gm9/pretendo-bridge-export.gm9
	@echo $(notdir $<)
	@cp $< gm9_export.gm9
	@bin2s -a 4 -H gm9_export_gm9.h gm9_export.gm9 | $(AS) -o gm9_export.gm9.o

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
