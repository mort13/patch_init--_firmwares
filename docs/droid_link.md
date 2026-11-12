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
| O7 | → | CV In 4, Buchse **CV_8** | Daten |
| O8 | → | Gate In 2, Buchse **B9** | Takt |

Die Frontplatte des patch.init() trägt die Pin-Namen des Patch SM: Die CV-Eingänge heißen CV_5–CV_8, die Gate-Eingänge B10 (Gate In 1) und **B9** (Gate In 2).
| X7 MIDI In (TRS) oder USB | | | MIDI vom Keyboard/DAW, wird automatisch erkannt |

Takt und Daten müssen an **Master-Ausgängen** (O1–O8) liegen, damit beide im selben DROID-Zyklus wechseln.
Über G8- oder X7-Gates ist das nicht garantiert.

## Schnellstart

1. `droid/droid_link.ini` in den DROID laden (Forge oder SD-Karte). Der Patch braucht O7 und O8; zum Einbau in einen bestehenden Patch die beiden Ausgänge anpassen.
   Danach zeigt der MASTER an seinen Ausgangs-LEDs, ob er sendet:

   | LED am MASTER | Bedeutung |
   |---|---|
   | Ausgang 8 blinkt grün, 2× pro Sekunde | Takt läuft (400 Hz, für die Anzeige durch 200 geteilt) |
   | Ausgang 7 leuchtet blau | mindestens eine Taste gehalten, MIDI kommt im Patch an |
   | LEDs blinken als Code (alle in einer Farbe oder einzelne als Zeilennummer) | Fehler im Patch, er läuft nicht (Klartext in `DROIDERR.TXT` auf der SD-Karte) |

   Der Diagnose-Block am Ende von `droid_link.ini` überschreibt dafür die LEDs von O7/O8 (Register `R15`/`R16`). Wer die normale Spannungsanzeige will, löscht ihn.
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
   | langsames Blinken | Verbindung steht, aber der CV-Eingang schwingt nicht rechtzeitig ein → am DROID `[lfo] hz` senken |

4. Akkord spielen: Audio Out L/R gibt jede Stimme als Sinus aus (Knob 1 = Lautstärke, Knob 2 = Release), CV Out zeigt das Modwheel (0–5 V).
   Über USB (serieller Monitor, z. B. PuTTY) kommt jede Sekunde eine Statistik mit Slotrate, Kalibrierung, Fehlerzählern und den 4 Stimmen.

### Kein Takt am patch.init()

1. **LED Ausgang 8 am MASTER blinkt nicht grün:** Der Patch läuft nicht oder nicht dieser.
   Blinken LEDs als Code, ist ein Fehler im Patch (`DROIDERR.TXT`). Ist DroidLink in einen bestehenden Patch eingefügt, darf kein anderer Circuit mehr auf O7 oder O8 schreiben.
2. **LED blinkt, aber am patch.init() kommt nichts an:** Kabel prüfen. Es muss von **O8** (die Ausgänge sind die unteren zwei Buchsenreihen am MASTER) nach **Gate In 2** gehen, nicht Gate In 1.
   Gegenprobe: dasselbe Kabel statt in Gate In 2 in einen freien DROID-Eingang stecken. Dessen LED leuchtet dann, wenn am Kabel Spannung ankommt.
3. **Rings statt Monitor-Firmware:** Rings zeigt die Verbindung erst an, wenn auch gültige Daten auf CV 4 ankommen (LED 3× kurz). Für die Fehlersuche ist die Monitor-Firmware besser, sie unterscheidet „kein Takt“ und „Takt ohne Daten“.

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

- **Takt** (Gate): Rechteck, 400 Hz, Pulsbreite 50 %, 0/5 V.
  Die steigende Flanke startet den nächsten Slot. **Abgetastet wird spät im alten Slot**: der Wert 0,5 ms vor der erkannten steigenden Flanke.
  Die fallende Flanke wird nicht ausgewertet, die Pulsbreite ist beliebig. Die Rate darf beliebig sein, solange der Slot lang genug zum Einschwingen ist.
- **Drift-Prüfung:** Der Empfänger vergleicht jeden Abtastwert mit dem Wert 0,3 ms davor. Bewegt sich ein Slot um mehr als 40 mV, wird der ganze Rahmen verworfen (`Stats::frames_drift`).
  Eine Geister-Note braucht mindestens 250 mV Fehler und kommt so nicht durch.
- **Low-Phase des Takts:** Der DROID rechnet nur mit 3–6 kHz. Die Low-Phase muss deshalb mehrere DROID-Zyklen lang sein, sonst kommt sie nicht sauber am Gate-Eingang an.
  Mit 1 kHz und 75 % (0,25 ms Low) sah das patch.init() nur etwa 6 von 1000 Flanken pro Sekunde. Mit 400 Hz und 50 % ist die Low-Phase 1,25 ms lang.
