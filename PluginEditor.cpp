#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::Colour kBg      { 0xff0f1a2e };
    const juce::Colour kPanel   { 0xff16243d };
    const juce::Colour kWave    { 0xfff2efe6 };
    const juce::Colour kAccent  { 0xff5fd0c5 };
    const juce::Colour kText    { 0xffe8e4d8 };
}

//==============================================================================
void WaveformView::setSample(std::shared_ptr<DoesitSamplerProcessor::SampleData> s)
{
    sample = std::move(s);
    viewStart = 0.0;
    viewLen = (double) juce::jmax(1, numSamples());
    cacheDirty = true;
    repaint();
}

double WaveformView::xToSample(float x) const
{
    return viewStart + (double) x / (double) juce::jmax(1, getWidth()) * viewLen;
}

float WaveformView::sampleToX(double s) const
{
    return (float) ((s - viewStart) / viewLen * (double) getWidth());
}

void WaveformView::clampView()
{
    const double n = (double) juce::jmax(1, numSamples());
    viewLen = juce::jlimit(32.0, n, viewLen);
    viewStart = juce::jlimit(0.0, juce::jmax(0.0, n - viewLen), viewStart);
}

void WaveformView::rebuildCache()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    cache = juce::Image(juce::Image::ARGB, w, h, true);
    cacheDirty = false;
    if (sample == nullptr || numSamples() == 0) return;

    juce::Graphics g(cache);
    g.setColour(kWave);
    const float* l = sample->audio.getReadPointer(0);
    const float* r = sample->audio.getReadPointer(1);
    const int n = numSamples();
    const float mid = (float) h * 0.5f;
    const float amp = (float) h * 0.45f;

    for (int x = 0; x < w; ++x)
    {
        const int a = juce::jlimit(0, n - 1, (int) std::floor(viewStart + (double) x / w * viewLen));
        const int b = juce::jlimit(a + 1, n, (int) std::ceil(viewStart + (double) (x + 1) / w * viewLen));
        const int stride = juce::jmax(1, (b - a) / 512);
        float mn = 1.0f, mx = -1.0f;
        for (int i = a; i < b; i += stride)
        {
            const float v = 0.5f * (l[i] + r[i]);
            mn = juce::jmin(mn, v);
            mx = juce::jmax(mx, v);
        }
        if (mn > mx) { mn = mx = 0.0f; }
        g.drawVerticalLine(x, mid - mx * amp, juce::jmax(mid - mx * amp + 1.0f, mid - mn * amp));
    }
}

void WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(kPanel);
    g.drawRect(getLocalBounds(), 1);

    if (numSamples() == 0)
    {
        g.setColour(kText.withAlpha(0.6f));
        g.setFont(16.0f);
        g.drawFittedText("Sample hierher ziehen oder \"Sample laden\" druecken",
                         getLocalBounds().reduced(20), juce::Justification::centred, 2);
        return;
    }

    if (cacheDirty) rebuildCache();
    g.drawImageAt(cache, 0, 0);

    const float xs = sampleToX((double) proc.getRegionStart());
    const float xe = sampleToX((double) proc.getRegionEnd());
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    if (xs > 0) g.fillRect(0.0f, 0.0f, juce::jmin(xs, (float) getWidth()), (float) getHeight());
    if (xe < (float) getWidth()) g.fillRect(juce::jmax(xe, 0.0f), 0.0f, (float) getWidth() - juce::jmax(xe, 0.0f), (float) getHeight());

    g.setColour(kAccent);
    g.drawVerticalLine((int) xs, 0.0f, (float) getHeight());
    g.drawVerticalLine((int) xe, 0.0f, (float) getHeight());

    if (playhead >= 0.0f)
    {
        const float px = sampleToX((double) playhead * (double) numSamples());
        g.setColour(juce::Colours::white);
        g.drawVerticalLine((int) px, 0.0f, (float) getHeight());
    }
}

