/**
 * Mutable Instruments Beads fuer den patch.init().
 *
 * Der Granular-Prozessor (beads/dsp/) stammt unveraendert bis auf drei kleine
 * Patches aus pichenettes/eurorack (MIT, Emilie Gillet), siehe README.md.
 * Diese Datei ersetzt beads.cc: Codec, ADC-Treiber, Normalisierungs-Erkennung
 * und Flash-Settings des Originals werden durch libDaisy und Heuristiken
 * ersetzt.
 *
 *   Audio In L/R  -> Beads In (leerer Kanal bekommt das Signal des anderen)
 *   CV 1..4       -> TIME, SIZE, PITCH (1 V/Oct), DENSITY
 *   Gate In 1     -> FREEZE (ODER Taster-Latch)
 *   Gate In 2     -> SEED / Clock
 *   Gate Out 1    -> Grain-Gate        CV Out -> Zufallsspannung pro Grain (0..5 V)
 *   Gate Out 2    -> Freeze-Status     LED    -> Freeze / Grain-Blitz / Blinkcodes
 *
 * Bedienung: siehe ui.h
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "daisy_patch_sm.h"
#include "util/CpuLoadMeter.h"
#include "util/PersistentStorage.h"

#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"

#include "beads/dsp/processor.h"
#include "beads/gain_control.h"

#include "input_detect.h"
#include "patch_init.h"
#include "settings.h"
#include "ui.h"

// CPU-Last jede Sekunde ueber USB-Seriell ausgeben (Terminal auf den COM-Port des Daisy)
// #define BEADS_DEBUG_CPU

using namespace daisy;
using namespace daisy::patch_sm;
using namespace beads;
using namespace beads_port;
using stmlib::GateFlags;

namespace
{

/** Beads rechnet mit 4 kHz Blockrate (beads.cc:50): 12 Frames bei 48 kHz, 8 bei 32 kHz */
constexpr float  kBlockRate    = 4000.f;
constexpr size_t kMaxBlockSize = 12;

/** Offset im 8-MB-QSPI-Flash. Die App liegt ab 0x40000 und ist < 480 KB gross. */
constexpr uint32_t kSettingsOffset      = 0x7F0000;
constexpr uint32_t kSettingsSaveDelayMs = 2000;
constexpr uint32_t kSettingsPollMs      = 100;

/** Seed um 2,5 ms verzoegern, damit eine mitgeschickte CV anliegt (beads.cc:146) */
constexpr int kSeedDelayBlocks = 10;

/** Nach jedem Codec-Start 250 ms stumm (beads.cc:292) */
constexpr int kTransitionBlocks = int(kBlockRate / 4);

/** Totzone gegen Rauschen offener CV-Eingaenge, +-1 = +-5 V */
constexpr float kCvDeadband = 0.01f;

/**
 * SIZE ab 0,99 schaltet in den Delay-Modus, zurueck unter 0,98 (processor.cc:113).
 * Das soll nur der Knob koennen: Mit CV wird SIZE unterhalb begrenzt.
 */
constexpr float kSizeDelayZone = 0.98f;
constexpr float kSizeCvMax     = 0.975f;

inline float Deadband(float x)
{
    if(x > kCvDeadband)
        return x - kCvDeadband;
    if(x < -kCvDeadband)
        return x + kCvDeadband;
    return 0.f;
}

inline float Clamp(float x, float lo, float hi)
{
    return std::min(std::max(x, lo), hi);
}

} // namespace

DaisyPatchSM                hw;
PersistentStorage<Settings> storage(hw.qspi);

// Beads braucht 3 Baenke a 256 KiB (beads/dsp/delay_line.h) und 64 KiB Reverb.
// Im Original liegen sie im internen SRAM, hier im SDRAM (mit D-Cache).
DSY_SDRAM_BSS uint8_t  delay_bank_0[256 * 1024];
DSY_SDRAM_BSS uint8_t  delay_bank_1[256 * 1024];
DSY_SDRAM_BSS uint8_t  delay_bank_2[256 * 1024];
DSY_SDRAM_BSS uint16_t reverb_buffer[32768];

Processor          processor;
GainControl        gain_control;
Ui                 ui;
AudioInputDetector input_detector;
ClockDetector      seed_detector;
CpuLoadMeter       cpu_meter;

volatile int transition_blocks = kTransitionBlocks;

bool      seed_delay[kSeedDelayBlocks];
int       seed_delay_index    = 0;
GateFlags previous_seed_flags = stmlib::GATE_FLAG_LOW;
bool      previous_grain_gate = false;

