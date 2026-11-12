#pragma once
/**
 * Einstellungen, die einen Neustart ueberleben (QSPI, daisy::PersistentStorage).
 *
 * Seite A wird nicht gespeichert: Sie folgt immer der physischen Knob-Position.
 * Seite B ist nach einem Neustart nicht sichtbar und wird deshalb gesichert,
 * ebenso die Engine (Rings / Disastrous Peace) und die V/Oct-Kalibrierung.
 */

#include <cmath>
#include <cstdint>

namespace rings_port
{

/** Ebenen-Indizes von Seite B, je Knob 1..4 */
enum PageBParameter
{
    PAGE_B_POSITION,
    PAGE_B_MODEL,
    PAGE_B_POLYPHONY,
    PAGE_B_WIDTH,
};

constexpr int kNumModels    = 6; // rings::RESONATOR_MODEL_LAST bzw. FX_LAST
constexpr int kMaxPolyphony = 4; // rings::kMaxPolyphony

/** V/Oct: Halbtoene = offset + scale * GetAdcValue(CV_IN_1); +-5 V = +-1 */
constexpr float kVoctNominalScale  = 60.f;
constexpr float kVoctNominalOffset = 0.f;

/** Knob-Position in der Mitte der Zone `index` von `zones` */
constexpr float ZoneCenter(int index, int zones)
{
    return (float(index) + 0.5f) / float(zones);
}

struct Settings
{
    /**
     * "RI" + Layout-Nummer. Bei Layout-Aenderung erhoehen, dann werden die
     * Defaults geladen. Das Praefix unterscheidet die Daten von anderen
     * Firmwares, falls sie doch einmal am selben QSPI-Offset liegen.
     */
    static constexpr uint32_t kVersion = 0x52490001;

    uint32_t version;
    uint8_t  easter_egg; // 1 = Disastrous Peace
    float    page_b[4];  // Knob-Position 0..1
    float    voct_scale;
    float    voct_offset;

    /** Kleine Knob-Aenderungen loesen keinen Flash-Schreibvorgang aus */
    bool operator==(const Settings& o) const
    {
        constexpr float kKnobTolerance = 0.01f;
        constexpr float kCalTolerance  = 1e-4f;
        if(version != o.version || easter_egg != o.easter_egg)
            return false;
        for(int i = 0; i < 4; i++)
        {
            if(fabsf(page_b[i] - o.page_b[i]) > kKnobTolerance)
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
    s.version                  = Settings::kVersion;
    s.easter_egg               = 0;
    s.page_b[PAGE_B_POSITION]  = 0.5f;
    s.page_b[PAGE_B_MODEL]     = ZoneCenter(0, kNumModels);    // Modal
    s.page_b[PAGE_B_POLYPHONY] = ZoneCenter(0, kMaxPolyphony); // 1 Stimme
    s.page_b[PAGE_B_WIDTH]     = 1.f; // ODD links, EVEN rechts wie das Original
    s.voct_scale               = kVoctNominalScale;
    s.voct_offset              = kVoctNominalOffset;
    return s;
}

} // namespace rings_port