- **Daten** (CV): pro Slot ein Spannungswert, am DROID per Sample & Hold für den ganzen Slot festgehalten.
- **Gemessen** (Rohmitschnitt der Monitor-Firmware mit 20 kHz, Anschlag-Protokoll der Rings-Firmware):
  - Die DROID-Ausgänge glätten mit ca. 0,2–0,25 ms Zeitkonstante, am Takt wie an den Daten. Die fallende Taktflanke kommt dadurch ca. 0,35 ms später an, und die Daten brauchen gut 1,5 ms für unter 5 mV Abweichung.
  - Der Daten-Wert ändert sich manchmal schon **vor** der erkannten Taktflanke, bei Sprüngen von −4 V auf +3,5 V teils fast vollständig. Vermutlich aktualisiert der DROID O7 und O8 nicht ganz gleichzeitig.
    Mit 0,1 und 0,2 ms Abstand kamen Geister-Noten: ein „Gate aus“ (−4 V), halb oder ganz auf dem Weg zum folgenden Velocity-Slot gelesen, ergab eine zufällige Tonhöhe (gemessen u. a. MIDI 31, 34, 53, 91, 96, 101).
  - Der ADC des patch.init() liest 12 Kanäle mit 32-fachem Oversampling und liefert für CV In 4 nur alle 1,05 ms einen neuen Wert. `PatchInitLink` stellt deshalb 8-fach ein (`Config::adc_oversampling`): ein neuer Wert alle 0,26 ms, Rauschen ca. 3–4 mV.
  - Die Audio-Eingänge sind wechselspannungsgekoppelt (Zeitkonstante ca. 90 ms) und taugen deshalb nicht für die Daten.
  - Bei 500 Hz und 0,2 ms Abstand: gehaltene Noten im Mittel −0,8 Cent, höchstens 3 Cent daneben (MIDI 48–86), aber vereinzelt Geister-Noten.

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

| | bei `hz = 400`, `pulsewidth = 0.5`, 1 Aux-Slot |
|---|---|
| Rahmen | 11 Slots = 27,5 ms (ca. 36 Rahmen/s) |
| Latenz Note-On | typ. 20–35 ms, max. ca. 50 ms |
| Einschwingzeit pro Slot | mind. ca. 1,7 ms (2,5 ms Slot − 0,5 ms Abstand − bis 0,26 ms ADC-Alter) |

Weniger Latenz: `hz` schrittweise erhöhen (z. B. 600) und im Monitor prüfen, dass `bad` nicht zählt und gehaltene Noten weiter auf ganze Zahlen kommen.
Unter ca. 1,2 ms Einschwingzeit werden die Tonhöhen ungenau (Zeitkonstante der DROID-Ausgänge 0,2–0,25 ms).

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

- **Neuanschlag**: Ein Gate-Aus muss mindestens einen Rahmen (ca. 27,5 ms) dauern, sonst wird es übersehen und die Stimme spielt legato weiter.
  Mit `roundrobin = 1` kommt das praktisch nur vor, wenn mehr als 4 Tasten gleichzeitig gedrückt sind und der DROID eine Stimme klaut.
  Dafür im Patch `notegap = 15` aktivieren (kostet bei geklauten Stimmen 15 ms Latenz).
- **Tonumfang**: MIDI-Noten 20 … 100 (G#0 … E7) inkl. Pitchbend-Reserve.
- **Einschwingen**: begrenzt durch die Glättung der DROID-Ausgänge (gemessen 0,2–0,25 ms, siehe *Physik*).
  Meldet der Monitor Einschwingfehler (LED blinkt langsam, `settle(mV)` > 10), `[lfo] hz` am DROID senken – das erhöht die Latenz.
- **ADC-Oversampling**: `PatchInitLink` setzt den ADC der ganzen Firmware auf 8-fach. Knobs und CV-Eingänge rauschen dadurch etwas mehr (ca. 3–4 mV); wer das nicht will, setzt `Config::adc_oversampling = 0` und senkt `[lfo] hz` auf ca. 300.

## Diagnose

`droid.GetStats()` liefert:

| Feld | Bedeutung |
|---|---|
| `clock`, `slot_hz` | Takt vorhanden, gemessene Slotrate |
| `frames_ok`, `frames_bad`, `sync_losses` | übernommene / verworfene Rahmen, Verbindungsabbrüche |
| `frames_drift` | davon verworfen, weil ein Slot beim Abtasten noch in Bewegung war (Drift-Prüfung). Einzelne sind normal; steigt der Wert stetig, `[lfo] hz` senken |
| `frame_slots` | Rahmenlänge (11 beim Beispiel-Patch) |
| `gain`, `offset` | gelernte Kalibrierung des CV-Eingangs |
| `settle_error` | mittlere Differenz zwischen Abtastwert (Slotende) und einem Wert ein Viertel Slot früher, in V. Rauschen allein ergibt ca. 0,003–0,004 V; deutlich mehr heißt: Takt zu schnell |
| `ticks`, `falls` | Rohzähler: Abfragen seit Start (steigt um `poll_hz`, sonst läuft der Timer nicht) und gesehene fallende Taktflanken (steigt um die Slotrate, sonst kommt der Takt nicht an) |

Die Monitor-Firmware gibt außerdem alle 5 s einen **Rohmitschnitt** aus (`cap begin` … `cap end`): 30 ms Takt-Pegel und CV-In-4-Spannung mit 20 kHz ab einer steigenden Flanke, dazu Audio In R mit 48 kHz.
Damit lassen sich Einschwingen, ADC-Takt und Flankenlage direkt ansehen.
| `gate`, `volts` | momentaner Pegel am Takt-Eingang und Spannung am Daten-Eingang |

Die Monitor-Firmware gibt alle Werte jede Sekunde über USB aus, die Rohzähler in der Zeile `diag`.

## PC-Test

`tests/droid_link_test.cpp` simuliert die ganze Strecke: den DROID-Patch mit seiner Zyklusrate (~5,5 kHz), den CV-Eingang mit Tiefpass, Verstärkungs- und Offsetfehler sowie Rauschen, und den Empfänger mit 20 kHz – mit demselben Code wie auf der Hardware.
Geprüft werden Lock, Akkord, Kalibrierung, Release, Neuanschlag, Pitchbend, Störimpuls, Kabel ab/an, Formatwechsel, Randwerte und die Einschwing-Diagnose.

```bash
make -C tests    # braucht einen PC-Compiler (g++ oder clang++), nicht arm-none-eabi
```
