// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

namespace denoise {

// Linear-interpolation sample rate converter.
//
// This is deliberately simple: linear interpolation introduces audible
// aliasing/imaging artifacts compared to a proper windowed-sinc or
// polyphase resampler. It is a placeholder good enough for getting the
// pipeline plumbing correct and for non-critical rate matching (e.g. a
// device capturing at 44.1kHz feeding a model trained at 48kHz). Swap in a
// higher-quality resampler (e.g. libsamplerate/soxr) before relying on this
// for production audio quality -- that swap has not been done here.
class LinearResampler {
public:
    LinearResampler(double inputRate, double outputRate);

    // Streaming API: feed input samples, get back as many output samples as
    // are currently determinable. Maintains fractional phase and the last
    // input sample across calls so a continuous stream resamples smoothly.
    std::vector<float> process(const std::vector<float>& input);

    double ratio() const { return outputRate_ / inputRate_; }

private:
    double inputRate_;
    double outputRate_;
    double phase_ = 0.0;   // fractional read position into `pending_`, in input-sample units
    std::vector<float> pending_;  // input samples carried over from the previous call, used
                                   // as interpolation context for the start of the next one
};

}  // namespace denoise
