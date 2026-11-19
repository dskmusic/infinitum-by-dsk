#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>

namespace gx
{
static constexpr int kBins = Wavetable::kBaseLen / 2 + 1;

void Wavetable::build (const std::vector<std::vector<float>>& re, const std::vector<std::vector<float>>& im)
{
    frames = (int) re.size();
    size_t off = 0;
    for (int L = 0; L < kLevels; ++L)
    {
        len[(size_t) L] = juce::jmax (64, kBaseLen >> L);
        offset[(size_t) L] = (int) off;
        off += (size_t) len[(size_t) L] + 1;
    }
    stride = off;
    data.assign (stride * (size_t) frames, 0.0f);

    juce::dsp::FFT fft (11);
    std::vector<float> buf ((size_t) kBaseLen * 2);
    for (int f = 0; f < frames; ++f)
        for (int L = 0; L < kLevels; ++L)
        {
            const int maxH = kMaxHarm >> L;
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int k = 1; k <= maxH && k < kBins; ++k)
            {
                buf[(size_t) k * 2] = re[(size_t) f][(size_t) k];
                buf[(size_t) k * 2 + 1] = im[(size_t) f][(size_t) k];
            }
            fft.performRealOnlyInverseTransform (buf.data());
            const int n = len[(size_t) L], step = kBaseLen / n;
            float* dst = data.data() + (size_t) f * stride + (size_t) offset[(size_t) L];
            for (int i = 0; i < n; ++i) dst[i] = buf[(size_t) (i * step)];
            dst[n] = dst[0];
        }

    float peak = 1.0e-9f;
    for (auto v : data) peak = juce::jmax (peak, std::abs (v));
    for (auto& v : data) v /= peak;
}

std::unique_ptr<Wavetable> Wavetable::fromSpectra (const std::vector<std::vector<float>>& spectra, juce::String nm)
{
    std::vector<std::vector<float>> re (spectra.size(), std::vector<float> (kBins, 0.0f)), im = re;
    for (size_t f = 0; f < spectra.size(); ++f)
        for (size_t k = 1; k < spectra[f].size() && k < (size_t) kBins; ++k)
            im[f][k] = -spectra[f][k]; // sine phase
    std::unique_ptr<Wavetable> w (new Wavetable());
    w->name = std::move (nm);
    w->build (re, im);
    return w;
}

std::unique_ptr<Wavetable> Wavetable::fromFrames (const std::vector<std::vector<float>>& frames, juce::String nm)
{
    juce::dsp::FFT fft (11);
    std::vector<float> buf ((size_t) kBaseLen * 2);
    std::vector<std::vector<float>> re, im;
    for (auto& fr : frames)
    {
        if (fr.empty()) continue;
        std::fill (buf.begin(), buf.end(), 0.0f);
        const float ratio = (float) fr.size() / kBaseLen;
        for (int i = 0; i < kBaseLen; ++i)
        {
            const float x = (float) i * ratio;
            const int a = (int) x;
            const float t = x - (float) a;
            buf[(size_t) i] = fr[(size_t) a % fr.size()] * (1 - t) + fr[(size_t) (a + 1) % fr.size()] * t;
        }
        fft.performRealOnlyForwardTransform (buf.data());
        std::vector<float> r (kBins), m (kBins);
        for (int k = 0; k < kBins; ++k) { r[(size_t) k] = buf[(size_t) k * 2]; m[(size_t) k] = buf[(size_t) k * 2 + 1]; }
        r[0] = m[0] = 0.0f; // remove DC
        re.push_back (std::move (r));
        im.push_back (std::move (m));
    }
    if (re.empty()) { re.assign (1, std::vector<float> (kBins, 0.0f)); im = re; im[0][1] = -1.0f; }
    std::unique_ptr<Wavetable> w (new Wavetable());
    w->name = std::move (nm);
    w->build (re, im);
    return w;
}

