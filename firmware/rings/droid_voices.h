#pragma once
/**
 * DroidLink -> Rings: verteilt die 4 MIDI-Stimmen vom DROID auf die
 * Rings-Stimmen (siehe docs/droid_link.md).
 *
 * Jedes Note-On wird ein Anschlag. Rings schlaegt pro Block (0,5 ms) nur eine
 * Stimme an. Gleichzeitige Note-Ons (Akkorde) kommen deshalb in eine
 * Warteschlange und gehen nacheinander raus:
 *
 *  - Rings-Engine (Part mit Direktsteuerung): ein Anschlag pro Block, ein
 *    Akkord aus 4 Noten steht nach 1,5 ms.
 *  - Disastrous Peace: mindestens 10 Bloecke (5 ms) Abstand. Dessen
 *    NoteFilter friert die alte Stimme mit einem 4 ms verzoegerten Wert ein;
 *    schnellere Anschlaege bekaemen die Tonhoehe der vorigen Note.
 *
 * DroidLink-Stimme k spielt Rings-Stimme k % POLYPHONY. Eine Rings-Stimme
 * folgt dem Pitchbend ihrer letzten Note, bis eine andere Note sie uebernimmt.
 */

#include <cstdint>

#include "droid_link.h"

namespace rings_port
{

class DroidVoices
{
  public:
    static constexpr int kNumVoices = droid_link::kNumVoices;

    void Init()
    {
        for(int i = 0; i < kNumVoices; i++)
        {
            note_ons_[i] = 0;
            owner_[i]    = -1;
            notes_[i]    = 0.f;
        }
        queue_len_       = 0;
        spacing_         = 0;
        strike_voice_    = -1;
        strike_velocity_ = 1.f;
        last_owner_      = -1;
        last_note_       = 0.f;
        locked_          = false;
        just_locked_     = false;
    }

    /**
     * Einmal pro Block. polyphony = 1..4, min_spacing = Bloecke zwischen zwei
     * Anschlaegen (1 = jeder Block).
     */
    void Process(const droid_link::State& s, int polyphony, int min_spacing)
    {
        just_locked_ = s.locked && !locked_;
        locked_      = s.locked;

        // Neue Note-Ons einreihen. Die Zaehler laufen auch ohne Lock mit,
        // damit beim Verbinden keine alten Anschlaege nachkommen.
        for(int k = 0; k < kNumVoices; k++)
        {
            if(s.voice[k].note_ons == note_ons_[k])
                continue;
            note_ons_[k] = s.voice[k].note_ons;
            if(locked_ && !Queued(k))
                queue_[queue_len_++] = k;
        }
        if(!locked_)
            queue_len_ = 0;

        strike_voice_ = -1;
        if(spacing_ > 0)
            --spacing_;
        if(queue_len_ > 0 && spacing_ == 0)
        {
            const int k = queue_[0];
            for(int i = 1; i < queue_len_; i++)
                queue_[i - 1] = queue_[i];
            --queue_len_;

            const int voice  = k % polyphony;
            owner_[voice]    = k;
            last_owner_      = k;
            strike_voice_    = voice;
            strike_velocity_ = s.voice[k].velocity;
            spacing_         = min_spacing;
        }

        for(int v = 0; v < kNumVoices; v++)
            notes_[v] = owner_[v] >= 0 ? 12.f * s.voice[owner_[v]].pitch : 0.f;
        last_note_ = last_owner_ >= 0 ? 12.f * s.voice[last_owner_].pitch : 0.f;
    }

    /** true = gueltige DroidLink-Rahmen kommen an */
    bool locked() const { return locked_; }
    /** Verbindung ist in diesem Block zustande gekommen */
    bool just_locked() const { return just_locked_; }

    /** In diesem Block anzuschlagende Rings-Stimme, -1 = keine */
    int strike_voice() const { return strike_voice_; }
    /** Velocity des Anschlags, 0..1 */
    float strike_velocity() const { return strike_velocity_; }

    /** Tonhoehe je Rings-Stimme in Halbtoenen, 0 = MIDI-Note 60 */
    const float* notes() const { return notes_; }
    /** Tonhoehe der zuletzt angeschlagenen Note, 0 = MIDI-Note 60 */
    float last_note() const { return last_note_; }

  private:
    bool Queued(int k) const
    {
        for(int i = 0; i < queue_len_; i++)
            if(queue_[i] == k)
                return true;
        return false;
    }

    uint32_t note_ons_[kNumVoices];
    int      queue_[kNumVoices];
    int      queue_len_;
    int      spacing_;
    int      owner_[kNumVoices]; // DroidLink-Stimme, die die Rings-Stimme zuletzt angeschlagen hat
    float    notes_[kNumVoices];
    float    last_note_;
    int      strike_voice_;
    float    strike_velocity_;
    int      last_owner_;
    bool     locked_;
    bool     just_locked_;
};

} // namespace rings_port
