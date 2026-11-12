# Beads für patch.init()

Port des Granular-Prozessors **Mutable Instruments Beads** (Emilie Gillet, MIT-Lizenz) auf das Electrosmith patch.init().
Der DSP-Kern ist bis auf drei kleine Patches unverändert. Die Bedienung ist neu, weil das patch.init() statt 12 Potis und 4 Tastern nur 4 Knobs, einen Taster und einen Kippschalter hat.

## Bauen und Flashen

Die Firmware ist größer als die 128 KB des internen Flashs und läuft deshalb aus dem SRAM (`APP_TYPE = BOOT_SRAM`).
Dafür braucht der Patch SM **einmalig** den Daisy-Bootloader.

```bash
make beads                                   # im Repo-Root, in Git Bash
cd firmware/beads && make program-boot       # einmalig: Bootloader flashen (DFU-Modus: BOOT halten, RESET drücken)
cd firmware/beads && make program-dfu        # Firmware flashen
```

Zum Flashen der Firmware über den Bootloader: RESET drücken und innerhalb von ca. 2 s BOOT drücken, solange die LED auf dem Patch SM pulsiert.
Danach `make program-dfu` aufrufen.

## Belegung

| Frontplatte | Funktion |
|---|---|
| Audio In L/R | Eingang (ist nur ein Kanal belegt, bekommt der andere dasselbe Signal) |
| Audio Out L/R | Ausgang |
| CV 1 | TIME |
| CV 2 | SIZE (kann den Delay-Modus nicht auslösen, das kann nur der Knob) |
| CV 3 | PITCH, 1 V/Oct (±5 V = ±5 Oktaven) |
| CV 4 | DENSITY |
| Gate In 1 | FREEZE (solange High, zusätzlich zum Taster) |
| Gate In 2 | SEED / Clock |
| Gate Out 1 | Grain-Gate: jede steigende Flanke ist ein neues Grain |
| CV Out | neue Zufallsspannung 0–5 V bei jedem Grain (Sample & Hold) |
| Gate Out 2 | Freeze-Status |
| LED | an = Freeze, sonst kurzer Blitz pro Grain, außerdem Blinkcodes |

### Knobs

| | Kippschalter unten (Seite A) | Kippschalter oben (Seite B) | Taster halten + Knob drehen |
|---|---|---|---|
| Knob 1 | TIME | FEEDBACK | TIME-Random |
| Knob 2 | SIZE | DRY/WET | SIZE-Random |
| Knob 3 | PITCH | REVERB | PITCH-Random |
| Knob 4 | DENSITY | SHAPE | SHAPE-Random |

- **Kein Wert springt.** Nach einem Ebenenwechsel holt der Knob den gespeicherten Wert relativ ein, bis sich beide treffen. Das Verfahren stammt aus dem Original.
- **Random-Ebene:** entspricht den Attenuvertern des Originals bei ungepatchtem CV-Eingang. Mitte = keine Zufallsmodulation, rechts = gleichmäßige Zufallswerte bzw. schnelle Zufallsbewegung, links = gelegentliche Ausreißer bzw. langsame Zufallsbewegung.
- **SIZE-Random im Delay-Modus:** regelt die Filterbandbreite.
- **SIZE am rechten Anschlag:** Beads wechselt mit 1,5 s Überblendung in den Delay-/Tape-Modus. TIME, DENSITY und SHAPE wirken dann wie im Delay-Modus des Originals (siehe Beads-Handbuch). Eine Clock an Gate In 2 synchronisiert das Delay.
- **PITCH:** rastet wie im Original auf 0, 1, 3, 4, 7, 12 und 24 Halbtöne ein, in beide Richtungen.
- **DENSITY:** Mitte = keine Grains. Links davon regelmäßige Grains, die nach links schneller werden. Rechts davon zufällige Grains, die nach rechts dichter werden.
- **DENSITY mit Clock an Gate In 2:** links = Teiler (ganz links jeder Clock-Puls, zur Mitte hin bis 1/16), Mitte = keine Grains, rechts = Wahrscheinlichkeit (rechter Anschlag = jeder Puls).

### Taster

| Geste | Wirkung |
|---|---|
| kurz drücken (ohne Knob zu bewegen) | FREEZE an/aus |
| halten + Knob drehen | Random-Ebene (siehe oben) |
| 2 s halten (ohne Knob zu bewegen) | nächste Quality-Stufe |
| beim Einschalten halten | V/Oct-Kalibrierung |

Ein Quality-Wechsel löscht den Puffer und wartet deshalb, solange Freeze aktiv ist (wie im Original).

### Quality-Stufen

| LED blinkt | Stufe | Abtastrate | Format | Pufferlänge |
|---|---|---|---|---|
| 1× | Digital (Werkseinstellung) | 48 kHz | 16 Bit | 4 s |
| 2× | Vintage Digital | 32 kHz | 12 Bit | 8 s |
| 3× | Clean Tape | 24 kHz intern | 12 Bit, dynamisches Soft-Clipping | 10,7 s |
| 4× | Dirty Tape | 24 kHz intern | 8 Bit µ-law, Wow & Flutter | 16 s |

