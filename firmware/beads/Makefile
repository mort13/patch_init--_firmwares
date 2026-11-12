# Mutable Instruments Beads fuer den patch.init() - siehe README.md
TARGET = beads

CPP_SOURCES = main.cpp ui.cpp

# Beads-DSP (Kopie aus ref/eurorack/beads) + benoetigte stmlib-Quellen
CC_SOURCES = \
	beads/dsp/processor.cc \
	beads/dsp/mu_law.cc \
	beads/dsp/delay_clock.cc \
	beads/dsp/delay_controller.cc \
	beads/dsp/trigger_generator.cc \
	beads/resources.cc \
	beads/gain_control.cc \
	$(STMLIB_DIR)/dsp/units.cc \
	$(STMLIB_DIR)/utils/random.cc

USE_STMLIB = 1

# Code + Tabellen sind groesser als die 128 KB internes Flash:
# laeuft aus dem SRAM, braucht einmalig den Daisy-Bootloader (make program-boot)
APP_TYPE = BOOT_SRAM

# Das Original wird mit -O2 -funroll-loops gebaut (stmlib/makefile.inc)
OPT = -O2 -funroll-loops

include ../common.mk
