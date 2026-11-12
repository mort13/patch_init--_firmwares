#pragma once
/**
 * DroidLink - Decoder fuer das DROID -> patch.init()-Protokoll.
 *
 * Uebertraegt 4 Stimmen (Gate, Pitch, Velocity) plus bis zu 6 frei belegbare
 * Aux-Werte ueber nur EIN CV-Kabel und EIN Gate-Kabel. Der DROID wandelt MIDI
 * (X7) in einen Zeitmultiplex-Strom um, das patch.init() dekodiert ihn hier.
 * Vollstaendige Spezifikation: docs/droid_link.md
 *
 *   Takt (Gate In):  Rechteck. Steigende Flanke = naechster Slot beginnt,
 *                    fallende Flanke = Abtastzeitpunkt (Wert ist eingeschwungen).
 *   Daten (CV In):   ein Spannungswert pro Slot. Ein Rahmen besteht aus
 *
 *     Slot 0           SYNC  +4.0 V  einziger Wert ueber +3.75 V -> Rahmenanfang
 *     Slot 1           REF    0.0 V  zweiter Kalibrierpunkt
 *     Slot 2..1+A      AUX 1..A      A = 0..6, frei belegbar, -3.5..+3.5 V
 *     dann je Stimme   NOTE k        Gate aus: -4.0 V, Gate an: Pitch -3.5..+3.5 V
 *                                    (1 V/Okt, 0 V = MIDI-Note 60)
 *                      VEL k         Velocity 0..1 -> 0..+3.5 V
 *
 *   Rahmenlaenge = 10 + A Slots (10..16). SYNC und REF sind exakte
 *   DROID-Spannungen, daraus kalibriert der Decoder laufend Offset und
 *   Verstaerkung des CV-Eingangs - die Toleranzen des patch.init() spielen
 *   damit fuer die Stimmung keine Rolle.
 *
 * Diese Datei ist hardwareunabhaengig (kein libDaisy), damit sie auch auf dem
 * PC getestet werden kann. Die Anbindung an Timer, ADC und Gate-Eingang des
 * patch.init() steckt in droid_link_patch_init.h.
 */

#include <cstdint>

namespace droid_link
{
constexpr int kNumVoices = 4;
constexpr int kMaxAux    = 6;
/** SYNC + REF + 4 x (NOTE, VEL) */
constexpr int kMinSlots = 2 + 2 * kNumVoices;
/** Der DROID-[switch] hat maximal 16 Eingaenge */
constexpr int kMaxSlots = kMinSlots + kMaxAux;

/** Spannungspegel des Protokolls (am DROID-Ausgang, in Volt) */
constexpr float kSyncVolts  = 4.0f;
constexpr float kRefVolts   = 0.0f;
constexpr float kOffVolts   = -4.0f;
constexpr float kRangeVolts = 3.5f; ///< Nutzbereich +-3.5 V, Vollausschlag fuer 0..1-Werte

/** Entscheidungsschwellen */
constexpr float kSyncThreshold = 3.75f;  ///< roher Messwert darueber = SYNC
constexpr float kOffThreshold  = -3.75f; ///< kalibrierter NOTE-Wert darunter = Gate aus
constexpr float kRefTolerance  = 0.4f;   ///< max. Abweichung des REF-Slots (roh)
constexpr float kMaxGainError  = 0.1f;   ///< max. +-10 % Verstaerkungsfehler des Eingangs
constexpr float kSlotMargin    = 0.25f;  ///< erlaubte Ueberschreitung von +-3.5 V

/** Nach so vielen fehlerhaften Rahmen in Folge gilt die Verbindung als verloren */
constexpr int kMaxBadFrames = 4;

/** MIDI-Note, die 0 V entspricht */
constexpr float kRootNote = 60.f;

struct Voice
{
    bool     gate     = false;
    float    pitch    = 0.f; ///< 1 V/Okt, 0 V = MIDI-Note 60; bleibt nach Gate aus erhalten
    float    velocity = 0.f; ///< 0..1; bleibt nach Gate aus erhalten
    uint32_t note_ons = 0;   ///< zaehlt jedes Note-On; Aenderung = neuer Anschlag

