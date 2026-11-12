/**
 * PC-Test fuer firmware/shared/droid_link.h
 *
 * Simuliert die komplette Strecke:
 *   DROID   [lfo] -> [switch] -> [sample] wie in droid/droid_link.ini, mit der
 *           Zyklusrate des DROID (~5.5 kHz, also zeitlich quantisierte Flanken)
 *   Kabel   RC-Tiefpass des CV-Eingangs, Verstaerkungs- und Offsetfehler, Rauschen
 *   patch.init()  Receiver::Tick() mit 20 kHz - derselbe Code wie auf der Hardware
 *
 * Bauen und ausfuehren (Host-Compiler, nicht arm-none-eabi):
 *   make -C tests
 */

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "droid_link.h"

using namespace droid_link;

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...)                                              \
    do                                                                \
    {                                                                 \
        checks++;                                                     \
        if(!(cond))                                                   \
        {                                                             \
            failures++;                                               \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while(0)

static bool Near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

struct DroidVoice
{
    bool  gate  = false;
    float pitch = 0.f; // V, 0 V = MIDI 60
    float vel   = 0.f; // 0..1
};

struct Sim
{
    // Strecke
    double poll_hz       = 20000.0;
    double droid_loop_hz = 5500.0;
    double slot_hz       = 1000.0; // [lfo] hz
    double pw            = 0.75;   // [lfo] pulsewidth
    double tau           = 50e-6;  // Zeitkonstante CV-Eingang
    double gain_err      = 0.02;   // CV-Eingang misst 2 % zu viel
    double offset        = 0.015;  // und 15 mV Offset
    double noise_rms     = 0.0003; // 0.3 mV
    int    num_aux       = 1;

    // Zustand am DROID
    DroidVoice voice[kNumVoices];
    float      aux[kMaxAux] = {}; // 0..1
    bool       clock_on     = true;
    bool       glitch       = false; // einmalig einen Stoerimpuls auf den Takt geben

    Receiver rx;

    void Init()
    {
        rx.Init(uint32_t(poll_hz), 50.f);
        held_ = SlotValue(0);
    }

    int FrameLen() const { return kMinSlots + num_aux; }

    /** Laufzeit fuer n Rahmen */
    double Frames(double n) const { return n * FrameLen() / slot_hz; }

    /** Wert des Slots s, genau wie die [switch]-Eingaenge im DROID-Patch */
    double SlotValue(int s) const
    {
        if(s == 0)
            return kSyncVolts;
        if(s == 1)
            return kRefVolts;
        if(s < 2 + num_aux)
            return aux[s - 2] * kRangeVolts;
        const int k = (s - 2 - num_aux) / 2;
        if((s - 2 - num_aux) % 2)
            return voice[k].vel * kRangeVolts;
        return voice[k].gate ? voice[k].pitch : kOffVolts;
    }

    void Run(double seconds)
    {
        const double dt = 1.0 / poll_hz;
        const double a  = 1.0 - std::exp(-dt / tau);
        const long   n  = long(seconds * poll_hz + 0.5);
        for(long i = 0; i < n; i++)
        {
            // DROID-Zyklen, die in diesen Tick fallen
            droid_acc_ += dt * droid_loop_hz;
            while(droid_acc_ >= 1.0)
            {
                droid_acc_ -= 1.0;
                DroidCycle();
            }

            bool gate = clock_on && clock_;
            if(glitch && gate && phase_ > 0.3 && phase_ < 0.5)
            {
                gate   = false; // ein Tick lang Low = zusaetzliche Flanke
                glitch = false;
            }

            line_ += (held_ - line_) * a;
            const double meas = line_ * (1.0 + gain_err) + offset + Noise();
            rx.Tick(gate, float(meas));
        }
    }

  private:
    void DroidCycle()
    {
        if(!clock_on)
            return;
        phase_ += slot_hz / droid_loop_hz;
        const bool rising = phase_ >= 1.0;
        if(rising)
            phase_ -= 1.0;
        clock_ = phase_ < pw;
        // [switch] forward + [sample] im selben Zyklus wie die Taktflanke
        if(rising)
        {
            slot_ = (slot_ + 1) % FrameLen();
            held_ = SlotValue(slot_);
        }
    }

    double Noise()
    {
        double sum = 0.0;
        for(int i = 0; i < 4; i++)
        {
            rng_ = rng_ * 1664525u + 1013904223u;
            sum += double(rng_ >> 8) / 16777216.0 - 0.5;
        }
        return sum * 1.7320508 * noise_rms; // 4 Gleichverteilungen -> sigma 1
    }

    double   droid_acc_ = 0.0;
    double   phase_     = 0.0;
    bool     clock_     = true;
    int      slot_      = 0;
    double   held_      = 0.0;
    double   line_      = 0.0;
    uint32_t rng_       = 12345u;
};

// --------------------------------------------------------------------------

static void TestLockAndChord()
{
    std::printf("Lock, Akkord, Kalibrierung\n");
    Sim s;
    s.Init();
    s.Run(s.Frames(4));
    CHECK(s.rx.state().locked, "kein Lock nach 4 Rahmen");
    CHECK(s.rx.stats().clock, "Takt nicht erkannt");
    CHECK(Near(s.rx.stats().slot_hz, 1000.0, 20.0), "slot_hz %.1f", s.rx.stats().slot_hz);

    const float p[3] = {0.f, 4.f / 12.f, 7.f / 12.f}; // C-Dur
    for(int k = 0; k < 3; k++)
    {
        s.voice[k].gate  = true;
        s.voice[k].pitch = p[k];
        s.voice[k].vel   = 0.5f + 0.1f * k;
    }
    s.Run(s.Frames(3));
    const State& st = s.rx.state();
    for(int k = 0; k < 3; k++)
    {
        CHECK(st.voice[k].gate, "Stimme %d Gate aus", k + 1);
        CHECK(Near(st.voice[k].pitch, p[k], 0.001), "Stimme %d pitch %.5f statt %.5f", k + 1,
              st.voice[k].pitch, p[k]);
        CHECK(Near(st.voice[k].velocity, s.voice[k].vel, 0.003), "Stimme %d vel %.4f", k + 1,
              st.voice[k].velocity);
        CHECK(st.voice[k].note_ons == 1, "Stimme %d note_ons %u", k + 1, unsigned(st.voice[k].note_ons));
    }
    CHECK(!st.voice[3].gate, "Stimme 4 sollte aus sein");
    CHECK(Near(st.voice[0].Note(), 60.0, 0.02), "Note %.3f", st.voice[0].Note());
    CHECK(Near(st.voice[2].Note(), 67.0, 0.02), "Note %.3f", st.voice[2].Note());

    // Kalibrierung hat Gain- und Offsetfehler des Eingangs gelernt
    const Stats stt = s.rx.stats();
    CHECK(Near(stt.gain, 1.0 / 1.02, 0.002), "gain %.5f", stt.gain);
    CHECK(Near(stt.offset, 0.015, 0.002), "offset %.5f", stt.offset);
    CHECK(stt.frames_bad == 0, "frames_bad %u", unsigned(stt.frames_bad));
    CHECK(stt.settle_error < 0.001, "settle_error %.5f", stt.settle_error);
}

static void TestReleaseRetriggerBend()
{
    std::printf("Release, Neuanschlag, Pitchbend\n");
    Sim s;
    s.Init();
    s.voice[0] = {true, 1.f, 0.8f};
    s.voice[1] = {true, -1.f, 0.3f};
    s.Run(s.Frames(5));

    // Gate aus: Pitch und Velocity bleiben fuer das Release erhalten
    s.voice[1].gate = false;
    s.Run(s.Frames(3));
    const State& st = s.rx.state();
    CHECK(!st.voice[1].gate, "Stimme 2 noch an");
    CHECK(Near(st.voice[1].pitch, -1.0, 0.001), "Pitch nach Release %.4f", st.voice[1].pitch);
    CHECK(Near(st.voice[1].velocity, 0.3, 0.003), "Velocity nach Release %.4f",
          st.voice[1].velocity);

    // Neuer Anschlag auf derselben Stimme mit anderer Tonhoehe
    s.voice[1] = {true, 0.25f, 1.f};
    s.Run(s.Frames(3));
    CHECK(st.voice[1].gate && st.voice[1].note_ons == 2, "Retrigger fehlt (ons %u)",
          unsigned(st.voice[1].note_ons));
    CHECK(Near(st.voice[1].pitch, 0.25, 0.001), "Pitch %.4f", st.voice[1].pitch);

    // Pitchbend auf gehaltener Note: folgt, ohne neu anzuschlagen
    for(int i = 1; i <= 50; i++)
    {
        s.voice[0].pitch = 1.f + (2.f / 12.f) * float(i) / 50.f;
        s.Run(0.004);
    }
    s.Run(s.Frames(4));
    CHECK(Near(st.voice[0].pitch, 1.0 + 2.0 / 12.0, 0.001), "Bend-Ende %.5f", st.voice[0].pitch);
    CHECK(st.voice[0].note_ons == 1, "Bend hat neu angeschlagen (ons %u)",
          unsigned(st.voice[0].note_ons));
}

static void TestGlitch()
{
    std::printf("Stoerimpuls auf der Taktleitung\n");
    Sim s;
    s.Init();
    s.voice[0] = {true, 0.5f, 0.7f};
    s.voice[2] = {true, -0.5f, 0.7f};
    s.Run(s.Frames(5) + 4.0 / s.slot_hz); // mitten in den Rahmen (Stimmen-Slots)
    const uint32_t bad_before = s.rx.stats().frames_bad;
    const uint32_t ok_before  = s.rx.stats().frames_ok;

    s.glitch = true; // ein zusaetzlicher Slot -> dieser Rahmen ist verrutscht
    s.Run(s.Frames(4));
    const State& st = s.rx.state();
    CHECK(s.rx.stats().frames_bad == bad_before + 1, "frames_bad %u -> %u", unsigned(bad_before),
          unsigned(s.rx.stats().frames_bad));
    CHECK(s.rx.stats().frames_ok >= ok_before + 2, "danach keine gueltigen Rahmen mehr");
    CHECK(st.locked, "Lock verloren");
    for(int k = 0; k < kNumVoices; k++)
        CHECK(st.voice[k].note_ons == (k == 0 || k == 2 ? 1u : 0u), "Stimme %d falsch angeschlagen",
              k + 1);
    CHECK(Near(st.voice[0].pitch, 0.5, 0.001) && Near(st.voice[2].pitch, -0.5, 0.001),
          "Pitch durch Glitch veraendert");
    CHECK(!st.voice[1].gate && !st.voice[3].gate, "Geister-Gate");
}

static void TestTimeoutAndRelock()
{
    std::printf("Takt weg und wieder da\n");
    Sim s;
    s.Init();
    s.voice[0] = {true, 0.f, 1.f};
    s.Run(s.Frames(5));

    s.clock_on = false;
    s.Run(0.1);
    CHECK(!s.rx.state().locked, "Lock trotz fehlendem Takt");
    CHECK(!s.rx.state().voice[0].gate, "haengende Note nach Kabel ab");
    CHECK(!s.rx.stats().clock, "Takt noch gemeldet");
    CHECK(s.rx.stats().sync_losses == 1, "sync_losses %u", unsigned(s.rx.stats().sync_losses));

    s.clock_on = true;
    s.Run(s.Frames(4));
    CHECK(s.rx.state().locked, "kein neuer Lock");
    CHECK(s.rx.state().voice[0].gate && s.rx.state().voice[0].note_ons == 2,
          "Note nach Relock nicht wieder angeschlagen");
}

static void TestAuxAndFormatChange()
{
    std::printf("Aux-Werte und Formatwechsel\n");
    Sim s;
    s.Init();
    s.aux[0] = 0.5f;
    s.Run(s.Frames(4));
    CHECK(s.rx.state().num_aux == 1, "num_aux %d", s.rx.state().num_aux);
    CHECK(Near(s.rx.state().AuxUnipolar(0), 0.5, 0.002), "aux %.4f", s.rx.state().AuxUnipolar(0));

    // Am DROID zwei Aux-Slots dazu (Rahmen 11 -> 13): kurz Fehler, dann neuer Lock
    s.num_aux = 3;
    s.aux[1]  = 0.25f;
    s.aux[2]  = 1.f;
    s.voice[3] = {true, 2.f, 0.6f};
    s.Run(s.Frames(10));
    const State& st = s.rx.state();
    CHECK(st.locked && st.num_aux == 3, "locked %d num_aux %d", int(st.locked), st.num_aux);
    CHECK(Near(st.AuxUnipolar(1), 0.25, 0.002) && Near(st.AuxUnipolar(2), 1.0, 0.002),
          "aux %.4f %.4f", st.AuxUnipolar(1), st.AuxUnipolar(2));
    CHECK(st.voice[3].gate && Near(st.voice[3].pitch, 2.0, 0.001), "Stimme 4 nach Formatwechsel");
}

static void TestExtremes()
{
    std::printf("Randwerte\n");
    Sim s;
    s.Init();
    s.voice[0] = {true, 3.5f, 1.f};  // hoechster Ton inkl. Pitchbend
    s.voice[1] = {true, -3.5f, 0.f}; // tiefster Ton, Velocity 0
    s.voice[2] = {true, 0.f, 1.f};
    s.aux[0]   = 1.f;
    s.Run(s.Frames(5));
    const State& st = s.rx.state();
    CHECK(st.locked, "kein Lock");
    CHECK(st.voice[0].gate && Near(st.voice[0].pitch, 3.5, 0.002), "+3.5 V: %.4f", st.voice[0].pitch);
    CHECK(st.voice[1].gate && Near(st.voice[1].pitch, -3.5, 0.002), "-3.5 V: %.4f", st.voice[1].pitch);
    CHECK(Near(st.voice[1].velocity, 0.0, 0.003), "Velocity 0: %.4f", st.voice[1].velocity);
    CHECK(s.rx.stats().frames_bad == 0, "frames_bad %u", unsigned(s.rx.stats().frames_bad));
}

static void TestSlowInputIsReported()
{
    std::printf("Zu schneller Takt wird gemeldet\n");
    Sim s;
    s.tau = 400e-6; // traeger Eingang: bei 1 kHz schwingt er nicht ein
    s.Init();
    s.voice[0] = {true, 1.f, 0.5f};
    s.voice[1] = {true, -2.f, 0.9f};
    s.Run(s.Frames(30));
    CHECK(s.rx.stats().settle_error > 0.002, "settle_error nur %.5f", s.rx.stats().settle_error);

    // Mit 200 Hz passt es wieder (3.75 ms bis zur Abtastung = 9 Zeitkonstanten)
    Sim t;
    t.tau     = 400e-6;
    t.slot_hz = 200.0;
    t.Init();
    t.voice[0] = {true, 1.f, 0.5f};
    t.voice[1] = {true, -2.f, 0.9f};
    t.Run(t.Frames(30));
    CHECK(t.rx.stats().settle_error < 0.002, "settle_error %.5f", t.rx.stats().settle_error);
    CHECK(t.rx.state().locked && Near(t.rx.state().voice[1].pitch, -2.0, 0.001),
          "pitch %.5f", t.rx.state().voice[1].pitch);
}

int main()
{
    TestLockAndChord();
    TestReleaseRetriggerBend();
    TestGlitch();
    TestTimeoutAndRelock();
    TestAuxAndFormatChange();
    TestExtremes();
    TestSlowInputIsReported();
    std::printf("%d von %d Checks ok\n", checks - failures, checks);
    return failures ? 1 : 0;
}
