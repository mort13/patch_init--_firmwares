#pragma once
/**
 * Schaetzt, ob an einem Gate-Eingang etwas gesteckt ist.
 *
 * Das patch.init() hat keine Schaltkontakte an den Buchsen. Ein Gate-Eingang
 * gilt deshalb als gesteckt, solange in den letzten
 * max(min_timeout, 3 x letztes Flanken-Intervall) eine steigende Flanke kam.
 * Langsame Clocks verlaengern das Fenster also automatisch.
 *
 * Verwendet von firmware/beads (Seed-Eingang) und firmware/rings (Strum-Eingang).
 */

#include <algorithm>
#include <cstdint>

namespace patch_init
{

class ClockDetector
{
  public:
    /** block_rate = Aufrufe von Process() pro Sekunde */
    void Init(float block_rate, float min_timeout_s = 4.f)
    {
        min_timeout_   = uint32_t(block_rate * min_timeout_s);
        timeout_       = min_timeout_;
        since_edge_    = timeout_;
        previous_gate_ = false;
        active_        = false;
    }

    /** Einmal pro Block mit dem Zustand des Gate-Eingangs */
    void Process(bool gate)
    {
        if(gate && !previous_gate_)
        {
            if(active_)
                timeout_ = std::max(min_timeout_, 3 * since_edge_);
            since_edge_ = 0;
            active_     = true;
        }
        else if(since_edge_ < timeout_)
        {
            since_edge_++;
        }
        else
        {
            active_  = false;
            timeout_ = min_timeout_;
        }
        previous_gate_ = gate;
    }

    bool active() const { return active_; }

  private:
    uint32_t min_timeout_;
    uint32_t timeout_;
    uint32_t since_edge_;
    bool     previous_gate_;
    bool     active_;
};

} // namespace patch_init
