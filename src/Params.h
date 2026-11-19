#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Single source of truth for every parameter. IDs are FROZEN (sessions/presets depend on them).
namespace gx
{
constexpr int kNumOsc = 4, kNumFilters = 2, kNumEnv = 4, kNumLfo = 4, kNumMacros = 8, kNumModSlots = 16;
constexpr int kMaxVoices = 32, kMaxUnison = 8, kNumFx = 12;
constexpr int kMaxArpSteps = 16, kMaxGateSteps = 32, kMaxSeqSteps = 32, kLfoTableSize = 256;

enum OscType { otWavetable, otAnalog, otFM, otNoise, otMutation };
enum FilterType { ftLp12, ftLp24, ftLadder, ftHp12, ftHp24, ftBp, ftNotch, ftCombPos, ftCombNeg, ftFormant };
enum LfoShape { lsSine, lsTri, lsSawUp, lsSawDown, lsSquare, lsSampleHold, lsSmoothRandom, lsCustom };
enum PlayMode { pmPoly, pmMono, pmLegato };
enum FxType { fxDist, fxChorus, fxFlanger, fxPhaser, fxDelay, fxReverb, fxComp, fxEq, fxCrush, fxWidener, fxShimmer, fxGranular };
enum NoiseType { ntWhite, ntPink, ntBrown, ntCrackle, ntDigital, ntSample };

// Modulation sources (index stored in mod slot "src")
enum ModSource
{
    msNone, msEnv1, msEnv2, msEnv3, msEnv4, msLfo1, msLfo2, msLfo3, msLfo4,
    msMacro1, msMacro2, msMacro3, msMacro4, msMacro5, msMacro6, msMacro7, msMacro8,
    msVelocity, msAftertouch, msModWheel, msPitchBend, msKey, msRandom, msStepSeq, msSlide,
    msNumSources
};

namespace P
{
    enum Global : int
    {
        master, polyphony, playMode, glide, glideLegato, uniVoices, uniDetune, uniSpread, uniBlend,
        bendRange, velSens, mpeOn, filtRouting, filtBalance,
        macro1, macroLast = macro1 + kNumMacros - 1,
        arpOn, arpMode, arpOct, arpRate, arpGate, arpSwing, arpLatch, arpSteps,
        tgOn, tgRate, tgSteps, tgDepth, tgAttack, tgRelease,
        seqRate, seqSteps, seqGlide,
        chordMode, strum, humanize,
        numGlobal
    };
    enum Osc : int
    {
        oOn, oType, oLevel, oPan, oOct, oSemi, oFine, oTable, oMorph, oShape, oSync, oFold,
        oFmAlgo, oFmR2, oFmR3, oFmR4, oFmI2, oFmI3, oFmI4, oMutRate, oNoise, oRandPhase, numOscFields
    };
    enum Filt : int { fOn, fType, fCutoff, fReso, fDrive, fKey, fEnv, numFiltFields };
    enum Env : int { eDelay, eAttack, eHold, eDecay, eSustain, eRelease, eACurve, eDCurve, eRCurve, numEnvFields };
    enum Lfo : int { lShape, lRate, lSync, lDiv, lRetrig, lFade, lPhase, numLfoFields };
    enum Mod : int { mSrc, mDst, mAmt, numModFields };

    constexpr int oscBase  = numGlobal;
    constexpr int filtBase = oscBase + kNumOsc * numOscFields;
    constexpr int envBase  = filtBase + kNumFilters * numFiltFields;
    constexpr int lfoBase  = envBase + kNumEnv * numEnvFields;
    constexpr int modBase  = lfoBase + kNumLfo * numLfoFields;
    constexpr int fxBase   = modBase + kNumModSlots * numModFields;

    constexpr int osc  (int i, int f) { return oscBase  + i * numOscFields  + f; }
    constexpr int filt (int i, int f) { return filtBase + i * numFiltFields + f; }
    constexpr int env  (int i, int f) { return envBase  + i * numEnvFields  + f; }
    constexpr int lfo  (int i, int f) { return lfoBase  + i * numLfoFields  + f; }
    constexpr int mod  (int i, int f) { return modBase  + i * numModFields  + f; }

    enum Fx : int
    {
        distOn = fxBase, distType, distDrive, distTone, distMix,
        choOn, choRate, choDepth, choMix,
        flaOn, flaRate, flaDepth, flaFb, flaMix,
        phaOn, phaRate, phaDepth, phaFb, phaMix,
        dlyOn, dlyDiv, dlyFb, dlyTone, dlyPing, dlyMix,
        revOn, revSize, revDamp, revWidth, revMix,
        cmpOn, cmpThresh, cmpRatio, cmpAttack, cmpRelease, cmpMakeup,
        eqOn, eqLoF, eqLoG, eqM1F, eqM1G, eqM2F, eqM2G, eqHiF, eqHiG, eqQ,
        bitOn, bitBits, bitRate, bitMix,
        widOn, widWidth, widBassMono,
        shmOn, shmSize, shmAmount, shmMix,
        grnOn, grnSize, grnDensity, grnSpread, grnPitch, grnMix,
        numParams
    };
}

enum ParamKind { pkFloat, pkInt, pkBool, pkChoice };

struct ParamDef
{
    int index = -1;
    juce::String id, name, unit;
    ParamKind kind = pkFloat;
    juce::NormalisableRange<float> range;
    float def = 0;
    juce::StringArray choices;
    int destIndex = 0;           // >0 when this parameter is a modulation destination
    juce::String group;          // "osc1", "filter2", "fx.reverb"... used by preset section copy/lock
};

const std::vector<ParamDef>& paramDefs();
int paramIndex (const juce::String& id);                     // -1 if unknown
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Modulation destinations: 0 = none, then parameter dests, then virtual ones.
enum VirtualDest { vdPitchAll, vdPitchOsc1, vdPitchOsc2, vdPitchOsc3, vdPitchOsc4, vdVolume, vdPan, vdNumVirtual };
struct DestDef { juce::String id, name; int param = -1; int virt = -1; };
const std::vector<DestDef>& destDefs();
int destIndex (const juce::String& id);
const juce::StringArray& sourceNames();
const juce::StringArray& sourceIds();

// Tempo divisions (shared by LFOs, arp, gate, seq, delay)
const juce::StringArray& divisionNames();
double divisionBeats (int index);
const juce::StringArray& fxNames();
const juce::StringArray& wavetableNames();
const juce::StringArray& chordNames();
const std::vector<int>& chordIntervals (int mode);
}