void UpdateOutputs(uint32_t now, bool frozen, bool grain_gate)
{
    hw.gate_out_1.Write(grain_gate);
    if(grain_gate && !previous_grain_gate)
        hw.WriteCvOut(patch_init::CV_OUT, stmlib::Random::GetFloat() * 5.f);
    previous_grain_gate = grain_gate;

    hw.gate_out_2.Write(frozen);
    hw.WriteCvOut(patch_init::LED, ui.Led(now, frozen, grain_gate) * 5.f);
}

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    cpu_meter.OnBlockStart();

    // Interleaved {L0, R0, L1, R1, ...} entspricht exakt FloatFrame[]
    const size_t      frames = std::min(size / 2, kMaxBlockSize);
    const FloatFrame* input  = reinterpret_cast<const FloatFrame*>(in);
    FloatFrame*       output = reinterpret_cast<FloatFrame*>(out);

    hw.ProcessAllControls();
    const uint32_t now = System::GetNow();
    ui.Process(now);

    // Freeze: Taster-Latch ODER Gate In 1 (beads.cc:135)
    const bool frozen = ui.freeze_latched() || hw.gate_in_1.State();
    bool       freeze[kMaxBlockSize];
    std::fill(&freeze[0], &freeze[frames], frozen);

    // Seed / Clock an Gate In 2
    const bool seed_now = hw.gate_in_2.State();
    seed_detector.Process(seed_now);
    const bool seed_gate           = seed_delay[seed_delay_index];
    seed_delay[seed_delay_index]   = seed_now;
    seed_delay_index               = (seed_delay_index + 1) % kSeedDelayBlocks;
    GateFlags        seed_flags[kMaxBlockSize];
    const GateFlags* seed_input = nullptr;
    if(seed_detector.active())
    {
        for(size_t i = 0; i < frames; i++)
            seed_flags[i] = previous_seed_flags
                = stmlib::ExtractGateFlags(previous_seed_flags, seed_gate);
        seed_input = seed_flags;
    }
    else
    {
        previous_seed_flags = stmlib::GATE_FLAG_LOW;
    }

    // Eingang: Auto-Gain und Fuellen eines leeren Kanals
    input_detector.Process(input, frames);
    FloatFrame float_in[kMaxBlockSize];
    gain_control.Process(transition_blocks == 0,
                         0, // 0 = automatische Pegelanpassung
                         input_detector.fill_left(),
                         input_detector.fill_right(),
                         input,
                         float_in,
                         frames);

    if(transition_blocks > 0)
    {
        --transition_blocks;
        std::fill(&out[0], &out[size], 0.f);
        UpdateOutputs(now, frozen, false);
        cpu_meter.OnBlockEnd();
        return;
    }

    // CV-Eingaenge werden immer fest addiert (has_cv = false), damit die
    // Random-Ebene wie ein ungepatchter Beads-Attenuverter die interne
    // Zufallsmodulation regelt. Vorbild: beads/dsp/synth.cc:66-67.
    Parameters p;
    p.time.base         = ui.time() + Deadband(hw.GetAdcValue(patch_init::CV_IN_1));
    p.time.attenuverter = ui.random_amount(RANDOM_TIME);
    p.time.cv           = 0.f;
    p.time.has_cv       = false;

    const float size_knob = ui.size();
    p.size.base           = size_knob >= kSizeDelayZone
                                ? size_knob
                                : std::min(size_knob
                                     + Deadband(hw.GetAdcValue(patch_init::CV_IN_2)),
                                           kSizeCvMax);
    p.size.attenuverter = ui.random_amount(RANDOM_SIZE);
    p.size.cv           = 0.f;
    p.size.has_cv       = false;

    p.shape.base         = ui.page_b(PAGE_B_SHAPE);
    p.shape.attenuverter = ui.random_amount(RANDOM_SHAPE);
    p.shape.cv           = 0.f;
    p.shape.has_cv       = false;

    p.pitch.base
        = ui.pitch()
          + Clamp(ui.VoctSemitones(hw.GetAdcValue(patch_init::CV_IN_3)), -60.f, 60.f);
    p.pitch.attenuverter = ui.random_amount(RANDOM_PITCH);
    p.pitch.cv           = 0.f;
    p.pitch.has_cv       = false;

    p.monophonic_mode = false;

    ModulatedParameter density;
    density.base = ui.density();
    density.cv   = Deadband(hw.GetAdcValue(patch_init::CV_IN_4));

    // Mix-Werte wie beads.cc:221-227
    Processor::MixParameters m;
    m.feedback      = Clamp(ui.page_b(PAGE_B_FEEDBACK) * 1.005f, 0.f, 1.f);
    m.dry_wet       = Clamp(ui.page_b(PAGE_B_DRY_WET) * 1.005f, 0.f, 1.f);
    m.reverb_amount = Clamp(ui.page_b(PAGE_B_REVERB) * 1.005f, 0.f, 1.f);
    m.output_gate   = false; // Gate liegt am echten Gate Out 1

    processor.Process(SEED_BUTTON_STATE_LOCKED,
                      seed_input,
                      freeze,
                      density,
                      p,
                      m,
                      float_in,
                      output,
                      frames);

    for(size_t i = 0; i < frames; i++)
    {
        output[i].l = Clamp(output[i].l, -1.f, 0.999999f);
        output[i].r = Clamp(output[i].r, -1.f, 0.999999f);
    }

    UpdateOutputs(now, frozen, processor.gate());
    cpu_meter.OnBlockEnd();
}

