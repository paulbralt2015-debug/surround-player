#pragma once
#include "audio_dsp.h"
#include <array>
#include <cstddef>

class ProLogic2Decoder {
public:
    enum Mode { Movie, Music };

    explicit ProLogic2Decoder(double sampleRate);

    void process(const float* inL, const float* inR,
                 float* outFL, float* outFR, float* outC,
                 float* outLFE, float* outRL, float* outRR,
                 size_t numSamples);

    void setEnabled(bool e) { enabled_ = e; }
    bool isEnabled() const { return enabled_; }
    void setMode(Mode m) { mode_ = m; updateCoefficients(); }
    void reset();

private:
    void updateCoefficients();

    double sampleRate_;
    bool enabled_ = true;
    Mode mode_ = Movie;

    std::array<BiquadFilter, 4> phaseShiftL;
    std::array<BiquadFilter, 4> phaseShiftR;

    BiquadFilter surroundHP, surroundLP;
    BiquadFilter surroundHP2, surroundLP2;

    BiquadFilter lfeFL, lfeFR, lfeC, lfeRL, lfeRR;

    DelayLine haasDelayL;
    DelayLine haasDelayR;

    double envL_ = 0.0, envDiff_ = 0.0;
    double steeringCenter_ = 1.0;
    double steeringSurround_ = 1.0;
};
