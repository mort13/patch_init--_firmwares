/**
 * Template fuer eigene patch.init()-Firmwares.
 *
 * Nutzt jeden Ein- und Ausgang einmal, damit man sieht, wie er angesprochen wird:
 *   - Audio L/R wird durchgereicht, Knob 1 + CV In 1 regeln die Lautstaerke (VCA)
 *   - Knob 2 setzt die Spannung am CV-Ausgang (0..5 V)
 *   - LED zeigt die aktuelle Lautstaerke
 *   - Gate Out 1 = Gate In 1 ODER Taster, Gate Out 2 = Kippschalter
 *
 * Neue Firmware anlegen:  make new NAME=meine_firmware   (im Repo-Root)
 */

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "patch_init.h"

using namespace daisy;
using namespace daisy::patch_sm;
using namespace daisysp;
using namespace patch_init;

DaisyPatchSM hw;
Switch       button;
Switch       toggle;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();
    button.Debounce();
    toggle.Debounce();

    // Knobs liefern 0..1, CV-Eingaenge -1..1 (entspricht -5..+5 V)
    const float gain = fclamp(hw.GetAdcValue(KNOB_1) + hw.GetAdcValue(CV_IN_1), 0.f, 1.f);

    for(size_t i = 0; i < size; i++)
    {
        OUT_L[i] = IN_L[i] * gain;
        OUT_R[i] = IN_R[i] * gain;
    }

    hw.WriteCvOut(CV_OUT, hw.GetAdcValue(KNOB_2) * 5.f);
    hw.WriteCvOut(LED, gain * 5.f);

    hw.gate_out_1.Write(hw.gate_in_1.State() || button.Pressed());
    hw.gate_out_2.Write(toggle.Pressed());
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    button.Init(BUTTON, hw.AudioCallbackRate());
    toggle.Init(TOGGLE, hw.AudioCallbackRate());

    hw.StartAudio(AudioCallback);

    while(1) {}
}
