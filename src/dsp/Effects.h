#pragma once
#include "Common.h"
#include "Filters.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace gx
{
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init (int minSize) { int s = 1; while (s < minSize) s <<= 1; buf.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float d) const // d samples behind the last pushed
    {
        float r = (float) w - 1.0f - d;
        while (r < 0) r += (float) (mask + 1);
        const int i = (int) r;
        const float t = r - (float) i;
        return buf[(size_t) (i & mask)] * (1 - t) + buf[(size_t) ((i + 1) & mask)] * t;
    }
};

struct Fdn // 8-line feedback delay network reverb
{
    std::array<DelayLine, 8> line;
    std::array<float, 8> len {}, lp {}, g {};
    std::array<DelayLine, 4> ap;
    std::array<float, 4> apLen {};
    float damp = 0.3f, modPh = 0;
    double sr = 48000;
    void prepare (double s);
    void clear();
    void set (float size, float dampAmt);
    void process (float inL, float inR, float& outL, float& outR);
};

struct PitchShifter
{
    DelayLine d;
    float phase = 0, win = 2400;
    void prepare (double sr) { d.init ((int) (sr * 0.2)); win = (float) (sr * 0.05); }
    void clear() { d.clear(); phase = 0; }
    float process (float x, float ratio);
};

class Effects
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const float* p, const std::array<int, kNumFx>& order, double bpm);

private:
    void dist (float* L, float* R, int n, const float* p);
    void chorus (float* L, float* R, int n, const float* p);
    void flanger (float* L, float* R, int n, const float* p);
    void phaser (float* L, float* R, int n, const float* p);
    void delay (float* L, float* R, int n, const float* p, double bpm);
    void reverb (float* L, float* R, int n, const float* p);
    void comp (float* L, float* R, int n, const float* p);
    void eq (float* L, float* R, int n, const float* p);
    void crush (float* L, float* R, int n, const float* p);
    void widen (float* L, float* R, int n, const float* p);
    void shimmer (float* L, float* R, int n, const float* p);
    void granular (float* L, float* R, int n, const float* p);

    double sr = 48000;
    int maxBlock = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    float distLp[2] {}, distDc[2] {};
    std::array<bool, kNumFx> wasOn {};
    DelayLine choD[2], flaD[2], dlyD[2];
    float choPh = 0, flaPh = 0, phaPh = 0, flaFbS[2] {}, phaFbS[2] {};
    float phaZ[2][6] {};
    float dlySmooth = -1, dlyLp[2] {}, dlyHp[2] {};
    Fdn rev, shm;
    PitchShifter shift[2];
    float shmFb[2] {};
    float compEnv = 0;
    std::array<std::array<Biquad, 4>, 2> eqF {};
    std::array<float, 9> eqCache {};
    float crushHold[2] {}, crushCount = 0;
    Biquad wideLp[2];
    float wideCacheF = -1;
    // granular
    DelayLine grnBuf[2];
    struct Grain { bool on = false; float pos = 0, inc = 1, len = 0, t = 0, panL = 1, panR = 1; };
    std::array<Grain, 24> grains {};
    float grnTimer = 0;
    Rng rng { 555 };
    Smooth mixS[kNumFx];
};
}