Beim Einschalten zeigt die LED die aktuelle Stufe an.

### V/Oct-Kalibrierung (CV 3)

1. Taster beim Einschalten gedrückt halten, dann loslassen. Die LED blinkt langsam.
2. Genau **1 V** an CV 3 anlegen und den Taster drücken. Die LED leuchtet 0,5 s und blinkt danach schnell.
3. Genau **3 V** an CV 3 anlegen und den Taster drücken. Die LED leuchtet 0,5 s.
4. Einmal langes Leuchten = gespeichert. Flackern = Messwert unplausibel (über 10 % Abweichung), die alte Kalibrierung bleibt.

Ohne Kalibrierung gilt der Nennwert: ±5 V = ±60 Halbtöne.

### Was gespeichert wird

Quality-Stufe, alle Werte von Seite B, die Random-Ebene und die V/Oct-Kalibrierung.
Gespeichert wird im QSPI-Flash, 2 s nach der letzten Änderung. Seite A folgt immer der physischen Knob-Position.

## Unterschiede zum Original

Beads erkennt per Probe-Signal, welche Buchsen gesteckt sind. Das patch.init() kann das nicht, deshalb:

- **CV-Eingänge immer aktiv.** Sie werden fest addiert (±5 V = voller Knob-Weg). Die Random-Ebene regelt immer die interne Zufallsmodulation. Die CV-Tiefe ist nicht einstellbar, und es gibt keine CV-gesteuerte Zufallsmodulation (negativer Attenuverter bei gepatchter CV).
- **Seed-Eingang:** gilt als gesteckt, solange in den letzten 4 s (bzw. 3 × letztes Intervall) eine Flanke kam. Sonst erzeugt Beads die Grains selbst. Bleibt die Clock weg, läuft Beads also nach kurzer Zeit intern weiter, statt zu verstummen.
- **Audio-Eingänge:** Ein Kanal gilt als leer, wenn er länger als 1 s unter ca. −66 dBFS liegt. Der Puffer läuft immer im Stereo-Modus; den doppelt so langen Mono-Puffer gibt es nicht.
- **Entfallen** sind:
  - Wavetable-Synth-Modus
  - SEED-Taster mit „gated“-Modus und Taps
  - Assign-CV
  - manuelle Eingangsverstärkung (es gibt nur Auto-Gain)
  - Monophon-Modus
  - Sicherung des Freeze-Puffers beim Ausschalten

## Port-Notizen

- **Upstream:** `ref/eurorack` (pichenettes/eurorack) @ `f4cfa76`.
- **Kopie unter `beads/`:** `dsp/` komplett, `resources.{h,cc}` und `gain_control.{h,cc}`. Die Kopie liegt hier, damit `ref/` unverändert bleibt.
- **Patches**, im Code mit `patch.init() port` markiert:
  1. `dsp/processor.h`: `ENABLE_HARDWARE_24K` aus. libDaisy kann keine 24 kHz, deshalb läuft Dirty Tape mit 48 kHz und interner 2:1-Konvertierung. Diesen Pfad gibt es schon im Original.
  2. `dsp/processor.{h,cc}`: `gate()` gibt den Grain-/Tap-Gate-Zustand des letzten Blocks heraus (für Gate Out 1 und CV Out).
  3. `gain_control.{h,cc}`: Float-Eingang statt der 32-Bit-Codec-Frames.
- **Ersatz für `beads.cc`:**
  - `main.cpp`: Audio-Callback mit 4 kHz Blockrate, 12 bzw. 8 Frames.
  - `ui.cpp`: Bedienung, Knob-Kennlinien aus `cv_reader.h`.
  - `firmware/shared/layered_knob.h`: Catch-up aus `pot_controller.h` (geteilt mit Rings).
  - `input_detect.h`: Ersatz für den `NormalizationDetector`, mit `firmware/shared/clock_detector.h` für den Seed-Eingang.
  - `settings.h`: QSPI-Einstellungen.
- **Speicher:**
  - Die Delay-Bänke (3 × 256 KiB) und der Reverb-Puffer (64 KiB) liegen im SDRAM; im Original liegen sie im internen SRAM.
  - Code: ca. 175 KB im SRAM. `.bss`/Stack: ca. 42 KB im DTCM.
- **CPU-Last messen:** `#define BEADS_DEBUG_CPU` in `main.cpp` aktivieren und ein serielles Terminal auf den USB-Port des Daisy öffnen. Worst Case: Digital, DENSITY rechts, kleine SIZE, PITCH-Random voll, REVERB und FEEDBACK hoch.
- **Falls die CPU-Last zu hoch ist:**
  1. `reverb_buffer` mit `DTCM_MEM_SECTION` ins DTCM legen (Platz ist da).
  2. Ein eigenes Linker-Script mit einer Sektion in RAM_D2 für eine Delay-Bank (`LDSCRIPT` ist im libDaisy-Makefile überschreibbar).
