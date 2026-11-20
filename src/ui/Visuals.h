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
    // engine state
    std::array<float, kNumLayers> presence {}, layerEnergy {};
    std::array<int, kChordVoices> chord {};
    int rootPc = 9, degree = 0, bellCount = 0, lastBellNote = 72, seed = 1;
    float runGain = 0, evoTime = 0, musTime = 0, elapsed = 0, lastBellPan = 0, light = 0.5f, space = 0.5f, harmony = 0.5f;
    int scale = 1, timerMin = 0, scene = 0;
    float rain = 0, evolution = 0.5f, density = 0.5f;
    bool running = false, frozen = false, playButton = false, hostPlaying = false;
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
    float dispPeak = 0.0f, levelDb = -90.0f;
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

// Generative landscape with four scenes (Valley, Starfield, Nebula, Neon Rain). Every layer has a visual role:
// drone and pad shape the big masses, texture is dust/grain, shimmer is aurora/beams, wind is mist/fog,
// bells are sparks/comets/flying lights; the seed decides the scenery (lake, forest, planet, skyline...).
class Landscape : public juce::Component, public Animated
{
public:
    Landscape();
    void tick (const Frame&) override;
    void paint (juce::Graphics&) override;
    void resized() override { cacheKey = {}; }
    bool showOverlay = true;                 // chord / key / clock texts
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }
private:
    struct Spark { float x, y, vx, vy, life, size; juce::Colour c; int kind; };
    void paintValley (juce::Graphics&, juce::Rectangle<float>);
    void paintStarfield (juce::Graphics&, juce::Rectangle<float>);
    void paintNebula (juce::Graphics&, juce::Rectangle<float>);
    void paintNeon (juce::Graphics&, juce::Rectangle<float>);
    void paintSparks (juce::Graphics&);
    void paintOverlay (juce::Graphics&, juce::Rectangle<float>);
    void rebuildCache();                     // seed/size dependent scenery
    Frame f;
    std::vector<Spark> sparks;
    std::array<juce::Point<float>, 260> stars {};
    std::array<float, 260> twinkle {}, starSize {};
    std::array<float, kNumLayers> pres {}, glow {};
    int lastBells = -1;
    double lastT = 0;
    juce::Random rng { 1234 };
    // seed-dependent scenery
    juce::String cacheKey;
    bool hasLake = false, hasForest = false, hasBirds = false, hasRings = false;
    int numClouds = 3;
    juce::Point<float> planetPos; float planetR = 100; juce::Colour planetCol, planetCol2;
    std::array<juce::Image, 3> skyline;      // Neon Rain building layers (with lit windows)
    struct Sign { juce::Rectangle<float> r; juce::Colour c; float phase; int layer; };
    std::vector<Sign> signs;
    juce::Point<float> arcology;
};

juce::String chordName (int rootPc, int scale, int degree, float harmony); // "Am7", "Fmaj9"...
}
