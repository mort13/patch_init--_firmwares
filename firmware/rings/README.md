# Rings für patch.init()

Port des Resonators **Mutable Instruments Rings** (Emilie Gillet, MIT-Lizenz) auf das Electrosmith patch.init().
Der DSP-Kern ist bis auf einen kleinen Patch für DroidLink unverändert, inklusive des versteckten Synth-Modus *Disastrous Peace*.
Über [DroidLink](../../docs/droid_link.md) spielt Rings die 4 MIDI-Stimmen vom DROID polyphon, mit Velocity.
Die Bedienung ist neu, weil das patch.init() statt 10 Potis und 2 Tastern nur 4 Knobs, einen Taster und einen Kippschalter hat.

## Bauen und Flashen

Wie Beads läuft die Firmware aus dem SRAM (`APP_TYPE = BOOT_SRAM`) und braucht **einmalig** den Daisy-Bootloader.
Ist Beads schon drauf, ist auch der Bootloader schon da, und man kann zwischen beiden Firmwares wechseln, ohne ihn neu zu flashen.

```bash
make rings                                   # im Repo-Root, in Git Bash
cd firmware/rings && make program-boot       # nur falls noch kein Bootloader drauf ist (DFU-Modus: BOOT halten, RESET drücken)
cd firmware/rings && make program-dfu        # Firmware flashen
```

Zum Flashen über den Bootloader: RESET drücken und innerhalb von ca. 2 s BOOT drücken, solange die LED auf dem Patch SM pulsiert.
Danach `make program-dfu` aufrufen.

## Belegung

| Frontplatte | Funktion |
|---|---|
| Audio In L/R | IN (Exciter). Beide Kanäle werden addiert, es reicht also eine der beiden Buchsen |
| Audio Out L | ODD |
| Audio Out R | EVEN |
| CV 1 | V/OCT, 1 V/Oct (±5 V = ±5 Oktaven), kalibrierbar |
| CV 2 | STRUCTURE |
| CV 3 | BRIGHTNESS |
| CV 4 | DAMPING (mit DroidLink: Datenleitung) |
| Gate In 1 | STRUM |
| Gate In 2 | DroidLink-Takt, sonst ohne Funktion |
| Gate Out 1 | Strum-Trigger (5 ms) bei jedem Anschlag, auch bei internen Anschlägen und beim Taster |
| Gate Out 2 | High, solange am Audio-Eingang ein Signal erkannt ist (externer Exciter aktiv) |
| CV Out | Ausgangspegel: 0 V = −60 dBFS, 5 V = 0 dBFS |
| LED | Ausgangspegel (gedimmt), Blitz bei jedem Strum, Blinkcodes |

Die CV-Eingänge werden fest zum Knob addiert, ±5 V entsprechen dem vollen Knob-Weg.

### Knobs

| | Kippschalter unten (Seite A) | Kippschalter oben (Seite B) |
|---|---|---|
| Knob 1 | FREQUENCY | POSITION |
| Knob 2 | STRUCTURE | MODEL |
| Knob 3 | BRIGHTNESS | POLYPHONY |
| Knob 4 | DAMPING | BREITE (Stereo) |

- **Kein Wert springt.** Nach einem Seitenwechsel holt der Knob den gespeicherten Wert relativ ein, bis sich beide treffen (wie im Beads-Port).
- **FREQUENCY:** 5 Oktaven ab C0. Mit Kabel an CV 1 rastet der Knob auf Halbtöne ein (wie im Original), ohne Kabel ist er stufenlos.
- **MODEL** (6 Zonen, LED blinkt 1–6× beim Wechsel):

  | Zone | Rings | Disastrous Peace |
  |---|---|---|
  | 1 (links) | Modal-Resonator | Formantfilter |
  | 2 | Sympathetische Saiten | Chorus |
  | 3 | Saite (moduliert/inharmonisch) | Reverb |
  | 4 | FM-Stimme | Formantfilter 2 |
  | 5 | Sympathetische Saiten, quantisiert (Akkorde) | Ensemble |
  | 6 (rechts) | Saite + Reverb | Reverb 2 |

  Die Zonen 4–6 sind im Original die per langem Tastendruck erreichbaren Zusatzmodelle.
- **POLYPHONY** (4 Zonen, LED blinkt 1–4×): 1, 2, 3 oder 4 Stimmen. Jeder Strum geht an die nächste Stimme, die vorige klingt aus.
- **BREITE:** rechter Anschlag = ODD links, EVEN rechts wie im Original. Linker Anschlag = beide Ausgänge bekommen die Summe, also **Mono**.
  Bei mehr als einer Stimme liegen die Stimmen abwechselnd auf ODD und EVEN. Wer nur einen Ausgang benutzt, sollte BREITE deshalb nach links drehen.

### Taster

