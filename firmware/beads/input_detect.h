#pragma once
/**
 * Software-Ersatz fuer den NormalizationDetector von Beads.
 *
 * Beads erkennt per Probe-Signal an den Klinkenkontakten, ob eine Buchse
 * gesteckt ist. Der patch.init() hat das nicht, deshalb wird aus dem Signal
 * selbst geschaetzt:
 *
 *  - AudioInputDetector: Ein Audio-Kanal gilt als leer, wenn er laenger als
 *    1 s unter ca. -66 dBFS liegt. Ist genau ein Kanal leer, bekommt er im
 *    GainControl das Signal des anderen (wie die Normalisierung R <- L bei Beads).
 *  - ClockDetector (firmware/shared/clock_detector.h): Der Seed-Eingang gilt
 *    als gesteckt, solange in den letzten max(4 s, 3 x letztes Intervall) eine
 *    steigende Flanke kam.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "beads/dsp/frame.h"
#include "clock_detector.h"

namespace beads_port
{

using patch_init::ClockDetector;

class AudioInputDetector
{
  public:
    void Init(float block_rate)
    {
        hold_blocks_ = uint32_t(block_rate * kHoldTime);
        for(int i = 0; i < 2; i++)
        {
            active_[i]       = false;
            silent_count_[i] = 0;
        }
    }

    void Process(const beads::FloatFrame* in, size_t size)
    {
        float peak[2] = {0.f, 0.f};
        for(size_t i = 0; i < size; i++)
        {
            peak[0] = std::max(peak[0], fabsf(in[i].l));
            peak[1] = std::max(peak[1], fabsf(in[i].r));
        }
        for(int c = 0; c < 2; c++)
        {
            if(peak[c] >= kOnThreshold)
                active_[c] = true;
            if(peak[c] >= kOffThreshold)
                silent_count_[c] = 0;
            else if(silent_count_[c] < hold_blocks_)
                silent_count_[c]++;
            else
                active_[c] = false;
        }
    }

    /** Links leer, rechts belegt -> links bekommt das rechte Signal */
    bool fill_left() const { return !active_[0] && active_[1]; }
    /** Rechts leer, links belegt -> rechts bekommt das linke Signal */
    bool fill_right() const { return !active_[1] && active_[0]; }

  private:
    static constexpr float kOnThreshold  = 0.002f;  // ca. -54 dBFS
    static constexpr float kOffThreshold = 0.0005f; // ca. -66 dBFS
    static constexpr float kHoldTime     = 1.f;     // s

    bool     active_[2];
    uint32_t silent_count_[2];
    uint32_t hold_blocks_;
};

} // namespace beads_port
