#pragma once
#include <cmath>
#include <algorithm>

// Reine DSP-Klasse ohne JUCE: eine Sampler-Stimme mit Grain-Pitch.
// Die Abspielposition (pos) laeuft im Originaltempo. Zwei Lesepunkte (A und B) lesen kurze
// Stuecke (Grains) mit dem Tonhoehen-Faktor aus dem Sample und werden ueberblendet.
// Bei Pitch 1.0 kommt exakt das Original heraus, bei anderen Tonhoehen bleibt die Laenge gleich.

struct SampleView
{
    const float* ch[2] = { nullptr, nullptr };
    int length = 0;
    double rate = 44100.0;
};

inline float readCubic(const float* d, int n, double pos)
{
    const int i = (int) std::floor(pos);
    const float f = (float) (pos - (double) i);
    auto at = [d, n](int k) { return (k >= 0 && k < n) ? d[k] : 0.0f; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

struct GrainVoice
{
    bool active = false;
    bool releasing = false;
    int note = -1;
    float velocity = 0.0f;
    unsigned long long age = 0;

    double pitch = 1.0;     // Tonhoehen-Faktor
    double pos = 0.0;       // Abspielposition im Sample (in Sample-Frames der Datei)
    double readA = 0.0, readB = 0.0;
    float m = 0.0f;         // Fensterphase 0..1
    float env = 0.0f;

    void start(int n, float vel, double startPos, double grainLen, double pitchRatio,
               double srRatio, unsigned long long a)
    {
        note = n; velocity = vel; age = a;
        active = true; releasing = false; env = 0.0f;
        pitch = pitchRatio;
        pos = startPos;
        m = 0.0f;
        readB = pos;                                                    // B ist gerade in der Mitte seines Fensters
        readA = pos + srRatio * grainLen * (1.0 - pitch) * 0.5;         // A startet neu, mittig zur Abspielposition
    }

    void release() { if (active) releasing = true; }

    // Addiert ein Sample auf l und r.
    void render(const SampleView& s, double endPos, double grainLen, double srRatio,
                float attackStep, float releaseStep, float& l, float& r)
    {
        if (! active)
            return;

        const float kPi = 3.14159265358979f;
        const float sw = std::sin(kPi * m);
        const float gA = sw * sw;
        const float gB = 1.0f - gA;

        const float outL = readCubic(s.ch[0], s.length, readA) * gA + readCubic(s.ch[0], s.length, readB) * gB;
        const float outR = readCubic(s.ch[1], s.length, readA) * gA + readCubic(s.ch[1], s.length, readB) * gB;

        if (releasing)
        {
            env -= releaseStep;
            if (env <= 0.0f) { env = 0.0f; active = false; }
        }
        else if (env < 1.0f)
        {
            env = std::min(1.0f, env + attackStep);
        }

        const float g = env * velocity;
        l += outL * g;
        r += outR * g;

        const double step = pitch * srRatio;
        readA += step;
        readB += step;
        pos += srRatio;

        const float prev = m;
        m += (float) (1.0 / grainLen);
        if (m >= 1.0f)
        {
            m -= 1.0f;
            readA = pos + srRatio * grainLen * (1.0 - pitch) * 0.5;
        }
        else if (prev < 0.5f && m >= 0.5f)
        {
            readB = pos + srRatio * grainLen * (1.0 - pitch) * 0.5;
        }

        if (! releasing && pos >= endPos)
            releasing = true;
    }
};