| Geste | Wirkung |
|---|---|
| drücken | Strum (Anschlag von Hand) |
| 3 s halten | Engine wechseln: Rings ↔ Disastrous Peace (LED 1× lang = Rings, 2× lang = Disastrous Peace) |
| beim Einschalten halten | V/Oct-Kalibrierung |

Beim Einschalten zeigt die LED die aktive Engine an. 3× kurz = DroidLink verbunden.

### Strum, Exciter und Tonhöhe ohne Kabel

Rings verhält sich anders, je nachdem welche Buchsen gesteckt sind. Das wird hier aus den Signalen geschätzt (siehe unten):

- **STRUM leer:** Rings schlägt selbst an, bei jedem Notenwechsel an CV 1. Ist CV 1 auch leer, bei jeder Transiente am Audio-Eingang.
- **IN leer:** Jeder Strum regt die Resonatoren mit dem internen Exciter an (Impuls, gezupfte Saite). Mit Audio-Signal am Eingang regt nur dieses an; ein Strum wählt dann nur die nächste Stimme.
- **Nur der Taster:** genügt zum Ausprobieren. Ohne Kabel schlägt jeder Druck die aktuelle Stimme mit dem internen Exciter an.

### Disastrous Peace

Der versteckte Polysynth von Rings (im Original: lange drücken mit einer bestimmten Poti-Stellung).
STRUCTURE wählt den Akkord, BRIGHTNESS die Registrierung, DAMPING die Hüllkurve (rechter Anschlag = Drone), POSITION die Effekt-Stärke.
MODEL wählt den Effekt (Tabelle oben). Der Audio-Eingang hat hier keine Wirkung.

### DroidLink (MIDI vom DROID)

Verkabelung und DROID-Patch wie in [docs/droid_link.md](../../docs/droid_link.md): DROID O7 → **CV 4**, O8 → **Gate In 2**.
Es gibt nichts einzuschalten. Sobald gültige DroidLink-Rahmen ankommen, blinkt die LED 3× kurz und Rings spielt die MIDI-Noten.
Ohne Takt (Kabel gezogen, DROID-Patch gewechselt) ist Rings nach 50 ms wieder im Normalbetrieb.

| Im DroidLink-Betrieb | Wirkung |
|---|---|
| Note-On | schlägt eine Rings-Stimme an: DroidLink-Stimme 1–4 → Rings-Stimme (1–4) modulo POLYPHONY. **Für Akkorde POLYPHONY auf 4.** |
| Velocity | Anschlagstärke der Note (quadratisch, halbe Velocity = −12 dB). Skaliert den internen Exciter bzw. das Audio-Signal am Eingang |
| Note-Off | ohne Wirkung, die Note klingt mit DAMPING aus (wie ein Strum) |
| Pitchbend | folgt, solange die Rings-Stimme nicht von einer neuen Note übernommen wurde |
| FREQUENCY | Oktavlage in 5 Zonen: Mitte = wie gespielt, links −2, rechts +2 Oktaven |
| CV 1 | Transponierung in Halbtönen (gerundet) |
| CV 2, CV 3 | STRUCTURE, BRIGHTNESS wie immer |
| AUX 1 (Modwheel) | wird zu DAMPING addiert, ersetzt CV 4 |
| AUX 2 | wird zu POSITION addiert (nur wenn der DROID-Patch einen zweiten Aux-Slot sendet) |
| STRUM, Taster | schlagen die zuletzt gespielte Stimme mit voller Stärke erneut an |

- Ein Akkord aus 4 Noten steht nach 1,5 ms: Rings schlägt pro Block (0,5 ms) eine Stimme an.
- Die selbsttätigen Anschläge ohne STRUM-Kabel (Notenwechsel an CV 1, Transienten am Eingang) sind aus. Ob der Eingang belegt ist (interner/externer Exciter), wird weiter erkannt.
- Die Grundtöne der sympathetischen Saiten (MODEL Zone 2) beziehen sich auf C der gewählten Oktavlage plus CV 1.
- **Disastrous Peace:** Jedes Note-On schlägt an, aber mit mindestens 5 ms Abstand (sonst bekommt die vorige Stimme die falsche Tonhöhe). Ein Akkord ist so nach 15 ms komplett. Velocity wirkt hier nicht.

### V/Oct-Kalibrierung (CV 1)

1. Taster beim Einschalten gedrückt halten, dann loslassen. Die LED blinkt langsam.
2. Genau **1 V** an CV 1 anlegen und den Taster drücken. Die LED leuchtet 0,5 s und blinkt danach schnell.
3. Genau **3 V** an CV 1 anlegen und den Taster drücken. Die LED leuchtet 0,5 s.
4. Einmal langes Leuchten = gespeichert. Flackern = Messwert unplausibel (über 10 % Abweichung), die alte Kalibrierung bleibt.

Ohne Kalibrierung gilt der Nennwert: ±5 V = ±60 Halbtöne.

### Was gespeichert wird