    /** Pitch als (gebrochene) MIDI-Notennummer, z. B. fuer daisysp::mtof() */
    float Note() const { return kRootNote + 12.f * pitch; }
};

struct State
{
    Voice    voice[kNumVoices];
    float    aux[kMaxAux] = {}; ///< kalibrierte Spannungen, siehe AuxUnipolar()
    int      num_aux      = 0;  ///< so viele Aux-Slots sendet der DROID
    bool     locked       = false; ///< true = gueltige Rahmen kommen an
    uint32_t frames       = 0;     ///< Zaehler uebernommener Rahmen

    /** Aux-Wert als 0..1 (DROID-Seite: wert * 3.5V), 0 falls nicht gesendet */
    float AuxUnipolar(int i) const
    {
        if(i < 0 || i >= num_aux)
            return 0.f;
        const float u = aux[i] / kRangeVolts;
        return u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    }
};

struct Stats
{
    uint32_t frames_ok   = 0;
    uint32_t frames_bad  = 0; ///< verworfene Rahmen (Laenge, REF, Bereich)
    uint32_t sync_losses = 0; ///< Verbindung verloren (Timeout oder zu viele Fehler)
    int      frame_slots = 0; ///< Laenge des letzten gueltigen Rahmens
    /** Kalibrierung: Spannung = (Messwert - offset) * gain + kRefVolts */
    float gain   = 1.f;
    float offset = 0.f;
    /** Mittlere Differenz zwischen Abtastwert (75 % des Slots) und Wert am
     *  Slotende in Volt. Grobe Diagnose: ueber ca. 0.002 V schwingt der
     *  CV-Eingang nicht rechtzeitig ein -> am DROID [lfo] hz verringern.
     *  Rauschen allein ergibt typisch < 0.001 V. */
    float settle_error = 0.f;
    /** Gemessene Slotrate in Hz (nur ueber Receiver, 0 = kein Takt) */
    float slot_hz = 0.f;
    /** true, solange am Takt-Eingang Flanken ankommen (nur ueber Receiver) */
    bool clock = false;
};

/** Reiner Protokoll-Decoder ohne Zeitbezug und ohne Hardware.
 *  Bekommt einen Messwert pro Slot (siehe Receiver fuer die Flankenlogik). */
class Decoder
{
  public:
    void Init()
    {
        state_       = State();
        stats_       = Stats();
        pos_         = 0;
        last_len_    = 0;
        bad_in_row_  = 0;
        calibrated_  = false;
        last_sample_ = 0.f;
    }

    /** An jeder fallenden Taktflanke mit der gemessenen, noch
     *  unkalibrierten Spannung (Volt) aufrufen. */
    void OnSample(float volts)
    {
        last_sample_ = volts;
        if(volts > kSyncThreshold)
        {
            if(pos_ == 1)
                return; // derselbe SYNC-Slot doppelt abgetastet (Stoerimpuls)
            if(pos_ > 0)
                EndFrame(pos_);
            buf_[0] = volts;
            pos_    = 1;
            return;
        }
        if(pos_ == 0)
            return; // noch kein SYNC gesehen
        if(pos_ >= kMaxSlots)
        {
            // Rahmen ohne SYNC zu lang: Synchronisation verloren
            BadFrame();
            pos_ = 0;
            return;
        }
        buf_[pos_++] = volts;
    }

    /** Optional: Spannung kurz vor der naechsten steigenden Flanke, also am
     *  Ende desselben Slots. Dient nur der Diagnose (Stats::settle_error). */
    void OnSlotEnd(float volts)
    {
        float err = volts - last_sample_;
        if(err < 0.f)
            err = -err;
        stats_.settle_error += (err - stats_.settle_error) * (1.f / 64.f);
    }

    /** Takt ausgefallen (Kabel gezogen, DROID-Patch gewechselt...):
     *  alle Gates aus, neu synchronisieren. */
    void OnTimeout()
    {
        pos_ = 0;
        Unlock();
    }

