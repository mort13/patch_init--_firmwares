#pragma once
/**
 * Einstellungen, die einen Neustart ueberleben (QSPI, daisy::PersistentStorage).
 *
 * Seite A wird nicht gespeichert: Sie folgt immer der physischen Knob-Position.
 * Seite B und die Random-Ebene sind nach einem Neustart nicht sichtbar und
 * werden deshalb gesichert, ebenso die Quality-Stufe und die V/Oct-Kalibrierung.
 */

#include <cmath>
#include <cstdint>

namespace beads_port
{

/** Ebenen-Indizes von Seite B bzw. der Random-Ebene, je Knob 1..4 */
enum PageBParameter
{
    PAGE_B_FEEDBACK,
    PAGE_B_DRY_WET,
    PAGE_B_REVERB,
    PAGE_B_SHAPE,
};

enum RandomParameter
{
    RANDOM_TIME,
    RANDOM_SIZE,
    RANDOM_PITCH,
    RANDOM_SHAPE,
};

/** V/Oct: Halbtoene = offset + scale * GetAdcValue(CV_IN_3); +-5 V = +-1 */
constexpr float kVoctNominalScale  = 60.f;
constexpr float kVoctNominalOffset = 0.f;

struct Settings
{
    /** Bei Layout-Aenderung erhoehen, dann werden die Defaults geladen */
    static constexpr uint32_t kVersion = 1;

    uint32_t version;
    uint8_t  media;     // beads::Processor::Media, 0..3
    float    page_b[4]; // Knob-Position 0..1
    float    random[4]; // Knob-Position 0..1, 0.5 = keine Zufallsmodulation
    float    voct_scale;
    float    voct_offset;

    /** Kleine Knob-Aenderungen loesen keinen Flash-Schreibvorgang aus */
    bool operator==(const Settings& o) const
    {
        constexpr float kKnobTolerance = 0.01f;
        constexpr float kCalTolerance  = 1e-4f;
        if(version != o.version || media != o.media)
            return false;
        for(int i = 0; i < 4; i++)
        {
            if(fabsf(page_b[i] - o.page_b[i]) > kKnobTolerance
               || fabsf(random[i] - o.random[i]) > kKnobTolerance)
                return false;
        }
        return fabsf(voct_scale - o.voct_scale) <= kCalTolerance
               && fabsf(voct_offset - o.voct_offset) <= kCalTolerance;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

inline Settings DefaultSettings()
{
    Settings s;
    s.version                = Settings::kVersion;
    s.media                  = 3; // MEDIA_DIGITAL, wie Beads ab Werk
    s.page_b[PAGE_B_FEEDBACK] = 0.f;
    s.page_b[PAGE_B_DRY_WET]  = 0.5f;
    s.page_b[PAGE_B_REVERB]   = 0.2f;
    s.page_b[PAGE_B_SHAPE]    = 0.5f;
    for(int i = 0; i < 4; i++)
        s.random[i] = 0.5f;
    s.voct_scale  = kVoctNominalScale;
    s.voct_offset = kVoctNominalOffset;
    return s;
}

} // namespace beads_port
