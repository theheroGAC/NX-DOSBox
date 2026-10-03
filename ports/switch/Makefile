DEVKITPRO ?= C:/devkitPro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64

PREFIX := aarch64-none-elf-
BINDIR := $(DEVKITA64)/bin
LIBNX  := $(DEVKITPRO)/libnx
TOOLS  := $(DEVKITPRO)/tools

CXX := $(BINDIR)/$(PREFIX)g++
AR  := $(BINDIR)/$(PREFIX)ar

BUILD  := build
OUT    := out
TARGET := NX-DOSBox

TOP := ../..

CORE_EXCLUDE := \
	$(TOP)/src/gui/sdlmain.cpp \
	$(TOP)/src/gui/sdl_mapper.cpp \
	$(TOP)/src/gui/sdl_gui.cpp \
	$(TOP)/src/dos/cdrom_ioctl_linux.cpp \
	$(TOP)/src/dos/cdrom_ioctl_win32.cpp \
	$(TOP)/src/dos/cdrom_ioctl_os2.cpp \
	$(TOP)/src/dos/cdrom_aspi_win32.cpp \
	$(TOP)/src/hardware/serialport/directserial.cpp

CORE_DIRS := cpu debug dos fpu gui hardware hardware/mame hardware/serialport \
            ints misc shell

CORE_SOURCES := $(filter-out $(CORE_EXCLUDE), \
	$(wildcard $(TOP)/src/*.cpp) \
	$(foreach dir,$(CORE_DIRS),$(wildcard $(TOP)/src/$(dir)/*.cpp)))

PORT_SOURCES := ../common/console_mapper.cpp \
                ../common/console_host.cpp \
                ../common/mainline.cpp \
                ../common/osd_draw.cpp \
                ../common/osd_menu.cpp \
                ../common/osd_keyboard.cpp \
                ../common/pad_mapping.cpp \
                main.cpp \
                switch_platform.cpp \
                switch_ftp.cpp

SOURCES := $(CORE_SOURCES) $(PORT_SOURCES)

.DEFAULT_GOAL := all

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

WARN := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
        -Wno-unused-variable -Wno-unused-but-set-variable \
        -Wno-missing-field-initializers -Wno-misleading-indentation

DEFINES := -DDOSBOX_PORT_SWITCH=1

INCLUDES := -I. -I../common -I$(TOP)/include -I$(TOP)/src/gui -I$(TOP)/src/ints -I$(TOP)/src/dos -I$(LIBNX)/include

CXXFLAGS := -g -O2 $(WARN) $(ARCH) $(DEFINES) $(INCLUDES) \
            -fno-strict-aliasing -fomit-frame-pointer \
            -std=gnu++17 -fno-threadsafe-statics

LDFLAGS  := -specs=$(LIBNX)/switch.specs $(ARCH) -Wl,-Map,$(OUT)/$(TARGET).map
LIBPATHS := -L$(LIBNX)/lib
LIBS     := -lnx

ifeq ($(V),1)
Q :=
else
Q := @
endif

export LD      := $(CXX)
export OUTPUT  := $(CURDIR)/$(OUT)
export DEPSDIR := $(CURDIR)/$(BUILD)

export DEVKITPRO
export DEVKITA64

export TMP     := $(CURDIR)/$(BUILD)/tmp
export TEMP    := $(CURDIR)/$(BUILD)/tmp
export TMPDIR  := $(CURDIR)/$(BUILD)/tmp

SCRATCH := $(BUILD)/tmp

define compile_rule
$(BUILD)/$(subst $(TOP)/,src/,$(1)).o: $(1) | $(SCRATCH)
	@mkdir -p $$(dir $$@)
	@echo $$(notdir $$<)
	@$(CXX) $$(CXXFLAGS) -MMD -MP -MF $$(@:.o=.d) -c $$< -o $$@
endef
$(foreach src,$(SOURCES),$(eval $(call compile_rule,$(src))))

OFILES   := $(foreach src,$(SOURCES),$(BUILD)/$(subst $(TOP)/,src/,$(src)).o)
DEPFILES := $(OFILES:.o=.d)

all: $(OUT)/$(TARGET).nro

$(SCRATCH):
	$(Q)mkdir -p $@

APP_TITLE  := NX-DOSBox
APP_AUTHOR := TheheroGAC
APP_VERSION := 1.00
APP_ICON   := $(CURDIR)/icon.jpg

$(OUT)/$(TARGET).nro: $(OUT)/$(TARGET).elf $(OUT)/$(TARGET).nacp
	$(Q)echo "nro  $@"
	$(Q)$(TOOLS)/bin/elf2nro $< $@ \
		--nacp=$(OUT)/$(TARGET).nacp --icon=$(APP_ICON) \
		$(if $(wildcard romfs),--romfsdir=romfs,)

$(OUT)/$(TARGET).elf: $(OFILES) | $(SCRATCH)
	$(Q)mkdir -p $(OUT)
	$(Q)echo "elf  $@"
	$(Q)$(CXX) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OUT)/$(TARGET).nacp: Makefile
	$(Q)mkdir -p $(OUT)
	$(Q)$(TOOLS)/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

clean:
	$(Q)echo "clean"
	$(Q)rm -rf $(BUILD) $(OUT) build.ninja

.PHONY: ninja-vars
ninja-vars:
	@echo "CXX='$(CXX)'"
	@echo "LDFLAGS='$(LDFLAGS)'"
	@echo "LIBPATHS='$(LIBPATHS)'"
	@echo "LIBS='$(LIBS)'"
	@echo "CXXFLAGS='$(CXXFLAGS)'"
	@echo "NACPTOOL='$(TOOLS)/bin/nacptool'"
	@echo "ELF2NRO='$(TOOLS)/bin/elf2nro'"
	@echo "APP_TITLE='$(APP_TITLE)'"
	@echo "APP_AUTHOR='$(APP_AUTHOR)'"
	@echo "APP_VERSION='$(APP_VERSION)'"
	@echo "APP_ICON='$(APP_ICON)'"
	@echo "NROFLAGS='$(if $(wildcard romfs),--romfsdir=romfs,)'"
	@echo "TARGET='$(TARGET)'"
	@echo "TOP='$(TOP)'"
	@echo "OUT_DIR='$(CURDIR)/$(OUT)'"
	@echo "OUT_REL='$(OUT)'"
	@echo "SOURCES='$(SOURCES)'"

-include $(DEPFILES)
