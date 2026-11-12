#pragma once
/**
 * DroidLink-Empfaenger fuer das patch.init(): verbindet droid_link::Receiver
 * (droid_link.h) mit einem CV-Eingang und einem Gate-Eingang.
 *
 * Ein Hardware-Timer (Default TIM5, 20 kHz) fragt Gate und ADC ab - so
 * funktioniert der Empfang unabhaengig von Blockgroesse und Rechenlast des
 * Audio-Callbacks. Damit der Timer das Audio-Callback unterbrechen darf, wird
 * die Interrupt-Prioritaet der Audio-DMA um eine Stufe abgesenkt
 * (Config::preempt_audio). Die Timer-Routine braucht ca. 1 % CPU.
 *
 * Verwendung:
 *
 *   #include "droid_link_patch_init.h"
 *   droid_link::PatchInitLink droid;   // nicht "link" nennen: kollidiert mit POSIX link()
 *
 *   int main() {
 *       hw.Init();
 *       droid.Init(hw);   // CV In 4 = Daten, Gate In 2 = Takt
 *       droid.Start();
 *       hw.StartAudio(AudioCallback);
 *       ...
 *   }
 *
 *   void AudioCallback(...) {
 *       const droid_link::State s = droid.GetState();
 *       for(int v = 0; v < droid_link::kNumVoices; v++) {
 *           const droid_link::Voice& voice = s.voice[v];
 *           // voice.gate, mtof(voice.Note()), voice.velocity, voice.note_ons
 *       }
 *   }
 *
 * GetState()/GetStats() liefern konsistente Kopien und duerfen aus dem
 * Audio-Callback oder der Hauptschleife aufgerufen werden.
 */

#include "daisy_patch_sm.h"
#include "droid_link.h"

namespace droid_link
{
class PatchInitLink
{
  public:
    using TimerPeriph = daisy::TimerHandle::Config::Peripheral;

    struct Config
    {
        /** ADC-Kanal der Datenleitung, CV_5..CV_8 = CV In 1..4 des patch.init() */
        int cv_in = daisy::patch_sm::CV_8;
        /** Takt-Eingang: 1 = Gate In 1, 2 = Gate In 2 */
        int gate_in = 2;
        /** Abfragerate. Sollte mind. 10x so hoch sein wie die Slotrate des DROID. */
        uint32_t poll_hz = 20000;
        /** So lange ohne Takt -> Verbindung verloren, alle Gates aus */
        float timeout_ms = 50.f;
        /** TIM_2 ist vom System belegt, TIM_3/4/5 sind frei */
        TimerPeriph timer = TimerPeriph::TIM_5;
        /** Audio-DMA-Prioritaet absenken, damit der Timer das Audio-Callback
         *  unterbrechen kann. Nur abschalten, wenn die Blockgroesse so klein ist,
         *  dass das Callback nie laenger als ca. 1/4 Slot dauert. */
        bool preempt_audio = true;
    };

    /** Mit Default-Konfiguration: CV In 4 = Daten, Gate In 2 = Takt */
    void Init(daisy::patch_sm::DaisyPatchSM& hw) { Init(hw, Config()); }

    void Init(daisy::patch_sm::DaisyPatchSM& hw, const Config& cfg)
    {
        cfg_  = cfg;
        adc_  = hw.adc.GetPtr(static_cast<uint8_t>(cfg.cv_in));
        gate_ = cfg.gate_in == 1 ? &hw.gate_in_1 : &hw.gate_in_2;
        if(cfg_.timer == TimerPeriph::TIM_2)
            cfg_.timer = TimerPeriph::TIM_5;
        rx_.Init(cfg_.poll_hz, cfg_.timeout_ms);
    }

    /** Startet die Abfrage per Timer-Interrupt. */
    void Start()
    {
        daisy::TimerHandle::Config tc;
        tc.periph     = cfg_.timer;
        tc.dir        = daisy::TimerHandle::Config::CounterDir::UP;
        tc.period     = daisy::System::GetPClk1Freq() * 2 / cfg_.poll_hz - 1;
        tc.enable_irq = true;
        timer_.Init(tc);
        timer_.SetCallback(&TimerCallback, this);

        // libDaisy setzt alle DMA-Interrupts auf Prioritaet 0 und eigene Timer
        // auf 15. Timer hoch, Audio-DMA (SAI1, DMA1 Stream 0/1) eine Stufe runter.
        HAL_NVIC_SetPriority(TimerIrq(cfg_.timer), 0, 0);
        if(cfg_.preempt_audio)
        {
            HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1, 0);
            HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 1, 0);
        }
        timer_.Start();
    }

    /** Eine Abfrage. Wird vom Timer aufgerufen; ohne Start() kann man Poll()
     *  auch selbst mit konstanter Rate aufrufen (dann poll_hz passend setzen). */
    void Poll()
    {
        // Patch-SM-CV-Eingaenge sind invertiert: 0 -> +5 V, 65535 -> -5 V
        const float v = (0.5f - float(*adc_) * (1.f / 65536.f)) * 10.f;
        const bool  g = gate_->State();
        BeginWrite();
        rx_.Tick(g, v);
        EndWrite();
    }

    /** Konsistente Kopie des aktuellen Zustands (Stimmen, Aux, Lock). */
    State GetState() const
    {
        State    s;
        uint32_t seq;
        do
        {
            seq = seq_;
            __DMB();
            s = rx_.state();
            __DMB();
        } while((seq & 1u) || seq != seq_);
        return s;
    }

    /** Konsistente Kopie der Diagnosewerte (inkl. Slotrate und Takt-Status). */
    Stats GetStats() const
    {
        Stats    s;
        uint32_t seq;
        do
        {
            seq = seq_;
            __DMB();
            s = rx_.stats();
            __DMB();
        } while((seq & 1u) || seq != seq_);
        return s;
    }

  private:
    static void TimerCallback(void* data) { static_cast<PatchInitLink*>(data)->Poll(); }

    static IRQn_Type TimerIrq(TimerPeriph p)
    {
        switch(p)
        {
            case TimerPeriph::TIM_3: return TIM3_IRQn;
            case TimerPeriph::TIM_4: return TIM4_IRQn;
            default: return TIM5_IRQn;
        }
    }

    // Sequenz-Lock: ungerade = Timer schreibt gerade. Der Timer unterbricht
    // die Leser, nie umgekehrt - Leser wiederholen einfach die Kopie.
    void BeginWrite()
    {
        seq_ = seq_ + 1;
        __DMB();
    }
    void EndWrite()
    {
        __DMB();
        seq_ = seq_ + 1;
    }

    Config             cfg_;
    Receiver           rx_;
    daisy::TimerHandle timer_;
    const uint16_t*    adc_  = nullptr;
    daisy::GateIn*     gate_ = nullptr;
    volatile uint32_t  seq_  = 0;
};

} // namespace droid_link