Alle Werte von Seite B, die Engine und die V/Oct-Kalibrierung.
Gespeichert wird im QSPI-Flash (Offset `0x7E0000`, Beads liegt bei `0x7F0000`), 2 s nach der letzten Änderung. Seite A folgt immer der physischen Knob-Position.

## Unterschiede zum Original

Rings erkennt per Probe-Signal an den Schaltkontakten, ob STRUM, V/OCT und IN gesteckt sind. Das patch.init() kann das nicht, deshalb:

- **STRUM** gilt als gesteckt, solange in den letzten 4 s (bzw. 3 × letztes Intervall) eine Flanke an Gate In 1 kam.
- **V/OCT** gilt als gesteckt, solange CV 1 in den letzten 4 s mindestens einmal über ±50 mV lag.
  Ein Sequenzer, der lange genau 0 V ausgibt, zählt also nach 4 s als „nicht gesteckt“. Die Tonhöhe ändert sich dadurch nicht:
  FREQUENCY bleibt auf Halbtönen eingerastet, bis man den Knob bewegt. Im Original wird die Note ohne Kabel auf 0 gesetzt, hier bleibt sie immer der Messwert.
- **IN** gilt als gesteckt, sobald der Eingang über ca. −54 dBFS ausschlägt, und wieder als leer, wenn er 2 s unter ca. −66 dBFS bleibt.
  Ein gestecktes, aber stummes Kabel schaltet also auf den internen Exciter, und ein Strum zupft dann selbst an.
- **Ausgänge:** Im Original führt ODD die Summe, wenn EVEN nicht gesteckt ist. Hier übernimmt das der Knob BREITE.
- **Entfallen** sind:
  - FM-Eingang (FREQUENCY-CV) mit seinem Attenuverter
  - CV für POSITION
  - die fünf Attenuverter (CV wirkt immer voll)
  - Farbenblind-Modus der LEDs, Werkstest über die serielle Schnittstelle, Kalibrierung der Normalisierungs-Probe

## Port-Notizen

- **Upstream:** `ref/eurorack` (pichenettes/eurorack) @ `f4cfa76`.
- **Kopie unter `rings/`:** `dsp/` komplett, `resources.{h,cc}`.
- **Patches**, im Code mit `patch.init() port` markiert:
  1. `dsp/part.{h,cc}`: `set_direct_control()` für DroidLink. Umgeht NoteFilter und Round-Robin, setzt die Note jeder Stimme direkt und schlägt eine bestimmte Stimme an.
     Dazu eine Anschlagstärke pro Stimme (`strike_gain_`), die den internen Exciter (Impuls, Zupf-Rauschen) und das Eingangssignal der angeschlagenen Stimme skaliert.
     Ohne DroidLink ist sie immer 1, Rings verhält sich dann wie das Original.
  2. `dsp/fm_voice.h`: `TriggerInternalEnvelope()` nimmt die Anschlagstärke (Default 1).
- **Ersatz für die Hardware-Dateien des Originals:**
  - `main.cpp` statt `rings.cc`: Audio-Callback mit 24 Frames bei 48 kHz (2 kHz Blockrate wie im Original), Rauschsperre, Ausgänge.
  - `cv_scaler.h` statt `cv_scaler.cc`: Knob + CV → `Patch`/`PerformanceState`, Erkennung von STRUM und V/OCT.
  - `input_detect.h`: Ersatz für den `NormalizationDetector` am IN-Eingang.
  - `droid_voices.h`: verteilt die DroidLink-Stimmen auf die Rings-Stimmen (Warteschlange für Akkorde).
  - `ui.{h,cpp}` statt `ui.cc`: Seiten, Zonen für MODEL/POLYPHONY, Taster, LED, Kalibrierung.
  - `settings.h` statt `settings.cc`: QSPI-Einstellungen.
  - Geteilt mit Beads: `firmware/shared/layered_knob.h` (Catch-up) und `firmware/shared/clock_detector.h` (STRUM-Erkennung).
- **Speicher:**
  - `rings::Part` (ca. 110 KB, vor allem 8 Saiten-Delays) passt nicht mehr ins DTCM und liegt im D2-SRAM.
    libDaisy hat dafür keine eigene Sektion; das Objekt liegt in der Sektion `.heap` (NOLOAD, deshalb in `main()` von Hand genullt).
  - Reverb-Puffer (64 KB, im Original im CCM) und alles andere: DTCM, zusammen ca. 85 KB.
  - Code: ca. 160 KB im SRAM.
- **DroidLink** braucht TIM5 (20 kHz Timer-Interrupt, ca. 1 % CPU) und senkt die Interrupt-Priorität der Audio-DMA um eine Stufe, siehe `firmware/shared/droid_link_patch_init.h`.
- **CPU-Last messen:** `#define RINGS_DEBUG_CPU` in `main.cpp` aktivieren und ein serielles Terminal auf den USB-Port des Daisy öffnen.
  Worst Case: Sympathetische Saiten (Zone 2 oder 5), POLYPHONY 4, oft anschlagen.
