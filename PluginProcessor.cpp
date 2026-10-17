#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr double kMaxSampleSeconds = 180.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout DoesitSamplerProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "volume", 1 }, "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "grain", 1 }, "Grain",
        juce::NormalisableRange<float>(5.0f, 250.0f, 0.1f, 0.5f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "root", 1 }, "Root", 0, 127, 60,
        juce::AudioParameterIntAttributes().withStringFromValueFunction(
            [](int v, int) { return juce::MidiMessage::getMidiNoteName(v, true, true, 5); })));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "gate", 1 }, "Gate", false));

    return layout;
}

DoesitSamplerProcessor::DoesitSamplerProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
    volumeParam = apvts.getRawParameterValue("volume");
    grainParam  = apvts.getRawParameterValue("grain");
    rootParam   = apvts.getRawParameterValue("root");
    gateParam   = apvts.getRawParameterValue("gate");
    formatManager.registerBasicFormats();
}

bool DoesitSamplerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DoesitSamplerProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    grainSmooth = grainParam->load() * 0.001f * (float) sampleRate;
    grainCoef = 1.0f - std::exp(-1.0f / (0.03f * (float) sampleRate));
    resetVoices.store(true);
}

std::shared_ptr<DoesitSamplerProcessor::SampleData> DoesitSamplerProcessor::getSample() const
{
    const juce::ScopedLock sl(dataLock);
    return data;
}

void DoesitSamplerProcessor::setRegion(int startSample, int endSample)
{
    regionStart.store(startSample);
    regionEnd.store(endSample);
    apvts.state.setProperty("sampleStart", startSample, nullptr);
    apvts.state.setProperty("sampleEnd", endSample, nullptr);
}

bool DoesitSamplerProcessor::loadSample(const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Dateiformat nicht unterstuetzt (wav, aiff, flac, ogg)";
        return false;
    }

    const juce::int64 length = reader->lengthInSamples;
    if (length <= 0 || reader->sampleRate <= 0.0)
    {
        error = "Datei ist leer";
        return false;
    }
    if ((double) length / reader->sampleRate > kMaxSampleSeconds)
    {
        error = "Sample zu lang (max. 180 Sekunden)";
        return false;
    }

    auto d = std::make_shared<SampleData>();
    const int n = (int) length;
    d->audio.setSize(2, n);
    d->audio.clear();
    reader->read(&d->audio, 0, n, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        d->audio.copyFrom(1, 0, d->audio, 0, 0, n);
    d->rate = reader->sampleRate;
    d->name = file.getFileName();
    d->path = file.getFullPathName();

    std::shared_ptr<SampleData> old;
    {
        const juce::ScopedLock sl(dataLock);
        old = data;
        data = d;
    }
    regionStart.store(0);
    regionEnd.store(n);
    apvts.state.setProperty("samplePath", d->path, nullptr);
    apvts.state.setProperty("sampleStart", 0, nullptr);
    apvts.state.setProperty("sampleEnd", n, nullptr);
    resetVoices.store(true);
    sampleVersion.fetch_add(1);

    // Das alte Sample erst spaeter freigeben, damit der Audio-Thread es nie freigeben muss
    juce::Timer::callAfterDelay(1500, [old]() mutable { old.reset(); });
    return true;
}

void DoesitSamplerProcessor::handleMidi(const juce::MidiMessage& msg, const SampleView& sv,
                                        double srRatio, double grainLen, int rootNote, bool gate)
{
    if (msg.isNoteOn())
    {
        const int note = msg.getNoteNumber();
        GrainVoice* target = nullptr;
        for (auto& v : voices)
            if (! v.active) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.age < target->age) target = &v;
        }

        const int s = juce::jlimit(0, juce::jmax(0, sv.length - 1), regionStart.load());
        const double pitch = std::pow(2.0, (double) (note - rootNote) / 12.0);
        target->start(note, msg.getFloatVelocity(), (double) s, grainLen, pitch, srRatio, ++voiceCounter);
    }
    else if (msg.isNoteOff())
    {
        if (gate)
            for (auto& v : voices)
                if (v.active && v.note == msg.getNoteNumber()) v.release();
    }
    else if (msg.isAllNotesOff())
    {
        for (auto& v : voices) v.release();
    }
    else if (msg.isAllSoundOff())
    {
        for (auto& v : voices) v.active = false;
    }
}

void DoesitSamplerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const juce::ScopedTryLock lock(dataLock);
    if (! lock.isLocked() || data == nullptr || data->audio.getNumSamples() == 0)
    {
        playheadNorm.store(-1.0f);
        return;
    }

    if (resetVoices.exchange(false))
        for (auto& v : voices) v.active = false;

    SampleView sv;
    sv.ch[0] = data->audio.getReadPointer(0);
    sv.ch[1] = data->audio.getReadPointer(1);
    sv.length = data->audio.getNumSamples();
    sv.rate = data->rate;

    const double sr = currentSampleRate;
    const double srRatio = sv.rate / sr;
    const float volume = volumeParam->load();
    const float grainTarget = juce::jmax(64.0f, grainParam->load() * 0.001f * (float) sr);
    const int rootNote = (int) (rootParam->load() + 0.5f);
    const bool gate = gateParam->load() > 0.5f;
    const float attackStep = 1.0f / (float) (0.002 * sr);
    const float releaseStep = 1.0f / (float) (0.012 * sr);
    const double endPos = (double) juce::jlimit(1, sv.length, regionEnd.load());

    const int numSamples = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);

    auto it = midi.begin();
    for (int i = 0; i < numSamples; ++i)
    {
        grainSmooth += grainCoef * (grainTarget - grainSmooth);
        const double grainLen = (double) juce::jmax(64.0f, grainSmooth);

        while (it != midi.end() && (*it).samplePosition <= i)
        {
            handleMidi((*it).getMessage(), sv, srRatio, grainLen, rootNote, gate);
            ++it;
        }

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
            v.render(sv, endPos, grainLen, srRatio, attackStep, releaseStep, l, r);

        outL[i] = l * volume;
        outR[i] = r * volume;
    }

    // Position der zuletzt gestarteten Stimme fuer die Anzeige
    const GrainVoice* newest = nullptr;
    for (const auto& v : voices)
        if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
    playheadNorm.store(newest != nullptr ? (float) (newest->pos / (double) sv.length) : -1.0f);
}

juce::AudioProcessorEditor* DoesitSamplerProcessor::createEditor()
{
    return new DoesitSamplerEditor(*this);
}

void DoesitSamplerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DoesitSamplerProcessor::setStateInformation(const void* stateData, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(stateData, sizeInBytes))
    {
        if (! xml->hasTagName(apvts.state.getType()))
            return;

        apvts.replaceState(juce::ValueTree::fromXml(*xml));

        const juce::String path = apvts.state.getProperty("samplePath").toString();
        if (path.isNotEmpty())
        {
            const int s = (int) apvts.state.getProperty("sampleStart", 0);
            const int e = (int) apvts.state.getProperty("sampleEnd", 0);
            juce::String error;
            if (loadSample(juce::File(path), error) && e > s)
                setRegion(juce::jlimit(0, getSample()->audio.getNumSamples() - 1, s),
                          juce::jlimit(1, getSample()->audio.getNumSamples(), e));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DoesitSamplerProcessor();
}
