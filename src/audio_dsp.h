#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class BiquadFilter {
public:
    BiquadFilter() { reset(); }

    void setCoefficients(double b0, double b1, double b2,
                         double a0, double a1, double a2) {
        b[0] = b0 / a0; b[1] = b1 / a0; b[2] = b2 / a0;
        a[1] = a1 / a0; a[2] = a2 / a0;
    }

    void setAllPass(double fs, double f0, double Q) {
        double w0 = 2.0 * M_PI * f0 / fs;
        double alpha = std::sin(w0) / (2.0 * Q);
        double c = std::cos(w0);
        setCoefficients(1.0 - alpha, -2.0 * c, 1.0 + alpha,
                        1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setLowPass(double fs, double f0, double Q) {
        double w0 = 2.0 * M_PI * f0 / fs;
        double alpha = std::sin(w0) / (2.0 * Q);
        double c = std::cos(w0);
        setCoefficients((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0,
                        1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighPass(double fs, double f0, double Q) {
        double w0 = 2.0 * M_PI * f0 / fs;
        double alpha = std::sin(w0) / (2.0 * Q);
        double c = std::cos(w0);
        setCoefficients((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0,
                        1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    inline double process(double x) {
        double y = b[0] * x + z[0];
        z[0] = b[1] * x - a[1] * y + z[1];
        z[1] = b[2] * x - a[2] * y;
        return y;
    }

    void reset() {
        z[0] = z[1] = 0.0;
        for (int i = 0; i < 3; ++i) { b[i] = a[i] = 0.0; }
        b[0] = 1.0; a[0] = 1.0;
    }

private:
    double b[3]{1,0,0};
    double a[3]{1,0,0};
    double z[2]{0,0};
};

class DelayLine {
public:
    DelayLine(size_t maxSamples = 8192)
        : buffer(maxSamples, 0.0), writePos(0), delay(0) {}

    void setDelaySamples(size_t samples) {
        delay = samples % buffer.size();
    }

    inline double process(double x) {
        buffer[writePos] = x;
        size_t readPos = (writePos + buffer.size() - delay) % buffer.size();
        writePos = (writePos + 1) % buffer.size();
        return buffer[readPos];
    }

    void reset() {
        std::fill(buffer.begin(), buffer.end(), 0.0);
        writePos = 0;
    }

private:
    std::vector<double> buffer;
    size_t writePos;
    size_t delay;
};
