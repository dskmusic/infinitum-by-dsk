#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "preset/Render.h"

InfinitumProcessor::InfinitumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "INFINITUM", gx::createLayout())
{
    for (auto& d : gx::paramDefs())
    {
        raw.push_back (apvts.getRawParameterValue (d.id));
        paramPtrs.push_back (apvts.getParameter (d.id));
    }
    clearMidiLearn();
    snapshot.assign (raw.size(), 0.0f);
    for (auto& t : userTables) t.store (nullptr);
    sharedPatterns.write (patterns);
}

InfinitumProcessor::~InfinitumProcessor() = default;

void InfinitumProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    engine.prepare (sampleRate, juce::jmax (blockSize, 16));
    keyboardState.reset();
}

bool InfinitumProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void InfinitumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber();
        if (cc == 1 || cc == 64 || cc == 74 || cc >= 120) continue; // reserved controllers
        if (const int lp = learnParam.exchange (-1); lp >= 0)
        {
            for (auto& c : ccMap) if (c.load() == lp) c.store (-1);
            ccMap[(size_t) cc].store (lp);
        }
        if (const int pi = ccMap[(size_t) cc].load(); pi >= 0)
            paramPtrs[(size_t) pi]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
    }
    for (size_t i = 0; i < raw.size(); ++i) snapshot[i] = raw[i]->load (std::memory_order_relaxed);
    sharedPatterns.read (audioPatterns);

    gx::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tr.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) tr.ppq = *ppq;
            tr.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }

    std::array<const gx::Wavetable*, gx::kNumOsc> ut {};
    for (int i = 0; i < gx::kNumOsc; ++i) ut[(size_t) i] = userTables[(size_t) i].load (std::memory_order_acquire);

    const int total = buffer.getNumSamples();
    engine.process (buffer, midi, snapshot.data(), audioPatterns, tr, ut, samplePtr.load (std::memory_order_acquire));

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (1);
    float pl = 0, pr = 0, sq = 0;
    for (int i = 0; i < total; ++i) { pl = juce::jmax (pl, std::abs (L[i])); pr = juce::jmax (pr, std::abs (R[i])); sq += L[i] * L[i] + R[i] * R[i]; }
    peakL.store (juce::jmax (pl, peakL.load() * 0.9f));
    peakR.store (juce::jmax (pr, peakR.load() * 0.9f));
    rms.store (std::sqrt (sq / juce::jmax (1, total * 2)));
    pushScope (L, R, total);
}

void InfinitumProcessor::pushScope (const float* L, const float* R, int n)
{
    const auto scope = scopeFifo.write (juce::jmin (n, scopeFifo.getFreeSpace()));
    for (int i = 0; i < scope.blockSize1; ++i) scopeBuf[(size_t) (scope.startIndex1 + i)] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < scope.blockSize2; ++i) scopeBuf[(size_t) (scope.startIndex2 + i)] = 0.5f * (L[scope.blockSize1 + i] + R[scope.blockSize1 + i]);
}

int InfinitumProcessor::readScope (float* dest, int maxSamples)
{
    const auto scope = scopeFifo.read (juce::jmin (maxSamples, scopeFifo.getNumReady()));
    for (int i = 0; i < scope.blockSize1; ++i) dest[i] = scopeBuf[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[scope.blockSize1 + i] = scopeBuf[(size_t) (scope.startIndex2 + i)];
    return scope.blockSize1 + scope.blockSize2;
}

void InfinitumProcessor::setParamPlain (int index, float plain)
{
    if (auto* prm = apvts.getParameter (gx::paramDefs()[(size_t) index].id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

void InfinitumProcessor::setUserWavetable (int osc, std::unique_ptr<gx::Wavetable> t)
{
    userTables[(size_t) osc].store (t.get(), std::memory_order_release);
    if (t) tableStore.push_back (std::move (t));
    // ponytail: keep the last 12 tables alive; the audio thread only holds a pointer for one block.
    while (tableStore.size() > 12)
    {
        bool inUse = false;
        for (auto& u : userTables) inUse |= u.load() == tableStore.front().get();
        if (inUse) break;
        tableStore.erase (tableStore.begin());
    }
}

void InfinitumProcessor::setSample (std::unique_ptr<gx::SampleData> s)
{
    samplePtr.store (s.get(), std::memory_order_release);
    if (s) sampleStore.push_back (std::move (s));
    while (sampleStore.size() > 4 && sampleStore.front().get() != samplePtr.load()) sampleStore.erase (sampleStore.begin());
}

void InfinitumProcessor::renderOffline (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double sampleRate, int blockSize)
{
    gx::renderPreset (gx::Preset::capture (*this), midi, sampleRate, blockSize, out);
}

juce::AudioProcessorEditor* InfinitumProcessor::createEditor() { return new InfinitumEditor (*this); }

void InfinitumProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    auto v = gx::Preset::capture (*this).toVar();
    juce::Array<juce::var> map;
    for (int cc = 0; cc < 128; ++cc)
        if (const int pi = ccMap[(size_t) cc].load(); pi >= 0) map.add (juce::Array<juce::var> { cc, gx::paramDefs()[(size_t) pi].id });
    v.getDynamicObject()->setProperty ("midiLearn", map);
    const auto json = juce::JSON::toString (v, true);
    mb.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void InfinitumProcessor::setStateInformation (const void* data, int size)
{
    gx::Preset p;
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), size);
    if (gx::Preset::fromJson (text, p))
    {
        clearMidiLearn();
        if (auto* map = juce::JSON::parse (text).getProperty ("midiLearn", {}).getArray())
            for (auto& e : *map)
                if (auto* pair = e.getArray(); pair != nullptr && pair->size() == 2)
                    if (const int pi = gx::paramIndex ((*pair)[1].toString()); pi >= 0) ccMap[(size_t) juce::jlimit (0, 127, (int) (*pair)[0])].store (pi);
        p.applyTo (*this);
        presetGeneration.fetch_add (1);
    }
}
