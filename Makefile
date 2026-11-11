# Komfort-Targets fuer das ganze Repo (in Git Bash ausfuehren).
#
#   make libs                  libDaisy + DaisySP (inkl. LGPL) bauen - einmalig bzw. nach Lib-Update
#   make                       Libraries + alle Firmwares unter firmware/ bauen
#   make <name>                nur firmware/<name> bauen
#   make new NAME=<name>       neue Firmware aus firmware/template anlegen
#   make clean                 Build-Ordner aller Firmwares loeschen
#   make clean-libs            Build-Ordner der Libraries loeschen
#
# Flashen: cd firmware/<name> && make program-dfu   (Patch SM vorher per BOOT+RESET in den DFU-Modus)

FIRMWARES := $(patsubst firmware/%/Makefile,%,$(wildcard firmware/*/Makefile))

.PHONY: all libs libdaisy daisysp firmwares new clean clean-libs $(FIRMWARES)

all: firmwares

libs: libdaisy daisysp

libdaisy:
	"$(MAKE)" -C lib/libDaisy

daisysp:
	"$(MAKE)" -C lib/DaisySP

firmwares: $(FIRMWARES)

$(FIRMWARES): libs
	"$(MAKE)" -C firmware/$@

new:
ifndef NAME
	$(error Bitte NAME angeben: make new NAME=meine_firmware)
endif
	@if [ -e firmware/$(NAME) ]; then echo "firmware/$(NAME) existiert bereits"; exit 1; fi
	mkdir -p firmware/$(NAME)
	cp firmware/template/main.cpp firmware/$(NAME)/main.cpp
	sed 's/^TARGET = .*/TARGET = $(NAME)/' firmware/template/Makefile > firmware/$(NAME)/Makefile
	@echo "Angelegt: firmware/$(NAME) - bauen mit: make $(NAME)"

clean:
	@for d in $(FIRMWARES); do "$(MAKE)" -C firmware/$$d clean; done

clean-libs:
	"$(MAKE)" -C lib/libDaisy clean
	"$(MAKE)" -C lib/DaisySP clean
