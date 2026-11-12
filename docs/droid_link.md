# DroidLink – MIDI-Akkorde vom DROID zum patch.init()

DroidLink überträgt **4 Stimmen (Gate, Pitch, Velocity) und bis zu 6 weitere Werte** (Modwheel, CCs, …) vom DROID zum patch.init() – über **ein CV-Kabel und ein Gate-Kabel**.
Der DROID nimmt MIDI über das X7 an (TRS oder USB), verteilt die Noten auf 4 Stimmen und schickt sie als Zeitmultiplex-Strom weiter.
Das patch.init() dekodiert den Strom und kalibriert sich dabei selbst.

Belegt am patch.init() nur **CV In 4** und **Gate In 2** (beides umstellbar).
CV In 1–3, Gate In 1 und die Audio-Eingänge bleiben frei.

| Teil | Datei |
|---|---|
| DROID-Patch (Sender) | [`droid/droid_link.ini`](../droid/droid_link.ini) |
| Empfänger, hardwareunabhängig | [`firmware/shared/droid_link.h`](../firmware/shared/droid_link.h) |
| Empfänger, patch.init()-Anbindung | [`firmware/shared/droid_link_patch_init.h`](../firmware/shared/droid_link_patch_init.h) |
| Testfirmware | [`firmware/droid_link_monitor/`](../firmware/droid_link_monitor/main.cpp) |
| Rings-Port: spielt die 4 Stimmen polyphon, schaltet sich bei gültigen Rahmen selbst zu | [`firmware/rings/`](../firmware/rings/README.md) |
| PC-Simulation | [`tests/droid_link_test.cpp`](../tests/droid_link_test.cpp) |

## Verkabelung

| DROID | → | patch.init() | Funktion |
|---|---|---|---|
| O7 | → | CV In 4 | Daten |
| O8 | → | Gate In 2 | Takt |
| X7 MIDI In (TRS) oder USB | | | MIDI vom Keyboard/DAW, wird automatisch erkannt |

Takt und Daten müssen an **Master-Ausgängen** (O1–O8) liegen, damit beide im selben DROID-Zyklus wechseln.
Über G8- oder X7-Gates ist das nicht garantiert.

## Schnellstart

1. `droid/droid_link.ini` in den DROID laden (Forge oder SD-Karte). Der Patch braucht O7 und O8; zum Einbau in einen bestehenden Patch die beiden Ausgänge anpassen.
2. Testfirmware bauen und flashen:
   ```bash
   make droid_link_monitor
   cd firmware/droid_link_monitor && make program-dfu
   ```
3. LED am patch.init() beobachten:

   | LED | Bedeutung |
   |---|---|
   | aus | kein Takt an Gate In 2 |
   | schnelles Blinken | Takt da, aber keine gültigen Rahmen (Daten-Kabel? falscher Patch?) |
   | an (gedimmt), blitzt bei jeder Note | Verbindung steht |
   | langsames Blinken | Verbindung steht, aber der CV-Eingang schwingt nicht rechtzeitig ein → am DROID `[lfo] hz` auf 500 senken |

4. Akkord spielen: Audio Out L/R gibt jede Stimme als Sinus aus (Knob 1 = Lautstärke, Knob 2 = Release), CV Out zeigt das Modwheel (0–5 V).
   Über USB (serieller Monitor, z. B. PuTTY) kommt jede Sekunde eine Statistik mit Slotrate, Kalibrierung, Fehlerzählern und den 4 Stimmen.

## In eine eigene Firmware einbauen

```cpp
#include "droid_link_patch_init.h"

droid_link::PatchInitLink droid;   // nicht "link" nennen: kollidiert mit POSIX link()

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    const droid_link::State s = droid.GetState();
    for(int v = 0; v < droid_link::kNumVoices; v++)
    {
        const droid_link::Voice& voice = s.voice[v];
        // voice.gate       Gate an/aus
        // voice.Note()     MIDI-Note (gebrochen, inkl. Pitchbend) -> daisysp::mtof()
        // voice.pitch      dasselbe als 1 V/Okt, 0 V = MIDI 60
        // voice.velocity   0..1, bleibt im Release erhalten
        // voice.note_ons   zählt Anschläge (Änderung = neue Note)
    }
    const float modwheel = s.AuxUnipolar(0); // 0..1
    // ...
}

int main()
{
    hw.Init();
    droid.Init(hw);   // CV In 4 + Gate In 2
    droid.Start();    // Timer-Interrupt, 20 kHz
    hw.StartAudio(AudioCallback);
    while(1) {}
}
```

