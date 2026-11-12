#pragma once
/**
 * Bedienung des Beads-Ports auf dem patch.init().
 *
 *   Kippschalter unten = Seite A   TIME      SIZE      PITCH     DENSITY
 *   Kippschalter oben  = Seite B   FEEDBACK  DRY/WET   REVERB    SHAPE
 *   Taster halten + Knob drehen    TIME-Rnd  SIZE-Rnd  PITCH-Rnd SHAPE-Rnd
 *
 *   Taster kurz (ohne Knob-Bewegung)  FREEZE an/aus
 *   Taster 2 s halten (ohne Knob)     naechste Quality-Stufe, LED blinkt 1-4x
 *   Taster beim Einschalten halten    V/Oct-Kalibrierung von CV 3
 *
 * Alle Methoden laufen im Audio-Callback (Blockrate 4 kHz). Die Hauptschleife
 * liest nur media() und FillSettings().
 */

#include <cstdint>

#include "daisy_patch_sm.h"
#include "stmlib/dsp/hysteresis_filter.h"

#include "layered_knob.h"
#include "settings.h"

namespace beads_port
{

using patch_init::LayeredKnob;

class Ui
{
  public:
    enum Layer
    {
        LAYER_A,
        LAYER_B,
        LAYER_SHIFT,
    };

    /** calibration = Taster war beim Einschalten gedrueckt */
    void Init(daisy::patch_sm::DaisyPatchSM* hw,
              const Settings&                 settings,
              bool                            calibration);

    /** Einmal pro Block, nach hw.ProcessAllControls() */
    void Process(uint32_t now_ms);

    /** LED-Helligkeit 0..1 */
    float Led(uint32_t now_ms, bool frozen, bool grain_gate);

    /** Seite A, mit Beads-Kennlinien */
    float time() const { return time_; }
    float size() const { return knob_[1].value(LAYER_A); }
    float pitch() const { return pitch_; } // Halbtoene, +-24
    float density() const { return knob_[3].value(LAYER_A); }

    /** Seite B */
    float page_b(PageBParameter p) const { return knob_[p].value(LAYER_B); }

    /** Random-Ebene, Attenuverter-Kennlinie: -1..+1, Mitte = aus */
    float random_amount(RandomParameter p) const { return random_[p]; }

    bool    freeze_latched() const { return freeze_latched_; }
    uint8_t media() const { return media_; }
    bool    calibrating() const { return mode_ != MODE_NORMAL; }

    /** CV 3 in Halbtoene (kalibriert) */
    float VoctSemitones(float adc) const
    {
        return voct_offset_ + voct_scale_ * adc;
    }

    void FillSettings(Settings* s) const;

    /** Blinkcode der aktuellen Quality-Stufe anzeigen (1-4x) */
    void ShowMedia(uint32_t now_ms);

  private:
    enum Mode
    {
        MODE_NORMAL,
        MODE_CAL_1V,
        MODE_CAL_3V,
    };

    struct BlinkCode
    {
        uint32_t start;
        int      count;
        uint32_t on_ms;
        uint32_t off_ms;
    };

    void ProcessButton(uint32_t now_ms, bool pressed);
    void ProcessCalibration(uint32_t now_ms, bool pressed);
    void FinishCalibration(uint32_t now_ms, float v1, float v3);
    void StartBlink(uint32_t now_ms, int count, uint32_t on_ms, uint32_t off_ms);

    daisy::patch_sm::DaisyPatchSM* hw_;
    daisy::Switch                  button_;
    daisy::Switch                  toggle_;

    LayeredKnob knob_[4];
    float       position_[4];  // geglaettete Knob-Position
    float       press_position_[4];
    bool        shift_engaged_[4];

    stmlib::HysteresisFilter time_filter_;
    float                    time_;
    float                    pitch_;
    float                    random_[4];

    Mode     mode_;
    bool     button_was_pressed_;
    bool     button_held_; // gueltiger Druck im Normalbetrieb
    uint32_t press_time_;
    bool     knob_moved_;
    bool     long_press_fired_;
    bool     freeze_latched_;
    uint8_t  media_;

    float voct_scale_;
    float voct_offset_;
    bool  cal_averaging_;
    int   cal_count_;
    float cal_sum_;
    float cal_1v_;

    BlinkCode blink_;
    bool      previous_grain_gate_;
    uint32_t  grain_flash_until_;
};

} // namespace beads_port