    const State& state() const { return state_; }
    const Stats& stats() const { return stats_; }

  private:
    float Calibrate(float measured) const
    {
        return (measured - stats_.offset) * stats_.gain + kRefVolts;
    }

    static bool InRange(float v, float lo, float hi) { return v >= lo && v <= hi; }

    void EndFrame(int len)
    {
        if(len < kMinSlots || len > kMaxSlots)
        {
            BadFrame();
            return;
        }

        // Struktur pruefen und Kalibrierung dieses einen Rahmens bestimmen
        const float m_sync = buf_[0];
        const float m_ref  = buf_[1];
        if(!InRange(m_ref, kRefVolts - kRefTolerance, kRefVolts + kRefTolerance))
        {
            BadFrame();
            return;
        }
        const float frame_gain = (kSyncVolts - kRefVolts) / (m_sync - m_ref);
        if(!InRange(frame_gain, 1.f - kMaxGainError, 1.f + kMaxGainError))
        {
            BadFrame();
            return;
        }

        // Erst wenn zwei Rahmen gleich lang sind, gilt das Format als erkannt.
        // Ein verschluckter oder zusaetzlicher Slot fuehrt so nie zu falschen
        // Noten. Im Lock bleibt das erkannte Format, bis kMaxBadFrames Fehler
        // in Folge kommen (z. B. weil am DROID die Aux-Anzahl geaendert wurde).
        if(len != last_len_)
        {
            if(state_.locked)
                BadFrame();
            else
                last_len_ = len;
            return;
        }

        // Laufende Kalibrierung, beim ersten Rahmen direkt uebernehmen
        if(!calibrated_)
        {
            stats_.gain   = frame_gain;
            stats_.offset = m_ref;
            calibrated_   = true;
        }
        else
        {
            constexpr float k = 1.f / 16.f;
            stats_.gain += k * (frame_gain - stats_.gain);
            stats_.offset += k * (m_ref - stats_.offset);
        }

        // Alle Slots dekodieren und pruefen, bevor irgendetwas uebernommen wird
        const int num_aux = len - kMinSlots;
        const int first   = 2 + num_aux;
        float     aux[kMaxAux];
        bool      gate[kNumVoices];
        float     pitch[kNumVoices], vel[kNumVoices];
        const float lo = -kRangeVolts - kSlotMargin;
        const float hi = kRangeVolts + kSlotMargin;

        for(int a = 0; a < num_aux; a++)
        {
            aux[a] = Calibrate(buf_[2 + a]);
            if(!InRange(aux[a], lo, hi))
            {
                BadFrame();
                return;
            }
        }
        for(int k = 0; k < kNumVoices; k++)
        {
            const float n = Calibrate(buf_[first + 2 * k]);
            const float v = Calibrate(buf_[first + 2 * k + 1]);
            // Velocity-Slots sind nie negativ: -4 V hier heisst, die Slots
            // sind verrutscht
            if(n > hi || !InRange(v, -kSlotMargin, hi))
            {
                BadFrame();
                return;
            }
            gate[k]  = n > kOffThreshold;
            pitch[k] = n < -kRangeVolts ? -kRangeVolts : (n > kRangeVolts ? kRangeVolts : n);
            vel[k]   = v / kRangeVolts;
            vel[k]   = vel[k] < 0.f ? 0.f : (vel[k] > 1.f ? 1.f : vel[k]);
        }

        // Uebernehmen
        for(int k = 0; k < kNumVoices; k++)
        {
            Voice& vo = state_.voice[k];
            if(gate[k] && !vo.gate)
            {
                // Neuer Anschlag: Pitch und Velocity springen sofort
                vo.note_ons++;
                vo.pitch    = pitch[k];
                vo.velocity = vel[k];
            }
            else if(gate[k])
            {
                // Gehaltene Note: Pitchbend folgen, ADC-Rauschen glaetten
                const float d = pitch[k] - vo.pitch;
                vo.pitch += (d > 0.005f || d < -0.005f) ? d : 0.5f * d;
                vo.velocity = vel[k];
            }
            vo.gate = gate[k];
        }
        for(int a = 0; a < num_aux; a++)
            state_.aux[a] = aux[a];
        for(int a = num_aux; a < kMaxAux; a++)
            state_.aux[a] = 0.f;
        state_.num_aux = num_aux;
        state_.locked  = true;
        state_.frames++;

        stats_.frames_ok++;
        stats_.frame_slots = len;
        bad_in_row_        = 0;
    }

