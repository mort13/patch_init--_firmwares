#pragma once
/**
 * Bedienung des Rings-Ports auf dem patch.init().
 *
 *   Kippschalter unten = Seite A   FREQUENCY  STRUCTURE  BRIGHTNESS  DAMPING
 *   Kippschalter oben  = Seite B   POSITION   MODEL      POLYPHONY   BREITE
 *
 *   Taster druecken                 Strum (Anschlag von Hand)
 *   Taster 3 s halten               Engine wechseln: Rings <-> Disastrous Peace
 *   Taster beim Einschalten halten  V/Oct-Kalibrierung von CV 1
 *
 * Alle Methoden laufen im Audio-Callback (Blockrate 2 kHz). Die Hauptschleife
 * liest nur FillSettings().
 */

#include <cstdint>

#include "daisy_patch_sm.h"

#include "layered_knob.h"
#include "settings.h"

namespace rings_port
{

using patch_init::LayeredKnob;

class Ui
{
  public:
    enum Layer
    {
        LAYER_A,
        LAYER_B,
    };

    /** calibration = Taster war beim Einschalten gedrueckt */
    void Init(daisy::patch_sm::DaisyPatchSM* hw,
              const Settings&                 settings,
              bool                            calibration);

    /** Einmal pro Block, nach hw.ProcessAllControls() */
    void Process(uint32_t now_ms);

    /**
     * LED-Helligkeit 0..1. level = Ausgangspegel 0..1 (wie CV Out),
     * strum = in diesem Block wurde angeschlagen.
     */
    float Led(uint32_t now_ms, float level, bool strum);

    /** Seite A, Knob-Position 0..1 */
    float frequency() const { return knob_[0].value(LAYER_A); }
    float structure() const { return knob_[1].value(LAYER_A); }
    float brightness() const { return knob_[2].value(LAYER_A); }
    float damping() const { return knob_[3].value(LAYER_A); }
    /** FREQUENCY als Oktavlage -2..+2 (5 Zonen), fuer den DroidLink-Betrieb */
    int frequency_octave() const { return octave_; }

    /** Seite B */
    float position() const { return knob_[PAGE_B_POSITION].value(LAYER_B); }
    float width() const { return knob_[PAGE_B_WIDTH].value(LAYER_B); }
    int   model() const { return model_; }         // 0..5
    int   polyphony() const { return polyphony_; } // 1..4

    bool easter_egg() const { return easter_egg_; }
    /** Taster wurde in diesem Block gedrueckt (nur im Normalbetrieb) */
    bool strum_pressed() const { return strum_pressed_; }
    bool calibrating() const { return mode_ != MODE_NORMAL; }

    /** CV 1 in Halbtoene (kalibriert) */
    float VoctSemitones(float adc) const
    {
        return voct_offset_ + voct_scale_ * adc;
    }

    void FillSettings(Settings* s) const;

    /** Blinkcode der Engine: 1x lang = Rings, 2x lang = Disastrous Peace */
    void ShowEngine(uint32_t now_ms);

    /** Blinkcode "DroidLink verbunden": 3x kurz */
    void ShowDroidLink(uint32_t now_ms);

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
    float       position_[4]; // geglaettete Knob-Position

    int  model_;
    int  polyphony_;
    int  octave_;
    bool easter_egg_;

    Mode     mode_;
    bool     button_was_pressed_;
    bool     button_held_; // gueltiger Druck im Normalbetrieb
    bool     strum_pressed_;
    uint32_t press_time_;
    bool     long_press_fired_;

    float voct_scale_;
    float voct_offset_;
    bool  cal_averaging_;
    int   cal_count_;
    float cal_sum_;
    float cal_1v_;

    BlinkCode blink_;
    uint32_t  strum_flash_until_;
};

} // namespace rings_port
