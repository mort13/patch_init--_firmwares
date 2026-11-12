/**
 * DroidLink-Monitor: Testfirmware fuer den Empfang von droid/droid_droid.ini.
 *
 * Zeigt, ob die Verbindung DROID -> patch.init() steht, und macht die vier
 * Stimmen als einfachen Sinus-Testton hoerbar (Akkorde, Stimmung, Velocity).
 * Kein Synth - nur zum Pruefen, bevor man den Empfaenger in eine eigene
 * Firmware einbaut.
 *
 *   CV In 4 + Gate In 2   DroidLink vom DROID (O7 = Daten, O8 = Takt)
 *   Audio Out L/R         4 Sinus-Stimmen (Gate -> Huellkurve, Velocity -> Lautstaerke)
 *   Gate Out 1            Gate von Stimme 1
 *   Gate Out 2            mindestens eine Stimme an
 *   CV Out                AUX1 (im Beispiel-Patch: Modwheel) als 0..5 V
 *   LED                   aus             kein Takt
 *                         schnelles Blinken  Takt da, aber kein gueltiger Rahmen
 *                         an (gedimmt)    Verbindung steht, blitzt bei jedem Note-On
 *                         langsames Blinken  Verbindung steht, aber Einschwingfehler
 *                                         -> am DROID [lfo] hz verringern
 *   Knob 1                Lautstaerke
 *   Knob 2                Release 10 ms .. 2 s
 *   USB (seriell)         jede Sekunde Statistik, z. B. mit PuTTY / screen anzeigen
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "patch_init.h"
#include "droid_link_patch_init.h"

using namespace daisy;
using namespace daisy::patch_sm;
using namespace daisysp;
using namespace patch_init;

DaisyPatchSM              hw;
droid_link::PatchInitLink droid;

Oscillator osc[droid_link::kNumVoices];
Adsr       env[droid_link::kNumVoices];
uint32_t   last_note_ons[droid_link::kNumVoices] = {};
uint32_t   flash  = 0; // Callbacks, die die LED noch hell bleibt
uint32_t   blink  = 0; // laufender Zaehler fuers Blinken
float      cb_rate = 1.f;

/** Ab dieser Abweichung (V) zwischen Abtastwert und Slotende ist der Takt zu schnell */
constexpr float kSettleWarn = 0.002f;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();

    const droid_link::State s       = droid.GetState();
    const droid_link::Stats st      = droid.GetStats();
    const float             volume  = hw.GetAdcValue(KNOB_1);
    const float             release = 0.01f + 2.f * hw.GetAdcValue(KNOB_2) * hw.GetAdcValue(KNOB_2);

    bool  gate[droid_link::kNumVoices];
    float amp[droid_link::kNumVoices];
    bool  any = false;
    for(int v = 0; v < droid_link::kNumVoices; v++)
    {
        const droid_link::Voice& vo = s.voice[v];
        if(vo.note_ons != last_note_ons[v])
        {
            last_note_ons[v] = vo.note_ons;
            flash            = uint32_t(0.03f * cb_rate);
        }
        osc[v].SetFreq(mtof(vo.Note()));
        env[v].SetReleaseTime(release);
        gate[v] = vo.gate;
        amp[v]  = 0.1f + 0.9f * vo.velocity;
        any |= vo.gate;
    }

    for(size_t i = 0; i < size; i++)
    {
        float sig = 0.f;
        for(int v = 0; v < droid_link::kNumVoices; v++)
            sig += osc[v].Process() * env[v].Process(gate[v]) * amp[v];
        OUT_L[i] = OUT_R[i] = sig * 0.25f * volume;
    }

    hw.gate_out_1.Write(s.voice[0].gate);
    hw.gate_out_2.Write(any);
    hw.WriteCvOut(CV_OUT, s.AuxUnipolar(0) * 5.f);

    // LED-Status
    blink++;
    const float t = float(blink) / cb_rate; // Sekunden
    float       led;
    if(!st.clock)
        led = 0.f;
    else if(!s.locked)
        led = (int(t * 8.f) & 1) ? 5.f : 0.f;
    else if(st.settle_error > kSettleWarn)
        led = (int(t * 1.5f) & 1) ? 3.f : 0.f;
    else
        led = flash > 0 ? 5.f : 1.f;
    if(flash > 0)
        flash--;
    hw.WriteCvOut(LED, led);
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    cb_rate = hw.AudioCallbackRate();

    for(int v = 0; v < droid_link::kNumVoices; v++)
    {
        osc[v].Init(hw.AudioSampleRate());
        osc[v].SetWaveform(Oscillator::WAVE_SIN);
        osc[v].SetAmp(1.f);
        env[v].Init(hw.AudioSampleRate());
        env[v].SetTime(ADSR_SEG_ATTACK, 0.005f);
        env[v].SetTime(ADSR_SEG_DECAY, 0.1f);
        env[v].SetSustainLevel(0.8f);
    }

    // Default: CV In 4 = Daten, Gate In 2 = Takt (siehe PatchInitLink::Config)
    droid.Init(hw);
    droid.Start();

    hw.StartLog(false);
    hw.StartAudio(AudioCallback);

    while(1)
    {
        System::Delay(1000);
        const droid_link::Stats st = droid.GetStats();
        const droid_link::State s  = droid.GetState();
        hw.PrintLine("lock %d  slots/s " FLT_FMT(1) "  frame %d  ok %u  bad %u  lost %u  "
                     "gain " FLT_FMT(4) "  offset(mV) " FLT_FMT(1) "  settle(mV) " FLT_FMT(2),
                     s.locked ? 1 : 0,
                     FLT_VAR(1, st.slot_hz),
                     st.frame_slots,
                     (unsigned)st.frames_ok,
                     (unsigned)st.frames_bad,
                     (unsigned)st.sync_losses,
                     FLT_VAR(4, st.gain),
                     FLT_VAR(1, st.offset * 1000.f),
                     FLT_VAR(2, st.settle_error * 1000.f));
        for(int v = 0; v < droid_link::kNumVoices; v++)
        {
            const droid_link::Voice& vo = s.voice[v];
            hw.PrintLine("  voice %d  gate %d  note " FLT_FMT(2) "  vel " FLT_FMT(2) "  ons %u",
                         v + 1,
                         vo.gate ? 1 : 0,
                         FLT_VAR(2, vo.Note()),
                         FLT_VAR(2, vo.velocity),
                         (unsigned)vo.note_ons);
        }
    }
}
