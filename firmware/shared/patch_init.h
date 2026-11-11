#pragma once
/**
 * Pinbelegung des patch.init()-Moduls auf dem Daisy Patch Submodule.
 *
 * Der Patch SM kennt nur generische Kanaele (CV_1..CV_8, CV_OUT_1/2, B5..B10).
 * Diese Namen machen sichtbar, was auf der Frontplatte des patch.init() liegt.
 *
 *   Knobs 1-4       CV_1..CV_4      GetAdcValue() ->  0..1
 *   CV-Eingaenge    CV_5..CV_8      GetAdcValue() -> -1..1  (= -5V..+5V)
 *   CV-Ausgang      CV_OUT_1 (C10)  WriteCvOut(ch, 0..5 V)
 *   LED             CV_OUT_2 (C1)   WriteCvOut(ch, 0..5 V) = Helligkeit
 *   Taster          B7              daisy::Switch
 *   Kippschalter    B8              daisy::Switch
 *   Gate In 1/2     B10 / B9        hw.gate_in_1 / hw.gate_in_2
 *   Gate Out 1/2    B5 / B6         hw.gate_out_1 / hw.gate_out_2
 *   Audio In/Out    L/R             IN_L, IN_R, OUT_L, OUT_R im Audio-Callback
 */

#include "daisy_patch_sm.h"

namespace patch_init
{
using daisy::patch_sm::DaisyPatchSM;

constexpr int KNOB_1 = daisy::patch_sm::CV_1;
constexpr int KNOB_2 = daisy::patch_sm::CV_2;
constexpr int KNOB_3 = daisy::patch_sm::CV_3;
constexpr int KNOB_4 = daisy::patch_sm::CV_4;

constexpr int CV_IN_1 = daisy::patch_sm::CV_5;
constexpr int CV_IN_2 = daisy::patch_sm::CV_6;
constexpr int CV_IN_3 = daisy::patch_sm::CV_7;
constexpr int CV_IN_4 = daisy::patch_sm::CV_8;

constexpr int CV_OUT = daisy::patch_sm::CV_OUT_1;
constexpr int LED    = daisy::patch_sm::CV_OUT_2;

constexpr daisy::Pin BUTTON = DaisyPatchSM::B7;
constexpr daisy::Pin TOGGLE = DaisyPatchSM::B8;

/** Vollausschlag der CV-Eingaenge: GetAdcValue(CV_IN_x) * CV_IN_VOLTS = Spannung in V */
constexpr float CV_IN_VOLTS = 5.f;

} // namespace patch_init