Andere Eingänge wählen:

```cpp
droid_link::PatchInitLink::Config cfg;
cfg.cv_in   = daisy::patch_sm::CV_5;  // CV_5..CV_8 = CV In 1..4
cfg.gate_in = 1;                      // Gate In 1
droid.Init(hw, cfg);
```

Der Empfang läuft in einem eigenen Timer-Interrupt (TIM5, Priorität 0) und ist damit unabhängig von Blockgröße und Last des Audio-Callbacks.
Dafür senkt `Start()` die Interrupt-Priorität der Audio-DMA um eine Stufe, damit der Timer das Callback unterbrechen darf (ca. 1 % CPU).
`GetState()` und `GetStats()` liefern konsistente Kopien und dürfen im Audio-Callback und in der Hauptschleife aufgerufen werden.

## Protokoll

### Physik

- **Takt** (Gate): Rechteck, 1 kHz, Pulsbreite 75 %, 0/5 V.
  Die steigende Flanke startet den nächsten Slot, an der **fallenden Flanke tastet das patch.init() ab** (75 % des Slots, Wert eingeschwungen).
  Der Empfänger ist rein flankengesteuert – die Rate darf beliebig sein, solange der Slot lang genug zum Einschwingen ist.
- **Daten** (CV): pro Slot ein Spannungswert, am DROID per Sample & Hold für den ganzen Slot festgehalten.

### Rahmen

| Slot | Inhalt | Spannung |
|---|---|---|
| 0 | SYNC | +4,0 V – einziger Wert über +3,75 V, markiert den Rahmenanfang |
| 1 | REF | 0,0 V |
| 2 … 1+A | AUX 1 … A | Wert 0..1 × 3,5 V (oder beliebig −3,5 … +3,5 V) |
| je Stimme k = 1…4 | NOTE k | Gate an: Pitch, 1 V/Okt, 0 V = MIDI-Note 60, −3,5 … +3,5 V · Gate aus: −4,0 V |
| | VEL k | Velocity 0..1 × 3,5 V |

- A = 0 … 6 Aux-Slots, Rahmenlänge = 10 + A (10 … 16 Slots, der DROID-`[switch]` hat 16 Eingänge).
- Die Aux-Slots stehen **vor** den Stimmen, damit die Stimmen möglichst kurz vor dem nächsten SYNC liegen (geringere Latenz).
- Der Empfänger erkennt A an der Rahmenlänge.

### Kalibrierung

SYNC (+4 V) und REF (0 V) sind exakte DROID-Spannungen.
Daraus bestimmt der Empfänger in jedem Rahmen Verstärkung und Offset seines CV-Eingangs und rechnet alle Werte damit um (gleitender Mittelwert über ca. 16 Rahmen).
Toleranzen des patch.init()-Eingangs (typ. 1–2 % Verstärkungsfehler, einige mV Offset) wirken sich so nicht auf die Stimmung aus – Intervalle in Akkorden bleiben sauber.

### Prüfung und Fehlerverhalten

Ein Rahmen wird nur übernommen, wenn

- seine Länge 10 … 16 Slots beträgt und mit der des vorigen Rahmens übereinstimmt,
- REF innerhalb ±0,4 V liegt und die Verstärkung innerhalb ±10 %,
- alle Werte im erlaubten Bereich liegen (insbesondere sind Velocity-Slots nie negativ – ein −4 V-Wert dort heißt, die Slots sind verrutscht).

Sonst wird der ganze Rahmen verworfen; eine Störung auf der Taktleitung kostet also einen Rahmen, erzeugt aber nie falsche Noten.
Nach 4 Fehlern in Folge oder 50 ms ohne Takt gilt die Verbindung als verloren: **alle Gates gehen aus**, keine hängenden Noten.
Danach synchronisiert der Empfänger von selbst neu (2 Rahmen).

### Timing

| | bei `hz = 1000`, 1 Aux-Slot |
|---|---|
| Rahmen | 11 Slots = 11 ms (ca. 90 Rahmen/s) |
| Latenz Note-On | typ. 8–14 ms, max. ca. 20 ms |
| Einschwingzeit pro Slot | 0,75 ms |

