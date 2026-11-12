# Mutable Instruments Rings fuer den patch.init() - siehe README.md
TARGET = rings

CPP_SOURCES = main.cpp ui.cpp

# Rings-DSP (Kopie aus ref/eurorack/rings) + benoetigte stmlib-Quellen
CC_SOURCES = \
	rings/dsp/fm_voice.cc \
	rings/dsp/part.cc \
	rings/dsp/resonator.cc \
	rings/dsp/string.cc \
	rings/dsp/string_synth_part.cc \
	rings/resources.cc \
	$(STMLIB_DIR)/dsp/units.cc \
	$(STMLIB_DIR)/utils/random.cc

USE_STMLIB = 1

# Laeuft wie Beads aus dem SRAM ueber den Daisy-Bootloader. So muss beim
# Wechsel zwischen beiden Firmwares der Bootloader nicht neu geflasht werden.
APP_TYPE = BOOT_SRAM

# Das Original wird mit -O2 -funroll-loops gebaut (stmlib/makefile.inc)
OPT = -O2 -funroll-loops

include ../common.mk
