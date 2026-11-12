/**
 * Mutable Instruments Rings fuer den patch.init().
 *
 * Der Resonator (rings/dsp/) stammt unveraendert aus pichenettes/eurorack
 * (MIT, Emilie Gillet), siehe README.md. Diese Datei ersetzt rings.cc:
 * Codec, ADC-Treiber, Normalisierungs-Probe und Flash-Settings des Originals
 * werden durch libDaisy und Heuristiken ersetzt.
 *
 *   Audio In L+R  -> IN (Exciter)
 *   CV 1..4       -> V/OCT, STRUCTURE, BRIGHTNESS, DAMPING
 *   Gate In 1     -> STRUM
 *   Audio Out L/R -> ODD / EVEN (mit Stereo-Breite, ganz links = Mono-Mix)
 *   Gate Out 1    -> Strum-Trigger (5 ms)
 *   Gate Out 2    -> Audio-Eingang erkannt (externer Exciter aktiv)
 *   CV Out        -> Ausgangspegel, 0 V = -60 dBFS .. 5 V = 0 dBFS
 *   LED           -> Ausgangspegel, Blitz bei jedem Strum, Blinkcodes
 *
 * DroidLink (docs/droid_link.md): Kommen an CV 4 (Daten) und Gate In 2 (Takt)
 * gueltige Rahmen an, spielt Rings die 4 MIDI-Stimmen vom DROID. Dann gilt:
 *   Note-On       -> Anschlag der Rings-Stimme k % POLYPHONY, Velocity = Staerke
 *   FREQUENCY     -> Oktavlage -2..+2,  CV 1 -> Transponierung in Halbtoenen
 *   AUX 1 (Modwheel) -> DAMPING (statt CV 4),  AUX 2 -> POSITION
 *
 * Bedienung: siehe ui.h
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "daisy_patch_sm.h"
#include "util/CpuLoadMeter.h"
#include "util/PersistentStorage.h"

#include "stmlib/utils/random.h"

#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"

#include "cv_scaler.h"
#include "droid_link_patch_init.h"
#include "droid_voices.h"
#include "input_detect.h"
#include "patch_init.h"
#include "settings.h"
#include "ui.h"

// CPU-Last jede Sekunde ueber USB-Seriell ausgeben (Terminal auf den COM-Port des Daisy)
// #define RINGS_DEBUG_CPU

using namespace daisy;
using namespace daisy::patch_sm;
using namespace rings_port;

namespace
{

/** Rings rechnet mit 24 Samples pro Block bei 48 kHz (rings/dsp/dsp.h) = 2 kHz */
constexpr size_t kBlockSize = rings::kMaxBlockSize;
const float      kBlockRate = rings::kSampleRate / float(kBlockSize);

/**
 * Offset im 8-MB-QSPI-Flash. Die App liegt ab 0x40000 und ist < 480 KB gross.
 * Beads nutzt 0x7F0000, so behalten beide Firmwares ihre Einstellungen.
 */
constexpr uint32_t kSettingsOffset      = 0x7E0000;
constexpr uint32_t kSettingsSaveDelayMs = 2000;
constexpr uint32_t kSettingsPollMs      = 100;

/** Rauschsperre fuer den Audio-Eingang (rings.cc:84), auf den mittleren Pegel */
constexpr float kNoiseGateThreshold = 0.00003f;

const int       kStrumPulseBlocks = int(kBlockRate * 0.005f); // 5 ms
const float     kLevelRelease     = 1.f / (0.1f * kBlockRate); // 100 ms
constexpr float kLevelRangeDb     = 60.f;

/** DroidLink: Abstand zwischen zwei Anschlaegen in Bloecken (siehe droid_voices.h) */
constexpr int kPartStrikeSpacing = 1;
constexpr int kDpStrikeSpacing   = 10;
/** DroidLink-Note 0 (= MIDI 60) bei FREQUENCY in der mittleren Zone */
constexpr float kDroidLinkTonic = 60.f;
static_assert(DroidVoices::kNumVoices == rings::kMaxPolyphony,
              "Part::set_direct_control() erwartet eine Note pro Rings-Stimme");