Die Stimmen werden am Rahmenende gemeinsam übernommen, Akkord-Töne starten also gleichzeitig.

## DROID-Patch im Detail

- `transpose = -3V`: Der DROID gibt MIDI-Note 24 (C0) als 0 V aus, das Protokoll erwartet MIDI-Note 60 bei 0 V.
- `lowestnote = 20`, `highestnote = 100`: Mit ±2 Halbtönen Pitchbend bleiben alle Noten innerhalb ±3,5 V.
- `roundrobin = 1`: Eine wiederholte Note landet auf einer freien Stimme. So wird sie sicher neu angeschlagen und die alte klingt im Release aus.
- `holdvelocity = 1`: Velocity bleibt nach dem Loslassen erhalten (für Release-Phasen).
- Pitchbend wirkt direkt auf die Pitches (`bendpitch` ist am DROID per Default an, Bereich `pitchbendrange`).
- `NOTE`-Slot: Der DROID kennt nur `A * B + C`, darum `_GATE1 * _PITCH1_UP - 4V` mit `_PITCH1_UP = _PITCH1 + 4V`: Gate an → Pitch, Gate aus → −4 V.

Weitere Aux-Werte: zusätzliche `switch`-Eingänge **direkt nach `input3`** einfügen und die folgenden Eingänge weiterzählen, z. B.

```ini
    input3  = _MODWHEEL * 3.5V    # AUX1
    input4  = _PRESSURE * 3.5V    # AUX2  (midiin: pressure = _PRESSURE)
    input5  = _GATE1 * _PITCH1_UP - 4V
    ...
    input12 = _VEL4 * 3.5V
```

Auf dem patch.init() steht AUX2 dann in `s.AuxUnipolar(1)`.

## Grenzen

- **Neuanschlag**: Ein Gate-Aus muss mindestens einen Rahmen (ca. 11 ms) dauern, sonst wird es übersehen und die Stimme spielt legato weiter.
  Mit `roundrobin = 1` kommt das praktisch nur vor, wenn mehr als 4 Tasten gleichzeitig gedrückt sind und der DROID eine Stimme klaut.
  Dafür im Patch `notegap = 15` aktivieren (kostet bei geklauten Stimmen 15 ms Latenz).
- **Tonumfang**: MIDI-Noten 20 … 100 (G#0 … E7) inkl. Pitchbend-Reserve.
- **Einschwingen**: Die Zeitkonstante des patch.init()-CV-Eingangs ist nicht dokumentiert.
  Bei 1 kHz stehen 0,75 ms zum Einschwingen zur Verfügung.
  Meldet der Monitor Einschwingfehler (LED blinkt langsam, `settle(mV)` > 2), `[lfo] hz` am DROID auf 500 senken – das verdoppelt die Latenz.

## Diagnose

`droid.GetStats()` liefert:

| Feld | Bedeutung |
|---|---|
| `clock`, `slot_hz` | Takt vorhanden, gemessene Slotrate |
| `frames_ok`, `frames_bad`, `sync_losses` | übernommene / verworfene Rahmen, Verbindungsabbrüche |
| `frame_slots` | Rahmenlänge (11 beim Beispiel-Patch) |
| `gain`, `offset` | gelernte Kalibrierung des CV-Eingangs |
| `settle_error` | mittlere Differenz zwischen Abtastwert und Slotende in V. Rauschen allein ergibt < 0,001 V; deutlich mehr heißt: Takt zu schnell |

## PC-Test

`tests/droid_link_test.cpp` simuliert die ganze Strecke: den DROID-Patch mit seiner Zyklusrate (~5,5 kHz), den CV-Eingang mit Tiefpass, Verstärkungs- und Offsetfehler sowie Rauschen, und den Empfänger mit 20 kHz – mit demselben Code wie auf der Hardware.
Geprüft werden Lock, Akkord, Kalibrierung, Release, Neuanschlag, Pitchbend, Störimpuls, Kabel ab/an, Formatwechsel, Randwerte und die Einschwing-Diagnose.

```bash
make -C tests    # braucht einen PC-Compiler (g++ oder clang++), nicht arm-none-eabi
```
