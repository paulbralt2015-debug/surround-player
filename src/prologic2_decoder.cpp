#include "prologic2_decoder.h"
#include <algorithm>
#include <cmath>

ProLogic2Decoder::ProLogic2Decoder(double sampleRate)
    : sampleRate_(sampleRate),
      haasDelayL(static_cast<size_t>(sampleRate * 0.040)),
      haasDelayR(static_cast<size_t>(sampleRate * 0.040))
{
    updateCoefficients();
}

void ProLogic2Decoder::updateCoefficients() {
    const double freqs[4] = { 220.0, 680.0, 2100.0, 6500.0 };
    const double Q = 1.2;
    for (int i = 0; i < 4; ++i) {
        phaseShiftL[i].setAllPass(sampleRate_, freqs[i], Q);
        phaseShiftR[i].setAllPass(sampleRate_, freqs[i], Q);
    }

    surroundHP.setHighPass(sampleRate_, 100.0, 0.707);
    surroundLP.setLowPass(sampleRate_, 7000.0, 0.707);
    surroundHP2.setHighPass(sampleRate_, 100.0, 0.707);
    surroundLP2.setLowPass(sampleRate_, 7000.0, 0.707);

    const double lfeFreq = 120.0;
    lfeFL.setLowPass(sampleRate_, lfeFreq, 0.707);
    lfeFR.setLowPass(sampleRate_, lfeFreq, 0.707);
    lfeC.setLowPass(sampleRate_, lfeFreq, 0.707);
    lfeRL.setLowPass(sampleRate_, lfeFreq, 0.707);
    lfeRR.setLowPass(sampleRate_, lfeFreq, 0.707);

    double haasMs = (mode_ == Movie) ? 0.025 : 0.015;
    haasDelayL.setDelaySamples(static_cast<size_t>(sampleRate_ * haasMs));
    haasDelayR.setDelaySamples(static_cast<size_t>(sampleRate_ * (haasMs + 0.003)));
}

void ProLogic2Decoder::reset() {
    for (auto& f : phaseShiftL) f.reset();
    for (auto& f : phaseShiftR) f.reset();
    surroundHP.reset(); surroundLP.reset();
    surroundHP2.reset(); surroundLP2.reset();
    lfeFL.reset(); lfeFR.reset(); lfeC.reset();
    lfeRL.reset(); lfeRR.reset();
    haasDelayL.reset(); haasDelayR.reset();
    envL_ = envDiff_ = 0.0;
    steeringCenter_ = steeringSurround_ = 1.0;
}

void ProLogic2Decoder::process(const float* inL, const float* inR,
                                float* outFL, float* outFR, float* outC,
                                float* outLFE, float* outRL, float* outRR,
                                size_t numSamples)
{
    if (!enabled_) {
        for (size_t i = 0; i < numSamples; ++i) {
            outFL[i] = inL[i];
            outFR[i] = inR[i];
            outC[i] = outLFE[i] = outRL[i] = outRR[i] = 0.0f;
        }
        return;
    }

    const double centerGain   = 0.707;
    const double surroundGain = (mode_ == Movie) ? 0.5 : 0.6;
    const double lfeGain      = 0.5;
    const double attackCoef   = 0.003;
    const double releaseCoef  = 0.0003;

    for (size_t i = 0; i < numSamples; ++i) {
        double L = inL[i];
        double R = inR[i];

        double sum  = (L + R) * 0.5;
        double diff = (L - R) * 0.5;

        double absDiff = std::abs(diff);
        double coef = (absDiff > envDiff_) ? attackCoef : releaseCoef;
        envDiff_ += coef * (absDiff - envDiff_);

        double absSum = std::abs(sum);
        coef = (absSum > envL_) ? attackCoef : releaseCoef;
        envL_ += coef * (absSum - envL_);

        double ratio = (envL_ > 1e-6) ? (envDiff_ / envL_) : 0.0;
        ratio = std::clamp(ratio, 0.0, 1.0);

        steeringCenter_   = 1.0 - 0.3 * ratio;
        steeringSurround_ = 0.7 + 0.6 * ratio;

        double center = sum * centerGain * steeringCenter_;

        double diffShifted = diff;
        for (auto& f : phaseShiftL) diffShifted = f.process(diffShifted);

        double Ls_raw = (diff + diffShifted) * 0.5;
        double Rs_raw = (diff - diffShifted) * 0.5;

        double Ls_filtered = surroundLP.process(surroundHP.process(Ls_raw));
        double Rs_filtered = surroundLP2.process(surroundHP2.process(Rs_raw));

        double Ls_delayed = haasDelayL.process(Ls_filtered);
        double Rs_delayed = haasDelayR.process(Rs_filtered);

        double Ls = Ls_delayed * surroundGain * steeringSurround_;
        double Rs = Rs_delayed * surroundGain * steeringSurround_;

        double FL = L - center * 0.5;
        double FR = R - center * 0.5;

        double lfe = lfeFL.process(FL) + lfeFR.process(FR)
                   + lfeC.process(center)
                   + lfeRL.process(Ls) + lfeRR.process(Rs);
        lfe *= lfeGain;

        outFL[i]  = static_cast<float>(FL);
        outFR[i]  = static_cast<float>(FR);
        outC[i]   = static_cast<float>(center);
        outLFE[i] = static_cast<float>(lfe);
        outRL[i]  = static_cast<float>(Ls);
        outRR[i]  = static_cast<float>(Rs);
    }
}