/** Codec fuer eine Quality-Stufe einrichten und starten (beads.cc:276-294) */
void ConfigureAndStart(Processor::Media media, bool reset_gain_control)
{
    processor.Configure(media, false); // immer Stereo-Puffer
    const float sr = processor.sample_rate(); // 48 kHz oder 32 kHz
    hw.SetAudioSampleRate(sr);
    hw.SetAudioBlockSize(size_t(sr / kBlockRate));

    gain_control.Init(sr,
                      media == Processor::MEDIA_DIRTY_TAPE
                          ? GainControl::SOFT_CLIPPING_MODE_ON
                          : (media == Processor::MEDIA_CLEAN_TAPE
                                 ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                                 : GainControl::SOFT_CLIPPING_MODE_OFF),
                      reset_gain_control);
    cpu_meter.Init(sr, hw.AudioBlockSize());

    transition_blocks = kTransitionBlocks;
    hw.StartAudio(AudioCallback);
}

Settings LoadSettings()
{
    storage.Init(DefaultSettings(), kSettingsOffset);
    if(storage.GetSettings().version != Settings::kVersion)
        storage.RestoreDefaults();

    Settings s = storage.GetSettings();
    s.media    = std::min<uint8_t>(s.media, 3);
    for(int i = 0; i < 4; i++)
    {
        if(!(s.page_b[i] >= 0.f && s.page_b[i] <= 1.f))
            s.page_b[i] = DefaultSettings().page_b[i];
        if(!(s.random[i] >= 0.f && s.random[i] <= 1.f))
            s.random[i] = 0.5f;
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
    hw.SetAudioBlockSize(size_t(48000.f / kBlockRate));

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

    memset(delay_bank_0, 0, sizeof(delay_bank_0));
    memset(delay_bank_1, 0, sizeof(delay_bank_1));
    memset(delay_bank_2, 0, sizeof(delay_bank_2));
    memset(reverb_buffer, 0, sizeof(reverb_buffer));
    uint8_t* banks[3] = {delay_bank_0, delay_bank_1, delay_bank_2};
    processor.Init(banks, reverb_buffer);

    ui.Init(&hw, settings, calibration);
    input_detector.Init(kBlockRate);
    seed_detector.Init(kBlockRate);
    std::fill(&seed_delay[0], &seed_delay[kSeedDelayBlocks], false);

#ifdef BEADS_DEBUG_CPU
    hw.StartLog(false);
#endif

    ConfigureAndStart(Processor::Media(ui.media()), true);
    if(!calibration)
        ui.ShowMedia(System::GetNow());

    Settings last_seen       = settings;
    uint32_t last_change     = System::GetNow();
    uint32_t last_poll       = last_change;
    uint32_t last_cpu_report = last_change;

    while(1)
    {
        const uint32_t now = System::GetNow();

        // Quality-Wechsel. Solange Freeze aktiv ist, wird er aufgeschoben,
        // weil Configure() den Puffer loescht (wie beads.cc:352).
        const Processor::Media media  = Processor::Media(ui.media());
        const bool             frozen = ui.freeze_latched() || hw.gate_in_1.State();
        if(media != processor.media() && !frozen && !ui.calibrating())
        {
            hw.StopAudio();
            ConfigureAndStart(media, false);
        }

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

#ifdef BEADS_DEBUG_CPU
        if(now - last_cpu_report >= 1000)
        {
            last_cpu_report = now;
            hw.PrintLine("CPU avg " FLT_FMT3 "%%  max " FLT_FMT3 "%%",
                         FLT_VAR3(cpu_meter.GetAvgCpuLoad() * 100.f),
                         FLT_VAR3(cpu_meter.GetMaxCpuLoad() * 100.f));
            cpu_meter.Reset();
        }
#else
        (void)last_cpu_report;
#endif
    }
}
