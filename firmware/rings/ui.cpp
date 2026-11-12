#include "ui.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "util/VoctCalibration.h"

#include "patch_init.h"

namespace rings_port
{

using daisy::patch_sm::DaisyPatchSM;

namespace
{

constexpr float    kBlockRate          = 2000.f; // 48 kHz / 24
constexpr float    kKnobLp             = 0.01f;  // wie rings/cv_scaler.cc (POT_*)
constexpr float    kZoneHysteresis     = 0.1f;   // Anteil einer Zonenbreite
constexpr uint32_t kLongPressMs        = 3000;   // wie rings/ui.cc
constexpr int      kCalAverageBlocks   = 1000;   // 0,5 s
constexpr float    kCalScaleTolerance  = 0.1f;   // 10 %
constexpr float    kCalOffsetTolerance = 3.f;    // Halbtoene (= 250 mV)
constexpr uint32_t kStrumFlashMs       = 20;
constexpr int      kNumOctaves         = 5; // FREQUENCY im DroidLink-Betrieb: -2..+2

const int kKnobChannel[4] = {patch_init::KNOB_1,
                             patch_init::KNOB_2,
                             patch_init::KNOB_3,
                             patch_init::KNOB_4};

/**
 * Knob-Position 0..1 -> Zone 0..zones-1. Eine neue Zone gilt erst, wenn der
 * Knob ein Stueck ueber die Grenze hinaus gedreht wurde (kein Flackern).
 */
int Zone(float value, int zones, int current)
{
    const float x = value * float(zones);
    if(x < float(current) - kZoneHysteresis
       || x > float(current + 1) + kZoneHysteresis)
    {
        current = std::min(std::max(int(x), 0), zones - 1);
    }
    return current;
}

} // namespace

void Ui::Init(DaisyPatchSM* hw, const Settings& settings, bool calibration)
{
    hw_ = hw;
    button_.Init(patch_init::BUTTON, kBlockRate);
    toggle_.Init(patch_init::TOGGLE, kBlockRate);

    for(int i = 0; i < 4; i++)
    {
        position_[i] = hw_->GetAdcValue(kKnobChannel[i]);
        knob_[i].Init(position_[i]);
        knob_[i].SetValue(LAYER_B, settings.page_b[i]);
    }

    model_      = Zone(settings.page_b[PAGE_B_MODEL], kNumModels, -10);
    polyphony_  = Zone(settings.page_b[PAGE_B_POLYPHONY], kMaxPolyphony, -10) + 1;
    octave_     = Zone(position_[0], kNumOctaves, -10) - kNumOctaves / 2;
    easter_egg_ = settings.easter_egg != 0;

    mode_ = calibration ? MODE_CAL_1V : MODE_NORMAL;
    // Ein beim Einschalten gehaltener Taster zaehlt erst nach dem Loslassen.
    button_was_pressed_ = true;
    button_held_        = false;
    strum_pressed_      = false;
    press_time_         = 0;
    long_press_fired_   = false;

    voct_scale_    = settings.voct_scale;
    voct_offset_   = settings.voct_offset;
    cal_averaging_ = false;
    cal_count_     = 0;
    cal_sum_       = 0.f;
    cal_1v_        = 0.f;

    blink_.count       = 0;
    strum_flash_until_ = 0;
}

void Ui::Process(uint32_t now_ms)
{
    button_.Debounce();
    toggle_.Debounce();
    const bool pressed = button_.Pressed();

    for(int i = 0; i < 4; i++)
    {
        ONE_POLE(position_[i], hw_->GetAdcValue(kKnobChannel[i]), kKnobLp);
    }

    strum_pressed_ = false;
    if(mode_ == MODE_NORMAL)
        ProcessButton(now_ms, pressed);
    else
        ProcessCalibration(now_ms, pressed);

    // Kippschalter oben = Pressed() (siehe DaisyExamples HardwareTest) = Seite B
    const int page = toggle_.Pressed() ? LAYER_B : LAYER_A;
    for(int i = 0; i < 4; i++)
        knob_[i].Process(position_[i], page);

    const int model = Zone(knob_[PAGE_B_MODEL].value(LAYER_B), kNumModels, model_);
    if(model != model_)
    {
        model_ = model;
        StartBlink(now_ms, model_ + 1, 150, 200);
    }
    const int polyphony
        = Zone(knob_[PAGE_B_POLYPHONY].value(LAYER_B), kMaxPolyphony, polyphony_ - 1)
          + 1;
    if(polyphony != polyphony_)
    {
        polyphony_ = polyphony;
        StartBlink(now_ms, polyphony_, 150, 200);
    }

    octave_ = Zone(frequency(), kNumOctaves, octave_ + kNumOctaves / 2)
              - kNumOctaves / 2;
}

void Ui::ProcessButton(uint32_t now_ms, bool pressed)
{
    if(pressed && !button_was_pressed_)
    {
        button_held_      = true;
        press_time_       = now_ms;
        long_press_fired_ = false;
        strum_pressed_    = true;
    }

    if(pressed && button_held_ && !long_press_fired_
       && now_ms - press_time_ >= kLongPressMs)
    {
        long_press_fired_ = true;
        easter_egg_       = !easter_egg_;
        ShowEngine(now_ms);
    }

    if(!pressed)
        button_held_ = false;
    button_was_pressed_ = pressed;
}

void Ui::ProcessCalibration(uint32_t now_ms, bool pressed)
{
    const bool press_edge = pressed && !button_was_pressed_;
    button_was_pressed_   = pressed;

    if(!cal_averaging_)
    {
        if(press_edge)
        {
            cal_averaging_ = true;
            cal_count_     = 0;
            cal_sum_       = 0.f;
        }
        return;
    }

    cal_sum_ += hw_->GetAdcValue(patch_init::CV_IN_1);
    if(++cal_count_ < kCalAverageBlocks)
        return;

    cal_averaging_ = false;
    const float v  = cal_sum_ / float(kCalAverageBlocks);
    if(mode_ == MODE_CAL_1V)
    {
        cal_1v_ = v;
        mode_   = MODE_CAL_3V;
    }
    else
    {
        FinishCalibration(now_ms, cal_1v_, v);
    }
}

void Ui::FinishCalibration(uint32_t now_ms, float v1, float v3)
{
    daisy::VoctCalibration cal;
    cal.Record(v1, v3);
    float scale, offset;
    cal.GetData(scale, offset);

    // Bei Division durch 0 entstehen inf/NaN, dann schlagen beide Vergleiche fehl.
    const bool ok
        = fabsf(scale / kVoctNominalScale - 1.f) < kCalScaleTolerance
          && fabsf(offset - kVoctNominalOffset) < kCalOffsetTolerance;
    if(ok)
    {
        voct_scale_  = scale;
        voct_offset_ = offset;
        StartBlink(now_ms, 1, 1000, 300); // einmal lang = gespeichert
    }
    else
    {
        StartBlink(now_ms, 10, 40, 60); // Flackern = verworfen
    }
    mode_ = MODE_NORMAL;
}

void Ui::StartBlink(uint32_t now_ms, int count, uint32_t on_ms, uint32_t off_ms)
{
    blink_.start  = now_ms;
    blink_.count  = count;
    blink_.on_ms  = on_ms;
    blink_.off_ms = off_ms;
}

void Ui::ShowEngine(uint32_t now_ms)
{
    StartBlink(now_ms, easter_egg_ ? 2 : 1, 600, 300);
}

void Ui::ShowDroidLink(uint32_t now_ms)
{
    StartBlink(now_ms, 3, 60, 100);
}

float Ui::Led(uint32_t now_ms, float level, bool strum)
{
    if(strum)
        strum_flash_until_ = now_ms + kStrumFlashMs;

    if(mode_ != MODE_NORMAL)
    {
        if(cal_averaging_)
            return 1.f;
        const uint32_t half = mode_ == MODE_CAL_1V ? 500 : 125;
        return ((now_ms / half) & 1) ? 0.f : 1.f;
    }

    if(blink_.count > 0)
    {
        constexpr uint32_t kPause = 250; // dunkel vor und nach dem Code
        const uint32_t     period = blink_.on_ms + blink_.off_ms;
        const uint32_t     length = period * uint32_t(blink_.count);
        uint32_t           t      = now_ms - blink_.start;
        if(t < kPause)
            return 0.f;
        t -= kPause;
        if(t < length)
            return (t % period) < blink_.on_ms ? 1.f : 0.f;
        if(t < length + kPause)
            return 0.f;
        blink_.count = 0;
    }

    if(int32_t(strum_flash_until_ - now_ms) > 0)
        return 1.f;
    return 0.5f * level;
}

void Ui::FillSettings(Settings* s) const
{
    s->version    = Settings::kVersion;
    s->easter_egg = easter_egg_ ? 1 : 0;
    for(int i = 0; i < 4; i++)
        s->page_b[i] = knob_[i].value(LAYER_B);
    s->voct_scale  = voct_scale_;
    s->voct_offset = voct_offset_;
}

} // namespace rings_port
