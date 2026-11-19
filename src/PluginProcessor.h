#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "dsp/Engine.h"
#include "preset/Preset.h"
#include "gen/History.h"

class InfinitumProcessor : public juce::AudioProcessor
{
public:
    InfinitumProcessor();
    ~InfinitumProcessor() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Infinitum by DSK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    bool supportsMPE() const override { return true; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message-thread API
    juce::AudioProcessorValueTreeState apvts;
    const gx::Patterns& getPatterns() const { return patterns; }
    void setPatterns (const gx::Patterns& p) { patterns = p; sharedPatterns.write (patterns); }
    void setUserWavetable (int osc, std::unique_ptr<gx::Wavetable> t);
    const gx::Wavetable* getUserWavetable (int osc) const { return userTables[(size_t) osc].load(); }
    void setSample (std::unique_ptr<gx::SampleData> s);
    const gx::SampleData* getSample() const { return samplePtr.load(); }
    float getParam (int index) const { return raw[(size_t) index]->load (std::memory_order_relaxed); }
    void setParamPlain (int index, float plainValue); // notifies host
    gx::Telemetry& telemetry() { return engine.tele; }
    gx::PresetMeta currentMeta;          // metadata of the loaded preset
    std::atomic<int> presetGeneration { 0 }; // bumped whenever a full state/preset is applied (UI polls)
    gx::GenHistory genHistory { 200 };      // generator ◀ ▶ history: current session only (UI thread)

    // Offline rendering helper (tool/tests/SFZ export): resets voices, deterministic.
    void renderOffline (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double sampleRate, int blockSize);
    void resetEngine() { engine.reset(); }

    // Scope / meter feed (lock-free SPSC)
    int readScope (float* dest, int maxSamples);
    std::atomic<float> peakL { 0 }, peakR { 0 }, rms { 0 };
    // MIDI learn: CC -> parameter index
    std::atomic<int> learnParam { -1 };
    std::array<std::atomic<int>, 128> ccMap;
    void clearMidiLearn() { for (auto& c : ccMap) c.store (-1); }
    // UI-injected MIDI (on-screen keyboard)
    juce::MidiKeyboardState keyboardState;

private:
    void pushScope (const float* L, const float* R, int n);

    gx::Engine engine;
    std::vector<std::atomic<float>*> raw;
    std::vector<juce::RangedAudioParameter*> paramPtrs;
    std::vector<float> snapshot;
    gx::Patterns patterns, audioPatterns;
    gx::SharedPatterns sharedPatterns;
    std::array<std::atomic<const gx::Wavetable*>, gx::kNumOsc> userTables {};
    std::vector<std::unique_ptr<gx::Wavetable>> tableStore; // ownership (message thread)
    std::atomic<const gx::SampleData*> samplePtr { nullptr };
    std::vector<std::unique_ptr<gx::SampleData>> sampleStore;
    juce::AbstractFifo scopeFifo { 32768 };
    std::vector<float> scopeBuf = std::vector<float> (32768, 0.0f);
    double internalPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfinitumProcessor)
};
