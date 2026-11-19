#pragma once
#include "Widgets.h"
#include <juce_dsp/juce_dsp.h>

namespace gx::ui
{
// Data gathered once per UI frame by the editor and handed to every animated component.
struct Frame
{
    static constexpr int kScope = 2048, kBands = 96;
    std::array<float, kScope> scope {};      // most recent mono output samples
    std::array<float, kBands> spectrum {};   // 0..1 log-frequency bands
    float peakL = 0, peakR = 0, rms = 0;
    double time = 0;                         // seconds since editor open
    float energy = 0;                        // smoothed rms, drives "aliveness"
    uint32_t voiceMask = 0;
    int activeVoices = 0, lastNote = -1, arpStep = -1, gateStep = -1, seqStep = -1;
    float lastVelocity = 0, gateLevel = 1, seqValue = 0;
    std::array<float, kNumEnv> env {};
    std::array<float, kNumLfo> lfo {};
    std::array<float, kNumOsc> morph {};
    std::array<float, kNumFilters> cutoff {};
    std::array<float, kMaxVoices> voiceLevel {};
    std::array<float, kNumMacros> macro {};
    bool reduced = false;                    // reduced animation mode
};

struct Animated
{
    virtual ~Animated() = default;
    virtual void tick (const Frame&) = 0;
};

class FrameSource // owned by the editor
{
public:
    explicit FrameSource (InfinitumProcessor& p) : proc (p) {}
    void update (Frame& f, double dt);
private:
    InfinitumProcessor& proc;
    std::vector<float> ring = std::vector<float> (8192, 0.0f);
    int ringPos = 0;
    juce::dsp::FFT fft { 11 };
    std::vector<float> fftBuf = std::vector<float> (4096, 0.0f);
    juce::dsp::WindowingFunction<float> window { 2048, juce::dsp::WindowingFunction<float>::hann };
};

class Scope : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override { trail = juce::Image(); }
    juce::String label = L ("OSCILOSCOPIO", "OSCILLOSCOPE");
private:
    juce::Image trail;
    std::array<float, 512> trace {};
    float energy = 0; double t = 0;
    float dispPeak = 0.0f, levelDb = -90.0f;   // smoothed display peak (AGC) and real output level
};

class Spectrum : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    std::array<float, Frame::kBands> bands {}, peaks {};
};

class VuMeter : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    float l = 0, r = 0, hl = 0, hr = 0;
};

// LED matrix: voices, arp/gate/seq steps, envelopes, LFOs.
class LedMatrix : public juce::Component, public Animated
{
public:
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    Frame f;
    double t = 0;
};

// Decorative circuit traces with pulses whose speed and brightness follow the audio.
class CircuitBus : public juce::Component, public Animated
{
public:
    CircuitBus();
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct Trace { juce::Path path; float length = 1, phase = 0, speed = 1; juce::Colour colour; };
    std::vector<Trace> traces;
    float energy = 0;
    juce::Random rng { 7 };
};

// Oscillator waveform preview (follows modulated morph).
class OscView : public juce::Component, public Animated, private juce::Timer
{
public:
    OscView (InfinitumProcessor& p, int osc);
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;
private:
    void timerCallback() override { rebuild (lastMorph); }
    void rebuild (float morph);
    InfinitumProcessor& proc;
    int osc;
    float lastMorph = -1;
    std::array<float, 256> wave {};
};

class FilterView : public juce::Component, public Animated
{
public:
    FilterView (InfinitumProcessor& p) : proc (p) {}
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    InfinitumProcessor& proc;
    std::array<float, kNumFilters> cut { -1, -1 };
};

class EnvView : public juce::Component, public Animated
{
public:
    EnvView (InfinitumProcessor& p, int env) : proc (p), e (env) {}
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
private:
    InfinitumProcessor& proc;
    int e;
    float level = 0;
};

// LFO display; with shape = Custom it can be drawn with the mouse.
class LfoView : public juce::Component, public Animated
{
public:
    LfoView (InfinitumProcessor& p, int lfo) : proc (p), l (lfo) {}
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { lastX = -1; draw (e); }
    void mouseDrag (const juce::MouseEvent& e) override { draw (e); }
private:
    void draw (const juce::MouseEvent&);
    InfinitumProcessor& proc;
    int l, lastX = -1;
    float value = 0;
};

// Generic bar-step editor bound to a slice of Patterns.
class StepGrid : public juce::Component, public Animated
{
public:
    using Getter = std::function<float& (Patterns&, int)>;
    StepGrid (InfinitumProcessor& p, Getter g, int maxSteps, float lo, float hi, int stepsParam, int playIndexKind);
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    juce::Colour colour = col::accent;
    bool quantize = false;  // integer values (ratchet/transpose)
    juce::String label;
private:
    void edit (const juce::MouseEvent&);
    InfinitumProcessor& proc;
    Getter get;
    int maxSteps, stepsParam, kind, play = -1;
    float lo, hi;
};
}
