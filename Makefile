#---------------------------------------------------------------------------------
# SlimeVR tracker for the Nintendo 3DS family
#   make          -> .3dsx, .cia and .3ds in dist/
#   make 3dsx     -> homebrew launcher build only
#
# Requires devkitARM + libctru + citro2d/citro3d (devkitPro 3ds-dev),
# and makerom / bannertool in PATH (or MAKEROM=... BANNERTOOL=...).
#---------------------------------------------------------------------------------
DEVKITPRO  ?= /opt/devkitpro
DEVKITARM  ?= $(DEVKITPRO)/devkitARM
CTRULIB    ?= $(DEVKITPRO)/libctru
TOOLS      ?= $(DEVKITPRO)/tools/bin

MAKEROM    ?= makerom
BANNERTOOL ?= bannertool
SMDHTOOL   ?= $(TOOLS)/smdhtool
3DSXTOOL   ?= $(TOOLS)/3dsxtool

TARGET      := SlimeVR-3DS
APP_TITLE   := SlimeVR 3DS
APP_DESC    := SlimeVR tracker for the 3DS family
APP_AUTHOR  := Homebrew

BUILD   := build
DIST    := dist
SOURCES := source
META    := meta

PREFIX  := $(DEVKITARM)/bin/arm-none-eabi-
CC      := $(PREFIX)gcc

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 -mword-relocations \
           -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -I$(CTRULIB)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map \
           -L$(CTRULIB)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRCS := $(wildcard $(SOURCES)/*.c)
OBJS := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

ELF  := $(BUILD)/$(TARGET).elf

.PHONY: all 3dsx cia cci clean

all: 3dsx cia cci
3dsx: $(DIST)/$(TARGET).3dsx
cia:  $(DIST)/$(TARGET).cia
cci:  $(DIST)/$(TARGET).3ds

$(BUILD) $(DIST):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@echo "CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(ELF): $(OBJS)
	@echo "LD  $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD)/$(TARGET).smdh: $(META)/icon.png | $(BUILD)
	@$(SMDHTOOL) --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $< $@

$(DIST)/$(TARGET).3dsx: $(ELF) $(BUILD)/$(TARGET).smdh | $(DIST)
	@echo "3DSX $@"
	@$(3DSXTOOL) $(ELF) $@ --smdh=$(BUILD)/$(TARGET).smdh

$(BUILD)/icon.icn: $(META)/icon.png | $(BUILD)
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTHOR)" -i $< -o $@ >/dev/null

$(BUILD)/banner.bnr: $(META)/banner.png $(META)/banner.wav | $(BUILD)
	@$(BANNERTOOL) makebanner -i $(META)/banner.png -a $(META)/banner.wav -o $@ >/dev/null

$(DIST)/$(TARGET).cia: $(ELF) $(BUILD)/icon.icn $(BUILD)/banner.bnr $(META)/app.rsf | $(DIST)
	@echo "CIA $@"
	@$(MAKEROM) -f cia -o $@ -elf $(ELF) -rsf $(META)/app.rsf -icon $(BUILD)/icon.icn \
		-banner $(BUILD)/banner.bnr -exefslogo -target t

$(DIST)/$(TARGET).3ds: $(ELF) $(BUILD)/icon.icn $(BUILD)/banner.bnr $(META)/app.rsf | $(DIST)
	@echo "3DS $@"
	@$(MAKEROM) -f cci -o $@ -elf $(ELF) -rsf $(META)/app.rsf -icon $(BUILD)/icon.icn \
		-banner $(BUILD)/banner.bnr -exefslogo -target d

clean:
	@rm -rf $(BUILD) $(DIST)

-include $(DEPS)
