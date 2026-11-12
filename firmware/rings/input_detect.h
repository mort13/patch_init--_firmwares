#pragma once
/**
 * Ersatz fuer den NormalizationDetector am IN-Eingang von Rings.
 *
 * Ist IN nicht gesteckt, regt Rings die Resonatoren bei jedem Strum selbst an
 * (interner Exciter). Der patch.init() erkennt nicht, ob eine Buchse gesteckt
 * ist. Deshalb gilt der Audio-Eingang als belegt, sobald er ueber ca.
 * -54 dBFS ausschlaegt, und wieder als leer, wenn er 2 s lang unter ca.
 * -66 dBFS bleibt (Schwellen wie im Beads-Port).
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rings_port
{

class ExciterDetector
{
  public:
    void Init(float block_rate)
    {
        hold_blocks_  = uint32_t(block_rate * kHoldTime);
        silent_count_ = hold_blocks_;
        active_       = false;
    }

    void Process(const float* in, size_t size)
    {
        float peak = 0.f;
        for(size_t i = 0; i < size; i++)
            peak = std::max(peak, fabsf(in[i]));

        if(peak >= kOnThreshold)
            active_ = true;
        if(peak >= kOffThreshold)
            silent_count_ = 0;
        else if(silent_count_ < hold_blocks_)
            silent_count_++;
        else
            active_ = false;
    }

    /** true = Audio-Eingang belegt, externer Exciter */
    bool active() const { return active_; }

  private:
    static constexpr float kOnThreshold  = 0.002f;  // ca. -54 dBFS
    static constexpr float kOffThreshold = 0.0005f; // ca. -66 dBFS
    static constexpr float kHoldTime     = 2.f;     // s

    bool     active_;
    uint32_t silent_count_;
    uint32_t hold_blocks_;
};

} // namespace rings_port