inline float Clamp(float x, float lo, float hi)
{
    return std::min(std::max(x, lo), hi);
}

/** Velocity 0..1 -> Anschlagstaerke; quadratisch, 0,5 = -12 dB */
inline float VelocityGain(float velocity)
{
    return velocity * velocity;
}

} // namespace

/**
 * libDaisy hat keine BSS-Sektion fuer das D2-SRAM (256 KB ab 0x30008000,
 * gecacht). Die Sektion ".heap" liegt dort und uebernimmt per KEEP(*(.heap))
 * beliebige Objekte; malloc() beginnt erst hinter ihrem Ende. Sie ist NOLOAD,
 * wird also nicht genullt - das passiert in main() von Hand.
 */
#define RINGS_D2_BSS __attribute__((section(".heap")))

DaisyPatchSM                hw;
PersistentStorage<Settings> storage(hw.qspi);

// Part braucht ca. 110 KB (8 Saiten mit je 3 K Floats Delay) und passt nicht
// mehr ins DTCM. Der Reverb-Puffer liegt im Original im CCM, hier im DTCM.
RINGS_D2_BSS rings::Part part;
uint16_t                 reverb_buffer[32768];

rings::StringSynthPart string_synth;
rings::Strummer        strummer;
Ui                     ui;
CvScaler               cv_scaler;
ExciterDetector        exciter_detector;
CpuLoadMeter           cpu_meter;

droid_link::PatchInitLink droid; // nicht "link" nennen: kollidiert mit POSIX link()
DroidVoices               droid_voices;

float in_level        = 0.f;
float output_level    = 0.f;
int   strum_pulse     = 0;
int   applied_model   = -1;
int   applied_poly    = -1;

#ifdef RINGS_DEBUG_CPU
/** Debug: Protokoll aller Anschlaege. Der Audio-Callback schreibt, die
 *  Hauptschleife gibt aus (ein Schreiber, ein Leser). */
enum StrikeSource : uint8_t
{
    SRC_MIDI,
    SRC_GATE,
    SRC_BUTTON,
    SRC_INTERNAL,
};
struct StrikeEvent
{
    uint32_t ms;
    float    note; // MIDI-Note wie vom DROID dekodiert
    float    velocity;
    int8_t   voice; // Rings-Stimme, -1 = aktive Stimme
    uint8_t  source;
};
constexpr uint32_t kNumStrikeEvents = 64;
StrikeEvent        strike_log[kNumStrikeEvents];
volatile uint32_t  strike_log_write = 0;

inline void LogStrike(uint32_t ms, int voice, uint8_t source, float note, float velocity)
{
    StrikeEvent& e = strike_log[strike_log_write % kNumStrikeEvents];
    e.ms           = ms;
    e.note         = note;
    e.velocity     = velocity;
    e.voice        = int8_t(voice);
    e.source       = source;
    __DMB();
    strike_log_write = strike_log_write + 1;
}
#endif

/** Modell / Polyphonie nur bei Aenderung setzen: Part::set_polyphony()
 *  initialisiert die Resonatoren neu. */
void ApplyModelAndPolyphony()
{
    if(ui.model() != applied_model)
    {
        applied_model = ui.model();
        part.set_model(rings::ResonatorModel(applied_model));
        string_synth.set_fx(rings::FxType(applied_model));
    }
    if(ui.polyphony() != applied_poly)
    {
        applied_poly = ui.polyphony();
        part.set_polyphony(applied_poly);
        string_synth.set_polyphony(applied_poly);
    }
}