void WaveformView::applyRegion(int s, int e)
{
    const int n = numSamples();
    s = juce::jlimit(0, juce::jmax(0, n - 1), s);
    e = juce::jlimit(s + 1, juce::jmax(1, n), e);
    proc.setRegion(s, e);
    repaint();
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    if (numSamples() == 0) return;
    downX = e.x;
    const float xs = sampleToX((double) proc.getRegionStart());
    const float xe = sampleToX((double) proc.getRegionEnd());
    if (std::abs((float) e.x - xs) <= 8.0f)       drag = DragStart;
    else if (std::abs((float) e.x - xe) <= 8.0f)  drag = DragEnd;
    else { drag = DragNew; anchor = (int) xToSample((float) e.x); }
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (numSamples() == 0 || drag == None) return;
    const int s = (int) juce::jlimit(0.0, (double) numSamples(), xToSample((float) e.x));
    if (drag == DragStart)
        applyRegion(juce::jmin(s, proc.getRegionEnd() - 1), proc.getRegionEnd());
    else if (drag == DragEnd)
        applyRegion(proc.getRegionStart(), juce::jmax(s, proc.getRegionStart() + 1));
    else if (std::abs(e.x - downX) > 3)
        applyRegion(juce::jmin(anchor, s), juce::jmax(anchor, s));
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent&)
{
    if (numSamples() == 0) return;
    viewStart = 0.0;
    viewLen = (double) numSamples();
    cacheDirty = true;
    applyRegion(0, numSamples());
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (numSamples() == 0) return;
    if (e.mods.isShiftDown())
    {
        viewStart -= (double) w.deltaY * viewLen * 0.5;
    }
    else
    {
        const double anchorSample = xToSample((float) e.x);
        const double frac = (double) e.x / (double) juce::jmax(1, getWidth());
        viewLen *= std::pow(0.5, (double) w.deltaY * 2.0);
        clampView();
        viewStart = anchorSample - frac * viewLen;
    }
    clampView();
    cacheDirty = true;
    repaint();
}

//==============================================================================
DoesitSamplerEditor::DoesitSamplerEditor(DoesitSamplerProcessor& p)
    : AudioProcessorEditor(&p), proc(p), waveform(p)
{
    addAndMakeVisible(waveform);

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Sample waehlen", juce::File(),
                                                      "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        juce::Component::SafePointer<DoesitSamplerEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser& fc)
                             {
                                 if (safe == nullptr) return;
                                 const auto f = fc.getResult();
                                 if (f.existsAsFile()) safe->loadFile(f);
                             });
    };

    fileLabel.setColour(juce::Label::textColourId, kText);
    fileLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(fileLabel);

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        addAndMakeVisible(s);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(l);
    };
    setup(volumeSlider, volumeLabel, "Volume");
    setup(grainSlider, grainLabel, "Grain");
    setup(rootSlider, rootLabel, "Root");

    gateButton.setColour(juce::ToggleButton::textColourId, kText);
    addAndMakeVisible(gateButton);

    auto& apvts = proc.getApvts();
    volumeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "volume", volumeSlider);
    grainAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "grain", grainSlider);
    rootAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "root", rootSlider);
    gateAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "gate", gateButton);

    setSize(760, 440);
    refreshFromProcessor();
    startTimerHz(30);
}

void DoesitSamplerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(kText.withAlpha(0.5f));
    g.setFont(12.0f);
    g.drawText("Mausrad: Zoom   Shift + Mausrad: verschieben   Ziehen: Bereich waehlen   Doppelklick: alles",
               getLocalBounds().removeFromBottom(18).reduced(10, 0), juce::Justification::centredLeft);
}

void DoesitSamplerEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(32);
    loadButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(10);
    fileLabel.setBounds(top);
    area.removeFromTop(8);
    waveform.setBounds(area.removeFromTop(250));
    area.removeFromTop(10);

    auto controls = area.removeFromTop(120);
    const int w = 110;
    auto place = [&](juce::Slider& s, juce::Label& l)
    {
        auto c = controls.removeFromLeft(w);
        l.setBounds(c.removeFromTop(20));
        s.setBounds(c);
        controls.removeFromLeft(10);
    };
    place(volumeSlider, volumeLabel);
    place(grainSlider, grainLabel);
    place(rootSlider, rootLabel);
    gateButton.setBounds(controls.removeFromLeft(220).withSizeKeepingCentre(220, 28));
}

bool DoesitSamplerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg")
            return true;
    }
    return false;
}

void DoesitSamplerEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
        loadFile(juce::File(files[0]));
}

void DoesitSamplerEditor::loadFile(const juce::File& f)
{
    juce::String error;
    if (! proc.loadSample(f, error))
        fileLabel.setText(error, juce::dontSendNotification);
    refreshFromProcessor();
}

void DoesitSamplerEditor::refreshFromProcessor()
{
    lastVersion = proc.getSampleVersion();
    auto s = proc.getSample();
    if (s != nullptr)
    {
        const double secs = (double) s->audio.getNumSamples() / s->rate;
        fileLabel.setText(s->name + "   (" + juce::String(secs, 2) + " s, "
                          + juce::String(s->rate / 1000.0, 1) + " kHz)", juce::dontSendNotification);
    }
    waveform.setSample(s);
}

void DoesitSamplerEditor::timerCallback()
{
    if (proc.getSampleVersion() != lastVersion)
        refreshFromProcessor();
    waveform.setPlayhead(proc.getPlayheadNorm());
}