std::unique_ptr<Wavetable> Wavetable::fromAudio (const float* d, int n, int cycleLen, juce::String nm)
{
    cycleLen = juce::jmax (16, cycleLen);
    std::vector<std::vector<float>> fr;
    for (int s = 0; s + cycleLen <= n && fr.size() < 256; s += cycleLen)
        fr.emplace_back (d + s, d + s + cycleLen);
    if (fr.empty() && n > 0) fr.emplace_back (d, d + n);
    return fromFrames (fr, std::move (nm));
}

std::vector<std::vector<float>> Wavetable::exportFrames() const
{
    std::vector<std::vector<float>> out;
    for (int f = 0; f < frames; ++f)
    {
        const float* p = frameData (f);
        out.emplace_back (p, p + kBaseLen);
    }
    return out;
}

// ------------------------------------------------------------------ built-ins
namespace
{
constexpr int kFrames = 32, kH = Wavetable::kMaxHarm;
using Spec = std::vector<float>;

float gauss (float x, float c, float w) { const float d = (x - c) / w; return std::exp (-d * d); }
bool isPrime (int k) { if (k < 2) return false; for (int i = 2; i * i <= k; ++i) if (k % i == 0) return false; return true; }

Spec basicShape (float x)
{
    Spec s (kH + 1, 0.0f);
    const float seg = x * 3.0f;
    for (int k = 1; k <= kH; ++k)
    {
        const float sine = k == 1 ? 1.0f : 0.0f;
        const float tri = (k % 2) ? ((((k - 1) / 2) % 2) ? -1.0f : 1.0f) * 8.0f / (kPi * kPi * (float) (k * k)) : 0.0f;
        const float saw = 2.0f / (kPi * (float) k);
        const float sq = (k % 2) ? 4.0f / (kPi * (float) k) : 0.0f;
        float v;
        if (seg < 1) v = sine + (tri - sine) * seg;
        else if (seg < 2) v = tri + (saw - tri) * (seg - 1);
        else v = saw + (sq - saw) * (seg - 2);
        s[(size_t) k] = v;
    }
    return s;
}

std::vector<float> timeFrame (std::function<float (float)> fn)
{
    std::vector<float> t (Wavetable::kBaseLen);
    for (int i = 0; i < Wavetable::kBaseLen; ++i) t[(size_t) i] = fn ((float) i / Wavetable::kBaseLen);
    return t;
}

std::unique_ptr<Wavetable> makeBuiltin (int idx)
{
    const auto name = wavetableNames()[idx];
    std::vector<Spec> spec;
    std::vector<std::vector<float>> time;
    Rng rng (1234u + (uint32_t) idx);
    std::vector<float> randAmp (kH + 1);
    for (auto& r : randAmp) r = rng.uni();

    for (int f = 0; f < kFrames; ++f)
    {
        const float x = (float) f / (kFrames - 1);
        Spec s (kH + 1, 0.0f);
        switch (idx)
        {
            case 0: s = basicShape (x); break;
            case 1: { const float w = 0.5f - 0.45f * x; time.push_back (timeFrame ([w] (float t) { return t < w ? 1.0f : -1.0f; })); continue; }
            case 2: { const float n = 1.0f + x * 63.0f; for (int k = 1; k <= 64; ++k) s[(size_t) k] = (k <= (int) n ? 1.0f : juce::jmax (0.0f, n - (float) (k - 1))) / (float) k; break; }
            case 3: { const float n = 1.0f + x * 63.0f; for (int k = 1; k <= 64; k += 2) s[(size_t) k] = (k <= (int) n ? 1.0f : juce::jmax (0.0f, (n - (float) (k - 2)) * 0.5f)) / (float) k; break; }
            case 4: case 13:
            {
                const float F[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                const float vp = x * 4.0f; const int v0 = juce::jmin (3, (int) vp); const float vt = vp - (float) v0;
                const float f0 = idx == 4 ? 110.0f : 196.0f;
                for (int k = 1; k <= 96; ++k)
                {
                    const float hz = f0 * (float) k; float a = 0;
                    const float g[3] { 1.0f, 0.55f, 0.3f }, bw[3] { 90, 130, 180 };
                    for (int j = 0; j < 3; ++j) a += g[j] * gauss (hz, F[v0][j] + (F[v0 + 1][j] - F[v0][j]) * vt, bw[j] * (idx == 13 ? 1.6f : 1.0f));
                    s[(size_t) k] = (a + 0.02f) / std::sqrt ((float) k) * (idx == 13 ? (0.75f + 0.5f * randAmp[(size_t) k]) : 1.0f);
                }
                break;
            }
            case 5:
            {
                const int h[9] { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
                const float P[4][9] { { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 6, 8, 4, 0, 3, 0, 0, 0 }, { 8, 8, 6, 8, 5, 4, 3, 2, 2 }, { 6, 0, 8, 0, 6, 0, 4, 0, 8 } };
                const float p = x * 3.0f; const int a = juce::jmin (2, (int) p); const float t = p - (float) a;
                for (int j = 0; j < 9; ++j) s[(size_t) h[j]] += (P[a][j] + (P[a + 1][j] - P[a][j]) * t) / 8.0f;
                break;
            }
            case 6: { const int h[10] { 1, 2, 3, 5, 7, 9, 12, 15, 19, 24 }; for (int j = 0; j < 10; ++j) s[(size_t) h[j]] = std::pow (0.7f, (float) j * (1.0f - x)) * (0.4f + 0.6f * gauss ((float) j, 9.0f * x, 3.0f)); break; }
            case 7: { const float r = 1.0f + 7.0f * x; time.push_back (timeFrame ([r] (float t) { return 2.0f * wrap01 (t * r) - 1.0f; })); continue; }
            case 8: { const float g = 1.0f + 9.0f * x; time.push_back (timeFrame ([g] (float t) { return std::sin (g * kPi * 0.5f * std::sin (kTwoPi * t)); })); continue; }
            case 9: { const float g = 0.5f + 19.5f * x * x; time.push_back (timeFrame ([g] (float t) { return std::tanh (g * std::sin (kTwoPi * t)) / std::tanh (g); })); continue; }
            case 10: { const float d = 0.02f + 0.23f * x; for (int k = 1; k <= kH; ++k) s[(size_t) k] = (0.5f + 0.5f * std::cos (kTwoPi * (float) k * d)) / (float) k; break; }
            case 11: for (int k = 1; k <= 128; ++k) s[(size_t) k] = (randAmp[(size_t) k] > 0.45f || k == 1 ? 1.0f : 0.0f) * std::pow ((float) k, -0.6f) * (0.15f + gauss (std::log2 ((float) k), 1.0f + 6.0f * x, 1.3f)); break;
            case 12: { const float lv = std::round (2.0f + 30.0f * (1.0f - x)); time.push_back (timeFrame ([lv] (float t) { return std::floor (t * lv) / lv * 2.0f - 1.0f; })); continue; }
            case 14: { const float c = 1.0f + 6.0f * x; for (int k = 1; k <= 256; ++k) s[(size_t) k] = (isPrime (k) || k == 1 ? 1.0f : 0.08f) * std::pow ((float) k, -0.7f) * (0.2f + gauss (std::log2 ((float) k), c, 1.5f)); break; }
            case 15: { const float d = 0.5f - 0.48f * x; time.push_back (timeFrame ([d] (float t) { const float p = t < d ? t * 0.5f / d : 0.5f + (t - d) * 0.5f / (1.0f - d); return -std::cos (kTwoPi * p); })); continue; }
            case 16: { const float h = 2.0f + 30.0f * x; const int a = (int) h; const float t = h - (float) a; s[1] = 1.0f; s[(size_t) a] += 0.5f * (1 - t); s[(size_t) a + 1] += 0.5f * t; break; }
            case 17: { const float e = 3.0f - 2.5f * x; for (int k = 1; k <= 256; ++k) s[(size_t) k] = std::pow ((float) k, -e); break; }
            case 18: for (int k = 1; k <= kH; ++k) s[(size_t) k] = (k % 2 ? 1.0f : x * 0.8f) / (float) k; break;
            case 19: for (int k = 1; k <= 256; ++k) s[(size_t) k] = (1.0f - x) / (float) k + x * randAmp[(size_t) k] * std::pow ((float) k, -0.5f); break;
            case 20: { const float q = std::exp2 (1.0f + 7.0f * (1.0f - x)); time.push_back (timeFrame ([q, x] (float t) { const float v = std::sin (kTwoPi * t) * (1.0f + x); const float fo = std::abs (v) > 1 ? (v > 0 ? 2 - v : -2 - v) : v; return std::round (fo * q) / q; })); continue; }
            case 21: { const float n = 1.0f + 11.0f * x; time.push_back (timeFrame ([n] (float t) { const float c = 0.95f * std::sin (kTwoPi * t); const int a = (int) n; const float fr = n - (float) a;
                        auto T = [c] (int m) { return std::cos ((float) m * std::acos (juce::jlimit (-1.0f, 1.0f, c))); }; return T (a) * (1 - fr) + T (a + 1) * fr; })); continue; }
            case 22: { const float F = 600.0f + 2000.0f * x; for (int k = 1; k <= 160; ++k) s[(size_t) k] = (0.25f + gauss (110.0f * (float) k, F, 650.0f)) / (float) k; break; }
            case 23: { const float p = 0.05f + 0.45f * x; for (int k = 1; k <= kH; ++k) s[(size_t) k] = std::abs (std::sin (kPi * (float) k * p)) / (float) k; break; }
            default: s[1] = 1.0f; break;
        }
        spec.push_back (std::move (s));
    }
    return time.empty() ? Wavetable::fromSpectra (spec, name) : Wavetable::fromFrames (time, name);
}

struct Builtins
{
    std::vector<std::unique_ptr<Wavetable>> t;
    Builtins() { for (int i = 0; i < 24; ++i) t.push_back (makeBuiltin (i)); }
};
const Builtins& builtins() { static const Builtins b; return b; }
} // namespace

int numBuiltinTables() { return 24; }
const Wavetable& builtinTable (int index) { auto& b = builtins().t; return *b[(size_t) juce::jlimit (0, (int) b.size() - 1, index)]; }

const std::array<std::array<float, kGenomeHarm>, kNumArchetypes>& genomeArchetypes()
{
    static const auto a = []
    {
        std::array<std::array<float, kGenomeHarm>, kNumArchetypes> g {};
        for (int k = 1; k < kGenomeHarm; ++k)
        {
            const float fk = (float) k;
            g[0][(size_t) k] = 1.0f / fk;                                               // saw
            g[1][(size_t) k] = (k % 2) ? 1.0f / fk : 0.0f;                              // square
            g[2][(size_t) k] = (k % 2) ? 1.0f / (fk * fk) : 0.0f;                       // triangle-ish
            g[3][(size_t) k] = (gauss (fk * 120, 730, 120) + 0.6f * gauss (fk * 120, 1090, 160) + 0.05f) / std::sqrt (fk); // "ah"
            g[4][(size_t) k] = (gauss (fk * 120, 570, 100) + 0.5f * gauss (fk * 120, 840, 130) + 0.03f) / std::sqrt (fk);  // "oh"
            g[5][(size_t) k] = (k == 1 || k == 2 || k == 3 || k == 4 || k == 8) ? 0.8f / std::sqrt (fk) : 0.0f;           // organ
            g[6][(size_t) k] = (k % 2 ? 0.25f : 1.0f) / fk;                             // hollow (even heavy)
            g[7][(size_t) k] = std::pow (fk, -0.45f) * (0.4f + 0.6f * std::abs (std::cos (fk * 0.37f))); // bright comb
        }
        return g;
    }();
    return a;
}
} // namespace gx