/** Ausgangspegel als 0..1 auf einer dB-Skala (-60..0 dBFS) */
float ProcessLevel(const float* l, const float* r, size_t size)
{
    float peak = 0.f;
    for(size_t i = 0; i < size; i++)
        peak = std::max(peak, std::max(fabsf(l[i]), fabsf(r[i])));
    if(peak > output_level)
        output_level = peak;
    else
        output_level += kLevelRelease * (peak - output_level);
    const float db = 20.f * log10f(output_level + 1e-6f);
    return Clamp(1.f + db / kLevelRangeDb, 0.f, 1.f);
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    cpu_meter.OnBlockStart();
    size = std::min(size, kBlockSize);

    hw.ProcessAllControls();
    const uint32_t now = System::GetNow();
    ui.Process(now);
    ApplyModelAndPolyphony();

    // Rings hat nur einen Eingang: L und R werden addiert, so funktioniert
    // jede der beiden Buchsen.
    float input[kBlockSize];
    for(size_t i = 0; i < size; i++)
        input[i] = in[0][i] + in[1][i];
    exciter_detector.Process(input, size);

    // DroidLink
    const droid_link::State link = droid.GetState();
    droid_voices.Process(link,
                         ui.polyphony(),
                         ui.easter_egg() ? kDpStrikeSpacing : kPartStrikeSpacing);
    const bool linked = droid_voices.locked();
    if(droid_voices.just_locked())
        ui.ShowDroidLink(now);

    float cv[4] = {hw.GetAdcValue(patch_init::CV_IN_1),
                   hw.GetAdcValue(patch_init::CV_IN_2),
                   hw.GetAdcValue(patch_init::CV_IN_3),
                   hw.GetAdcValue(patch_init::CV_IN_4)};
    if(linked)
        cv[3] = link.AuxUnipolar(0); // CV 4 ist die Datenleitung; Modwheel -> DAMPING

    rings::Patch            patch;
    rings::PerformanceState performance_state;
    cv_scaler.Read(ui, cv, hw.gate_in_1.State(), &patch, &performance_state);
    performance_state.internal_exciter = !exciter_detector.active();

    // STRUM-Gate und Taster. Ohne DroidLink kommt der Strummer noch dazu.
    const bool gate_strum   = performance_state.strum;
    const bool manual_strum = gate_strum || ui.strum_pressed();
    const bool midi_strike  = droid_voices.strike_voice() >= 0;
    if(linked)
    {
        patch.position = Clamp(patch.position + link.AuxUnipolar(1), 0.f, 1.f);
        // Note = DroidLink-Pitch, Tonika = MIDI 60 + Oktavlage + CV 1 (Halbtoene)
        performance_state.tonic = kDroidLinkTonic + 12.f * float(ui.frequency_octave())
                                  + roundf(ui.VoctSemitones(cv[0]));
        performance_state.note  = droid_voices.last_note();
        performance_state.strum = midi_strike || manual_strum;
    }

    float odd[kBlockSize];
    float even[kBlockSize];
    if(ui.easter_egg())
    {
        if(!linked)
        {
            strummer.Process(nullptr, size, &performance_state);
            performance_state.strum |= ui.strum_pressed();
        }
        part.clear_direct_control();
        string_synth.Process(performance_state, patch, input, odd, even, size);
    }
    else
    {
        // Rauschsperre wie rings.cc:83-95
        for(size_t i = 0; i < size; i++)
        {
            const float error = input[i] * input[i] - in_level;
            in_level += error * (error > 0.f ? 0.1f : 0.0001f);
            const float gain = in_level <= kNoiseGateThreshold
                                   ? (1.f / kNoiseGateThreshold) * in_level
                                   : 1.f;
            input[i] *= gain;
        }
        if(linked)
        {
            // Direktsteuerung (Patch in rings/dsp/part.cc): Stimme und
            // Velocity vom DROID. STRUM/Taster schlagen die aktive Stimme
            // erneut an.
            part.set_direct_control(droid_voices.notes(),
                                    droid_voices.strike_voice(),
                                    midi_strike ? VelocityGain(droid_voices.strike_velocity())
                                                : 1.f);
        }
        else
        {
            part.clear_direct_control();
            strummer.Process(input, size, &performance_state);
            // Der Taster schlaegt immer an, auch wenn der Strummer gerade
            // intern entscheidet (STRUM-Eingang leer).
            performance_state.strum |= ui.strum_pressed();
        }
        part.Process(performance_state, patch, input, odd, even, size);
    }

#ifdef RINGS_DEBUG_CPU
    if(performance_state.strum)
    {
        if(linked && midi_strike)
            LogStrike(now,
                      droid_voices.strike_voice(),
                      SRC_MIDI,
                      kDroidLinkTonic + droid_voices.notes()[droid_voices.strike_voice()],
                      droid_voices.strike_velocity());
        else
            LogStrike(now,
                      -1,
                      ui.strum_pressed() ? SRC_BUTTON : (gate_strum ? SRC_GATE : SRC_INTERNAL),
                      0.f,
                      1.f);
    }
#else
    (void)gate_strum;
#endif

    // Stereo-Breite: 1 = ODD links / EVEN rechts wie das Original,
    // 0 = beide Ausgaenge bekommen den Mono-Mix (im Original: nur ODD gesteckt).
    const float width = ui.width();
    for(size_t i = 0; i < size; i++)
    {
        const float mid  = 0.5f * (odd[i] + even[i]);
        const float side = 0.5f * (odd[i] - even[i]) * width;
        out[0][i]        = Clamp(mid + side, -1.f, 0.999999f);
        out[1][i]        = Clamp(mid - side, -1.f, 0.999999f);
    }

    // Nebenausgaenge
    const float level = ProcessLevel(out[0], out[1], size);
    if(performance_state.strum)
        strum_pulse = kStrumPulseBlocks;
    else if(strum_pulse > 0)
        --strum_pulse;
    hw.gate_out_1.Write(strum_pulse > 0);
    hw.gate_out_2.Write(exciter_detector.active());
    hw.WriteCvOut(patch_init::CV_OUT, level * 5.f);
    hw.WriteCvOut(patch_init::LED, ui.Led(now, level, performance_state.strum) * 5.f);

    cpu_meter.OnBlockEnd();
}

