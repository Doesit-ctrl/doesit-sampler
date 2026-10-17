#pragma once
#include "PluginProcessor.h"

// Wellenform mit Zoom und Auswahl (Start/Ende)
class WaveformView : public juce::Component
{
public:
    explicit WaveformView(DoesitSamplerProcessor& p) : proc(p) {}

    void setSample(std::shared_ptr<DoesitSamplerProcessor::SampleData> s);
    void setPlayhead(float norm) { if (norm != playhead) { playhead = norm; repaint(); } }

    void paint(juce::Graphics& g) override;
    void resized() override { cacheDirty = true; }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { drag = None; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    enum DragMode { None, DragStart, DragEnd, DragNew };

    int numSamples() const { return sample != nullptr ? sample->audio.getNumSamples() : 0; }
    double xToSample(float x) const;
    float sampleToX(double s) const;
    void clampView();
    void rebuildCache();
    void applyRegion(int s, int e);

    DoesitSamplerProcessor& proc;
    std::shared_ptr<DoesitSamplerProcessor::SampleData> sample;
    juce::Image cache;
    bool cacheDirty = true;
    double viewStart = 0.0, viewLen = 1.0;
    float playhead = -1.0f;
    DragMode drag = None;
    int anchor = 0;
    int downX = 0;
};

class DoesitSamplerEditor : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    explicit DoesitSamplerEditor(DoesitSamplerProcessor&);
    ~DoesitSamplerEditor() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void loadFile(const juce::File& f);
    void refreshFromProcessor();

    DoesitSamplerProcessor& proc;
    WaveformView waveform;
    juce::TextButton loadButton { "Sample laden" };
    juce::Label fileLabel;

    juce::Slider volumeSlider, grainSlider, rootSlider;
    juce::Label volumeLabel, grainLabel, rootLabel;
    juce::ToggleButton gateButton { "Gate (Note-Off stoppt)" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAtt, grainAtt, rootAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DoesitSamplerEditor)
};
