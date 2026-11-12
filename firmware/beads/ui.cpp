#include "ui.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "util/VoctCalibration.h"

#include "patch_init.h"

namespace beads_port
{

using daisy::patch_sm::DaisyPatchSM;

namespace
{

constexpr float    kBlockRate        = 4000.f;
constexpr float    kKnobLp           = 0.05f; // wie beads/cv_reader.cc:51-54
constexpr float    kShiftThreshold   = 0.03f; // wie POT_STATE_LOCKING
constexpr uint32_t kLongPressMs      = 2000;
constexpr int      kCalAverageBlocks = 2000; // 0,5 s
constexpr float    kCalScaleTolerance  = 0.1f; // 10 % wie beads/calibrator.h
constexpr float    kCalOffsetTolerance = 3.f;  // Halbtoene (= 250 mV)
constexpr uint32_t kGrainFlashMs     = 25;

const int kKnobChannel[4] = {patch_init::KNOB_1,
                             patch_init::KNOB_2,
                             patch_init::KNOB_3,
                             patch_init::KNOB_4};

/** beads/cv_reader.h, PotChannel::LAW_ATTENUVERTER: -1..+1 mit Totzone in der Mitte */
inline float AttenuverterLaw(float v)
{
    v -= 0.5f;
    return (2.0f / 0.47f) * v * std::max(fabsf(v) - 0.03f, 0.0f);
}

/** beads/cv_reader.h, PotChannel::LAW_DETUNE: +-24 Halbtoene, rastet auf 0/1/3/4/7/12/24 */
inline float DetuneLaw(float v)
{
    v            = std::min(std::max(v, 0.f), 1.f);
    float detune = 2.0f * v - 1.0f;
    float sign   = detune < 0.0f ? -1.0f : 1.0f;
    detune       = detune * sign * 6.0f;
    MAKE_INTEGRAL_FRACTIONAL(detune);

    const float intervals[8] = {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f};

    detune_fractional
        = std::max((detune_fractional - 0.33333333f) * 1.5f, 0.0f);

    const float a = intervals[detune_integral];
    const float b = intervals[detune_integral + 1];
    return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional))
           * sign;
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
        knob_[i].SetValue(LAYER_SHIFT, settings.random[i]);
        press_position_[i] = position_[i];
        shift_engaged_[i]  = false;
        random_[i]         = AttenuverterLaw(settings.random[i]);
    }

    time_filter_.Init(0.001f); // wie beads/cv_reader.cc:54
    time_  = time_filter_.Process(position_[0]);
    pitch_ = DetuneLaw(position_[2]);

    mode_ = calibration ? MODE_CAL_1V : MODE_NORMAL;
    // Ein beim Einschalten gehaltener Taster zaehlt erst nach dem Loslassen.
    button_was_pressed_ = true;
    button_held_        = false;
    press_time_         = 0;
    knob_moved_         = false;
    long_press_fired_   = false;
    freeze_latched_     = false;
    media_              = std::min<uint8_t>(settings.media, 3);

    voct_scale_    = settings.voct_scale;
    voct_offset_   = settings.voct_offset;
    cal_averaging_ = false;
    cal_count_     = 0;
    cal_sum_       = 0.f;
    cal_1v_        = 0.f;

    blink_.count         = 0;
    previous_grain_gate_ = false;
    grain_flash_until_   = 0;
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

    if(mode_ == MODE_NORMAL)
        ProcessButton(now_ms, pressed);
    else
        ProcessCalibration(now_ms, pressed);

    // Kippschalter oben = Pressed() (siehe DaisyExamples HardwareTest) = Seite B
    const int page = toggle_.Pressed() ? LAYER_B : LAYER_A;
    for(int i = 0; i < 4; i++)
    {
        int layer = page;
        if(button_held_)
        {
            // Erst wenn der Knob deutlich bewegt wurde, gehoert er zur
            // Random-Ebene. Bis dahin bleiben alle Werte stehen.
            layer = shift_engaged_[i] ? int(LAYER_SHIFT)
                                      : LayeredKnob::LAYER_FROZEN;
        }
        knob_[i].Process(position_[i], layer);
    }

    time_  = time_filter_.Process(knob_[0].value(LAYER_A));
    pitch_ = DetuneLaw(knob_[2].value(LAYER_A));
    for(int i = 0; i < 4; i++)
        random_[i] = AttenuverterLaw(knob_[i].value(LAYER_SHIFT));
}

void Ui::ProcessButton(uint32_t now_ms, bool pressed)
{
    if(pressed && !button_was_pressed_)
    {
        button_held_      = true;
        press_time_       = now_ms;
        knob_moved_       = false;
        long_press_fired_ = false;
        for(int i = 0; i < 4; i++)
        {
            press_position_[i] = position_[i];
            shift_engaged_[i]  = false;
        }
    }

    if(pressed && button_held_)
    {
        for(int i = 0; i < 4; i++)
        {
            if(!shift_engaged_[i]
               && fabsf(position_[i] - press_position_[i]) > kShiftThreshold)
            {
                shift_engaged_[i] = true;
                knob_moved_       = true;
            }
        }
        if(!knob_moved_ && !long_press_fired_
           && now_ms - press_time_ >= kLongPressMs)
        {
            long_press_fired_ = true;
            // Reihenfolge wie beads/ui.cc:417: Digital -> Vintage -> Clean Tape -> Dirty Tape
            media_ = (media_ + 3) % 4;
            ShowMedia(now_ms);
        }
    }
    else if(!pressed && button_held_)
    {
        if(!knob_moved_ && !long_press_fired_)
            freeze_latched_ = !freeze_latched_;
        button_held_ = false;
    }

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

    cal_sum_ += hw_->GetAdcValue(patch_init::CV_IN_3);
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

void Ui::ShowMedia(uint32_t now_ms)
{
    // Digital = 1x, Vintage Digital = 2x, Clean Tape = 3x, Dirty Tape = 4x
    StartBlink(now_ms, 4 - media_, 150, 200);
}

float Ui::Led(uint32_t now_ms, bool frozen, bool grain_gate)
{
    if(grain_gate && !previous_grain_gate_)
        grain_flash_until_ = now_ms + kGrainFlashMs;
    previous_grain_gate_ = grain_gate;

    if(mode_ != MODE_NORMAL)
    {
        if(cal_averaging_)
            return 1.f;
        const uint32_t half = mode_ == MODE_CAL_1V ? 500 : 125;
        return ((now_ms / half) & 1) ? 0.f : 1.f;
    }

    if(blink_.count > 0)
    {
        constexpr uint32_t kPause  = 250; // dunkel vor und nach dem Code
        const uint32_t     period  = blink_.on_ms + blink_.off_ms;
        const uint32_t     length  = period * uint32_t(blink_.count);
        uint32_t           t       = now_ms - blink_.start;
        if(t < kPause)
            return 0.f;
        t -= kPause;
        if(t < length)
            return (t % period) < blink_.on_ms ? 1.f : 0.f;
        if(t < length + kPause)
            return 0.f;
        blink_.count = 0;
    }

    if(frozen)
        return 1.f;
    return int32_t(grain_flash_until_ - now_ms) > 0 ? 0.5f : 0.f;
}

void Ui::FillSettings(Settings* s) const
{
    s->version = Settings::kVersion;
    s->media   = media_;
    for(int i = 0; i < 4; i++)
    {
        s->page_b[i] = knob_[i].value(LAYER_B);
        s->random[i] = knob_[i].value(LAYER_SHIFT);
    }
    s->voct_scale  = voct_scale_;
    s->voct_offset = voct_offset_;
}

} // namespace beads_port