Settings LoadSettings()
{
    storage.Init(DefaultSettings(), kSettingsOffset);
    if(storage.GetSettings().version != Settings::kVersion)
        storage.RestoreDefaults();

    Settings s     = storage.GetSettings();
    s.easter_egg   = s.easter_egg ? 1 : 0;
    const Settings defaults = DefaultSettings();
    for(int i = 0; i < 4; i++)
    {
        if(!(s.page_b[i] >= 0.f && s.page_b[i] <= 1.f))
            s.page_b[i] = defaults.page_b[i];
    }
    if(!(fabsf(s.voct_scale / kVoctNominalScale - 1.f) < 0.5f)
       || !(fabsf(s.voct_offset) < 12.f))
    {
        s.voct_scale  = kVoctNominalScale;
        s.voct_offset = kVoctNominalOffset;
    }
    return s;
}

int main(void)
{
    hw.Init();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(kBlockSize);

    // Controls einschwingen lassen und pruefen, ob der Taster gehalten wird
    Switch boot_button;
    boot_button.Init(patch_init::BUTTON);
    for(int i = 0; i < 100; i++)
    {
        hw.ProcessAllControls();
        boot_button.Debounce();
        System::Delay(1);
    }
    const bool calibration = boot_button.Pressed();

    stmlib::Random::Seed(System::GetUs()
                         ^ uint32_t(hw.GetAdcValue(patch_init::CV_IN_1) * 1e6f));

    const Settings settings = LoadSettings();

    // Wie .bss im Original: alles mit 0 beginnen, dann Init()
    memset(static_cast<void*>(&part), 0, sizeof(part));
    part.Init(reverb_buffer);
    string_synth.Init(reverb_buffer);
    strummer.Init(0.01f, kBlockRate);

    ui.Init(&hw, settings, calibration);
    cv_scaler.Init(kBlockRate);
    exciter_detector.Init(kBlockRate);
    droid_voices.Init();
    ApplyModelAndPolyphony();
    cpu_meter.Init(rings::kSampleRate, kBlockSize);

    // DroidLink-Empfang: CV 4 = Daten, Gate In 2 = Takt. Laeuft immer mit
    // (Timer-Interrupt, ca. 1 % CPU) und schaltet sich bei gueltigen Rahmen zu.
    droid.Init(hw);
    droid.Start();

#ifdef RINGS_DEBUG_CPU
    hw.StartLog(false);
#endif

    hw.StartAudio(AudioCallback);
    if(!calibration)
        ui.ShowEngine(System::GetNow());

    Settings last_seen       = settings;
    uint32_t last_change     = System::GetNow();
    uint32_t last_poll       = last_change;
    uint32_t last_cpu_report = last_change;

    while(1)
    {
        const uint32_t now = System::GetNow();

        // Einstellungen 2 s nach der letzten Aenderung im QSPI sichern
        if(now - last_poll >= kSettingsPollMs)
        {
            last_poll = now;
            Settings current;
            ui.FillSettings(&current);
            if(current != last_seen)
            {
                last_seen   = current;
                last_change = now;
            }
            else if(now - last_change >= kSettingsSaveDelayMs
                    && current != storage.GetSettings())
            {
                storage.GetSettings() = current;
                storage.Save();
            }
        }

#ifdef RINGS_DEBUG_CPU
        if(now - last_cpu_report >= 1000)
        {
            last_cpu_report             = now;
            const droid_link::Stats st  = droid.GetStats();
            const droid_link::State lnk = droid.GetState();
            hw.PrintLine("CPU avg " FLT_FMT3 "%%  max " FLT_FMT3 "%%  link %d  ok %lu  bad %lu  drift %lu  lost %lu  settle(mV) " FLT_FMT(2),
                         FLT_VAR3(cpu_meter.GetAvgCpuLoad() * 100.f),
                         FLT_VAR3(cpu_meter.GetMaxCpuLoad() * 100.f),
                         lnk.locked ? 1 : 0,
                         (unsigned long)st.frames_ok,
                         (unsigned long)st.frames_bad,
                         (unsigned long)st.frames_drift,
                         (unsigned long)st.sync_losses,
                         FLT_VAR(2, st.settle_error * 1000.f));
            cpu_meter.Reset();
        }

        // Anschlaege sofort ausgeben
        static uint32_t strike_log_read = 0;
        const uint32_t  written         = strike_log_write;
        if(written - strike_log_read > kNumStrikeEvents)
        {
            hw.PrintLine("  (%lu Anschlaege verpasst)",
                         (unsigned long)(written - strike_log_read - kNumStrikeEvents));
            strike_log_read = written - kNumStrikeEvents;
        }
        while(strike_log_read != written)
        {
            const StrikeEvent e = strike_log[strike_log_read % kNumStrikeEvents];
            strike_log_read++;
            if(e.source == SRC_MIDI)
            {
                const float frac  = e.note - roundf(e.note);
                const bool  ghost = e.note < 19.5f || e.note > 100.5f || fabsf(frac) > 0.15f;
                hw.PrintLine("  %lu ms  strike midi  voice %d  note " FLT_FMT(2) "  vel " FLT_FMT(2) "%s",
                             (unsigned long)e.ms,
                             e.voice + 1,
                             FLT_VAR(2, e.note),
                             FLT_VAR(2, e.velocity),
                             ghost ? "  <- GEIST?" : "");
            }
            else
            {
                const char* src = e.source == SRC_BUTTON ? "taster"
                                  : e.source == SRC_GATE ? "strum-gate"
                                                         : "intern";
                hw.PrintLine("  %lu ms  strike %s", (unsigned long)e.ms, src);
            }
        }
#else
        (void)last_cpu_report;
#endif
    }
}
