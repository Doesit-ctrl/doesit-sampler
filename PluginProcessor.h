#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include "GrainVoice.h"

// Doesit Sampler, Stufe 1: Sample laden, Wellenform, MIDI-Abspielen mit Grain-Pitch.
class DoesitSamplerProcessor : public juce::AudioProcessor
{
public:
    struct SampleData
    {
        juce::AudioBuffer<float> audio;   // immer 2 Kanaele
        double rate = 44100.0;
        juce::String name;
        juce::String path;
    };

    DoesitSamplerProcessor();
    ~DoesitSamplerProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Doesit Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

    // Fuer die Oberflaeche (Message-Thread)
    bool loadSample(const juce::File& file, juce::String& error);
    std::shared_ptr<SampleData> getSample() const;
    int getSampleVersion() const { return sampleVersion.load(); }
    void setRegion(int startSample, int endSample);
    int getRegionStart() const { return regionStart.load(); }
    int getRegionEnd() const { return regionEnd.load(); }
    float getPlayheadNorm() const { return playheadNorm.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi(const juce::MidiMessage& msg, const SampleView& sv, double srRatio,
                    double grainLen, int rootNote, bool gate);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* grainParam = nullptr;
    std::atomic<float>* rootParam = nullptr;
    std::atomic<float>* gateParam = nullptr;

    juce::AudioFormatManager formatManager;
    mutable juce::CriticalSection dataLock;
    std::shared_ptr<SampleData> data;
    std::atomic<int> sampleVersion { 0 };
    std::atomic<int> regionStart { 0 };
    std::atomic<int> regionEnd { 0 };
    std::atomic<float> playheadNorm { -1.0f };
    std::atomic<bool> resetVoices { false };

    static constexpr int kNumVoices = 8;
    GrainVoice voices[kNumVoices];
    unsigned long long voiceCounter = 0;

    double currentSampleRate = 44100.0;
    float grainSmooth = 0.0f;
    float grainCoef = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DoesitSamplerProcessor)
};
