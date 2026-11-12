#pragma once
/**
 * Ersatz fuer rings/cv_scaler.cc: Knobs + CV-Eingaenge -> rings::Patch und
 * rings::PerformanceState.
 *
 *   CV 1  V/OCT (kalibrierbar)      CV 3  BRIGHTNESS
 *   CV 2  STRUCTURE                 CV 4  DAMPING
 *   Gate In 1  STRUM
 *
 * Rings erkennt per Probe-Signal an den Klinkenkontakten, ob STRUM, V/OCT und
 * IN gesteckt sind, und schaltet dann auf interne Anschlaege, freie Stimmung
 * bzw. den internen Exciter um. Das patch.init() hat keine Schaltkontakte,
 * deshalb wird aus den Signalen geschaetzt:
 *
 *  - STRUM gesteckt: in den letzten max(4 s, 3 x letztes Intervall) kam eine
 *    Flanke an Gate In 1 (patch_init::ClockDetector).
 *  - V/OCT gesteckt: CV 1 lag in den letzten 4 s mindestens einmal ueber
 *    +-50 mV.
 *  - IN gesteckt: siehe ExciterDetector in input_detect.h.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stmlib/dsp/dsp.h"

#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"

#include "clock_detector.h"
#include "ui.h"

namespace rings_port
{

class CvScaler
{
  public:
    void Init(float block_rate)
    {
        std::fill(&cv_lp_[0], &cv_lp_[3], 0.f);
        transpose_       = 0.f;
        chord_           = 0;
        quantize_tonic_  = false;
        frequency_ref_   = 0.f;
        voct_hold_       = uint32_t(block_rate * kVoctHoldTime);
        voct_quiet_      = voct_hold_;
        previous_strum_  = false;
        strum_detector_.Init(block_rate);
    }

    /**
     * cv[0..3] = GetAdcValue(CV_IN_1..4), -1..1 = -5..+5 V. internal_exciter
     * wird nicht gesetzt, das macht der Aufrufer (ExciterDetector).
     */
    void Read(const Ui&                ui,
              const float              cv[4],
              bool                     strum_gate,
              rings::Patch*            patch,
              rings::PerformanceState* performance_state)
    {
        // STRUCTURE, BRIGHTNESS, DAMPING: Knob + CV, +-5 V = voller Knob-Weg.
        // Tiefpaesse wie channel_settings_ in rings/cv_scaler.cc (2 kHz).
        const float kCvLp[3] = {0.1f, 0.1f, 0.05f};
        for(int i = 0; i < 3; i++)
            ONE_POLE(cv_lp_[i], Deadband(cv[i + 1]), kCvLp[i]);

        patch->structure  = Clamp(ui.structure() + cv_lp_[0], 0.f, 0.9995f);
        patch->brightness = Clamp(ui.brightness() + cv_lp_[1], 0.f, 1.f);
        patch->damping    = Clamp(ui.damping() + cv_lp_[2], 0.f, 1.f);
        patch->position   = Clamp(ui.position(), 0.f, 1.f);

        // V/OCT. Das Original setzt die Note ohne Kabel auf 0; hier bleibt sie
        // immer der Messwert, damit beim Umschalten der Erkennung nichts springt.
        if(fabsf(cv[0]) > kVoctThreshold)
            voct_quiet_ = 0;
        else if(voct_quiet_ < voct_hold_)
            voct_quiet_++;
        const bool voct_patched = voct_quiet_ < voct_hold_;

        performance_state->note = ui.VoctSemitones(cv[0]);
        performance_state->fm   = 0.f; // kein FM-Eingang

        // FREQUENCY: 5 Oktaven ab C0. Mit V/OCT in Halbtonschritten (mit
        // Hysterese, rings/cv_scaler.cc), ohne stufenlos. Zurueck zu stufenlos
        // erst, wenn der Knob bewegt wird - sonst verstimmte sich eine
        // gehaltene Note, sobald V/OCT laenger auf 0 V steht.
        const float transpose  = 60.f * ui.frequency();
        const float hysteresis = transpose - transpose_ > 0.f ? -0.3f : 0.3f;
        transpose_ = float(int32_t(transpose + hysteresis + 0.5f));
        if(voct_patched)
        {
            quantize_tonic_ = true;
            frequency_ref_  = ui.frequency();
        }
        else if(quantize_tonic_
                && fabsf(ui.frequency() - frequency_ref_) > kFrequencyMoved)
        {
            quantize_tonic_ = false;
        }
        performance_state->tonic
            = 12.f + (quantize_tonic_ ? transpose_ : transpose);

        // STRUM an Gate In 1. Ohne Kabel schlaegt der Strummer selbst an:
        // bei Notenwechseln an V/OCT, sonst bei Transienten am Audio-Eingang.
        strum_detector_.Process(strum_gate);
        performance_state->strum          = strum_gate && !previous_strum_;
        performance_state->internal_strum = !strum_detector_.active();
        performance_state->internal_note  = !voct_patched;
        previous_strum_                   = strum_gate;

        // Akkord fuer die sympathetischen Saiten: ungefilterte CV wie im
        // Original, damit sequenzierte Akkordwechsel sofort greifen.
        float chord = Clamp(ui.structure() + Deadband(cv[1]), 0.f, 1.f)
                      * float(rings::kNumChords - 1);
        const float chord_hysteresis = chord - float(chord_) > 0.f ? -0.1f : 0.1f;
        chord_ = std::min(std::max(int32_t(chord + chord_hysteresis + 0.5f), int32_t(0)),
                          rings::kNumChords - 1);
        performance_state->chord = chord_;
    }

  private:
    /** Totzone gegen Rauschen offener CV-Eingaenge, +-1 = +-5 V */
    static constexpr float kCvDeadband = 0.01f;
    /** 50 mV: unter einem halben Halbton, ueber dem Rauschen eines offenen Eingangs */
    static constexpr float kVoctThreshold  = 0.01f;
    static constexpr float kVoctHoldTime   = 4.f; // s
    static constexpr float kFrequencyMoved = 0.01f;

    static float Deadband(float x)
    {
        if(x > kCvDeadband)
            return x - kCvDeadband;
        if(x < -kCvDeadband)
            return x + kCvDeadband;
        return 0.f;
    }

    static float Clamp(float x, float lo, float hi)
    {
        return std::min(std::max(x, lo), hi);
    }

    float    cv_lp_[3];
    float    transpose_;
    int32_t  chord_;
    bool     quantize_tonic_;
    float    frequency_ref_;
    uint32_t voct_hold_;
    uint32_t voct_quiet_;
    bool     previous_strum_;

    patch_init::ClockDetector strum_detector_;
};

} // namespace rings_port
