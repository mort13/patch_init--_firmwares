# patch.init() Firmwares

Eigene Firmwares für das Electrosmith **patch.init()** Eurorack-Modul (Daisy Patch Submodule, STM32H750).

## Struktur

```
lib/                      Libraries, gegen die gebaut wird (Git-Submodule)
  libDaisy/               Hardware-Abstraktion, v9.0.0
  DaisySP/                DSP-Bausteine (+ DaisySP-LGPL)
  stmlib/                 Mutable-Instruments-Hilfsbibliothek (pichenettes/stmlib)
ref/                      Nur zum Lesen/Nachvollziehen (flache Submodule)
  DaisyExamples/          offizielle Beispiele, u. a. ref/DaisyExamples/patch_sm/
firmware/
  common.mk               gemeinsame Build-Einstellungen
  shared/patch_init.h     Pinbelegung des patch.init() mit sprechenden Namen
  template/               Startpunkt für neue Firmwares
scripts/setup.sh          Submodule initialisieren + Libraries bauen
```

Die Versionen in `lib/` sind auf genau die Commits gepinnt, die `ref/DaisyExamples` verwendet.
So lassen sich alle Beispiele unverändert gegen unsere Libraries bauen.

## Voraussetzungen

- [DaisyToolchain](https://github.com/daisyaudio/DaisyToolchain) (arm-none-eabi-gcc, make, dfu-util, openocd)
- Git for Windows – **alle `make`-Befehle in Git Bash ausführen**, die libDaisy-Makefiles brauchen `sh`, `rm`, `mkdir`

## Erstes Setup (nach frischem Clone)

```bash
git clone <repo-url>
cd <repo>
./scripts/setup.sh
```

Nicht `git clone --recursive` verwenden: Das lädt die Submodule von DaisyExamples ein zweites Mal herunter.

## Bauen und Flashen

```bash
make template                       # eine Firmware bauen (Libraries werden bei Bedarf mitgebaut)
make                                # alle Firmwares unter firmware/ bauen
cd firmware/template && make program-dfu
```

DFU-Modus am Patch SM: **BOOT** gedrückt halten, **RESET** kurz drücken, BOOT loslassen. Dann `make program-dfu`.
Mit ST-Link V3 geht es auch per `make program` (und Debuggen per `make openocd` + `make debug`).

## Neue Firmware anlegen

```bash
make new NAME=meine_firmware
make meine_firmware
```

Das kopiert `firmware/template/` nach `firmware/meine_firmware/` und setzt `TARGET`.
Optionen im Projekt-Makefile (Details in `firmware/common.mk`):

| Variable | Wirkung |
|---|---|
| `USE_DAISYSP_LGPL = 1` | LGPL-Module aus DaisySP linken (ReverbSc, MoogLadder, …) |
| `USE_STMLIB = 1` | `#include "stmlib/..."` möglich |
| `CC_SOURCES += $(STMLIB_DIR)/dsp/units.cc` | `.cc`-Quellen mitbauen (stmlib, Mutable-Code) |
| `APP_TYPE = BOOT_SRAM` | Firmware > 128 KB, braucht einmalig den Daisy-Bootloader (`make program-boot`) |
| `OPT = -Os` | Optimierung (Default `-O2`) |

## patch.init() Pinbelegung

| Frontplatte | Patch SM | Code |
|---|---|---|
| Knob 1–4 | `CV_1`–`CV_4` | `hw.GetAdcValue(KNOB_1)` → 0..1 |
| CV In 1–4 | `CV_5`–`CV_8` | `hw.GetAdcValue(CV_IN_1)` → -1..1 (±5 V) |
| CV Out | `CV_OUT_1` (C10) | `hw.WriteCvOut(CV_OUT, volt)` 0..5 V |
| LED | `CV_OUT_2` (C1) | `hw.WriteCvOut(LED, volt)` 0..5 V |
| Taster | B7 | `Switch button; button.Init(BUTTON, ...)` |
| Kippschalter | B8 | `Switch toggle; toggle.Init(TOGGLE, ...)` |
| Gate In 1 / 2 | B10 / B9 | `hw.gate_in_1.State()` |
| Gate Out 1 / 2 | B5 / B6 | `hw.gate_out_1.Write(bool)` |
| Audio In/Out L/R | – | `IN_L`, `IN_R`, `OUT_L`, `OUT_R` im Audio-Callback |

Die Namen (`KNOB_1`, `CV_IN_1`, `LED`, …) kommen aus `firmware/shared/patch_init.h`.

## Referenz-Beispiele bauen

Die Beispiele in `ref/DaisyExamples` erwarten `../../libDaisy`. Statt deren Submodule zu laden,
werden die Pfade beim Aufruf auf `lib/` umgebogen:

```bash
make -C ref/DaisyExamples/patch_sm/ReverbExample LIBDAISY_DIR=../../../../lib/libDaisy DAISYSP_DIR=../../../../lib/DaisySP
```

Weitere Referenz-Repos (z. B. `pichenettes/eurorack` mit den Original-Quellen der Mutable-Instruments-Module)
flach unter `ref/` einhängen:

```bash
git submodule add --depth 1 https://github.com/pichenettes/eurorack ref/eurorack
git config -f .gitmodules submodule.ref/eurorack.shallow true
```

## Libraries aktualisieren

```bash
git -C lib/libDaisy fetch --tags && git -C lib/libDaisy checkout v9.x.y
git -C lib/libDaisy submodule update --init --recursive --depth 1
make clean-libs libs
git add lib/libDaisy && git commit -m "libDaisy auf v9.x.y"
```

Sinnvoll ist, `lib/` im Gleichschritt mit `ref/DaisyExamples` zu halten (dessen gepinnte Commits zeigen,
welche Kombination upstream getestet ist): `git -C ref/DaisyExamples ls-tree HEAD libDaisy DaisySP stmlib`.
