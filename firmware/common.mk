# Gemeinsame Build-Einstellungen fuer alle Firmwares unter firmware/<name>/.
#
# Verwendung am Ende eines Projekt-Makefiles:
#
#   TARGET      = MeineFirmware
#   CPP_SOURCES = main.cpp
#   include ../common.mk
#
# Optionale Schalter (vor dem include setzen):
#   USE_DAISYSP_LGPL = 1      DaisySP-LGPL-Module (ReverbSc, Moogladder, ...) linken
#   USE_STMLIB       = 1      stmlib-Header verfuegbar machen (#include "stmlib/dsp/units.h")
#   CC_SOURCES      += ...    .cc-Dateien mitbauen (z. B. $(STMLIB_DIR)/dsp/units.cc)
#   APP_TYPE         = BOOT_SRAM | BOOT_QSPI   fuer Firmwares > 128 KB (Daisy-Bootloader)
#   OPT              = -Os    Optimierung (Default von libDaisy: -O2)
#
# Alle Pfade bleiben relativ, weil der Repo-Pfad Klammern enthaelt
# ("patch_init()_firmwares") und make mit Klammern in absoluten Pfaden
# nicht zuverlaessig umgehen kann.

FIRMWARE_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(FIRMWARE_DIR)/..

LIBDAISY_DIR ?= $(REPO_ROOT)/lib/libDaisy
DAISYSP_DIR  ?= $(REPO_ROOT)/lib/DaisySP
STMLIB_DIR   ?= $(REPO_ROOT)/lib/stmlib

# Projektordner selbst und firmware/shared/ fuer gemeinsamen Code
C_INCLUDES += -I. -I$(FIRMWARE_DIR)/shared

ifeq ($(USE_STMLIB),1)
# stmlib-Header inkludieren sich gegenseitig als "stmlib/...", daher lib/ als Include-Wurzel
C_INCLUDES += -I$(REPO_ROOT)/lib
# stmlib nutzt STATIC_ASSERT-Makros, die sonst Warnungen erzeugen
C_INCLUDES += -Wno-unused-local-typedefs
endif

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# libDaisy kennt nur .c/.cpp/.s - hier kommt Unterstuetzung fuer .cc dazu (stmlib, Mutable-Instruments-Code)
ifneq ($(strip $(CC_SOURCES)),)
CC_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))
OBJECTS += $(CC_OBJECTS)

$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@
endif
