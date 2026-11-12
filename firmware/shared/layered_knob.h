#pragma once
/**
 * Ein physischer Knob, der mehrere Parameter-Ebenen bedient (z. B. Seite A,
 * Seite B, Shift-Ebene).
 *
 * Wird die aktive Ebene gewechselt und passt deren gespeicherter Wert nicht
 * zur Knob-Position, springt nichts: Der Wert wird relativ nachgefuehrt, bis
 * Knob und Wert sich treffen. Die Nachfuehr-Rechnung (skew ratio) stammt aus
 * POT_STATE_CATCHING_UP in beads/pot_controller.h von Emilie Gillet (MIT):
 * Beide erreichen den Anschlag gleichzeitig.
 *
 * Die Ebene LAYER_FROZEN haelt alle Werte fest, z. B. waehrend der Taster
 * gedrueckt ist, der Knob aber noch nicht bewegt wurde.
 *
 * Verwendet von firmware/beads und firmware/rings.
 */

#include <cmath>

namespace patch_init
{

class LayeredKnob
{
  public:
    static constexpr int kNumLayers   = 3;
    static constexpr int LAYER_FROZEN = -1;

    void Init(float position)
    {
        for(int i = 0; i < kNumLayers; i++)
            value_[i] = position;
        layer_       = 0;
        catching_up_ = false;
        previous_    = position;
    }

    /** Wert einer Ebene setzen (z. B. aus gespeicherten Einstellungen) */
    void SetValue(int layer, float value) { value_[layer] = value; }

    /** Jeden Block mit der aktuellen Knob-Position (0..1) aufrufen */
    void Process(float position, int layer)
    {
        if(layer != layer_)
        {
            layer_ = layer;
            if(layer_ != LAYER_FROZEN)
            {
                catching_up_ = fabsf(value_[layer_] - position) > kMatch;
                previous_    = position;
            }
        }
        if(layer_ == LAYER_FROZEN)
            return;

        float& v = value_[layer_];
        if(!catching_up_)
        {
            v         = position;
            previous_ = position;
            return;
        }

        const float delta = position - previous_;
        if(fabsf(delta) > kStep)
        {
            float skew = delta > 0.f ? (1.001f - v) / (1.001f - previous_)
                                     : (0.001f + v) / (0.001f + previous_);
            skew = fminf(fmaxf(skew, 0.1f), 10.f);
            v    = fminf(fmaxf(v + skew * delta, 0.f), 1.f);
            previous_ = position;
            if(fabsf(v - position) < kStep)
            {
                v            = position;
                catching_up_ = false;
            }
        }
    }

    float value(int layer) const { return value_[layer]; }
    bool  catching_up() const { return catching_up_; }

  private:
    static constexpr float kMatch = 0.01f;
    static constexpr float kStep  = 0.005f;

    float value_[kNumLayers];
    int   layer_;
    bool  catching_up_;
    float previous_;
};

} // namespace patch_init