    void BadFrame()
    {
        stats_.frames_bad++;
        if(++bad_in_row_ >= kMaxBadFrames)
        {
            bad_in_row_ = 0;
            Unlock();
        }
    }

    void Unlock()
    {
        if(state_.locked)
            stats_.sync_losses++;
        state_.locked = false;
        last_len_     = 0;
        for(Voice& v : state_.voice)
            v.gate = false;
    }

    State state_;
    Stats stats_;
    float buf_[kMaxSlots] = {};
    int   pos_            = 0;
    int   last_len_       = 0;
    int   bad_in_row_     = 0;
    bool  calibrated_     = false;
    float last_sample_    = 0.f;
};

/** Flankenlogik: wird mit konstanter Rate (poll_hz) mit dem aktuellen
 *  Zustand des Takt-Eingangs und der Spannung am Daten-Eingang aufgerufen.
 *
 *   fallende Flanke   -> Decoder::OnSample()  (Abtastzeitpunkt, ~75 % des Slots)
 *   steigende Flanke  -> Decoder::OnSlotEnd() mit dem Wert von 2 Ticks vorher
 *                        (sicher noch derselbe Slot, nur Diagnose)
 *   keine Flanke fuer timeout_ms -> Decoder::OnTimeout()
 */
class Receiver
{
  public:
    void Init(uint32_t poll_hz, float timeout_ms)
    {
        decoder_.Init();
        poll_hz_       = poll_hz;
        timeout_ticks_ = static_cast<uint32_t>(timeout_ms * 0.001f * float(poll_hz));
        tick_          = 0;
        last_fall_     = 0;
        have_fall_     = false;
        prev_gate_     = false;
        timed_out_     = true;
        slot_ticks_    = 0.f;
        for(float& h : hist_)
            h = 0.f;
    }

    void Tick(bool gate, float volts)
    {
        tick_++;
        hist_[tick_ & 3u] = volts;

        if(prev_gate_ && !gate)
        {
            decoder_.OnSample(volts);
            const uint32_t d = tick_ - last_fall_;
            if(!timed_out_)
                slot_ticks_ = slot_ticks_ > 0.f ? slot_ticks_ + 0.05f * (float(d) - slot_ticks_)
                                                : float(d);
            last_fall_ = tick_;
            have_fall_ = true;
            timed_out_ = false;
        }
        else if(!prev_gate_ && gate)
        {
            if(have_fall_ && tick_ - last_fall_ >= 3)
                decoder_.OnSlotEnd(hist_[(tick_ - 2) & 3u]);
            have_fall_ = false;
        }
        prev_gate_ = gate;

        if(!timed_out_ && tick_ - last_fall_ > timeout_ticks_)
        {
            timed_out_  = true;
            slot_ticks_ = 0.f;
            decoder_.OnTimeout();
        }
    }

    const State& state() const { return decoder_.state(); }

    Stats stats() const
    {
        Stats s   = decoder_.stats();
        s.slot_hz = slot_ticks_ > 0.f ? float(poll_hz_) / slot_ticks_ : 0.f;
        s.clock   = !timed_out_;
        return s;
    }

  private:
    Decoder  decoder_;
    uint32_t poll_hz_       = 20000;
    uint32_t timeout_ticks_ = 1000;
    uint32_t tick_          = 0;
    uint32_t last_fall_     = 0;
    float    hist_[4]       = {};
    float    slot_ticks_    = 0.f;
    bool     have_fall_     = false;
    bool     prev_gate_     = false;
    bool     timed_out_     = true;
};

} // namespace droid_link
