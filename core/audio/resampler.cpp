// SPDX-License-Identifier: Apache-2.0
#include "denoise/resampler.h"

#include <stdexcept>

namespace denoise {

LinearResampler::LinearResampler(double inputRate, double outputRate)
    : inputRate_(inputRate), outputRate_(outputRate) {
    if (inputRate_ <= 0.0 || outputRate_ <= 0.0) {
        throw std::invalid_argument("LinearResampler: rates must be positive");
    }
}

std::vector<float> LinearResampler::process(const std::vector<float>& input) {
    std::vector<float> buffer;
    buffer.reserve(pending_.size() + input.size());
    buffer.insert(buffer.end(), pending_.begin(), pending_.end());
    buffer.insert(buffer.end(), input.begin(), input.end());

    std::vector<float> output;
    if (buffer.size() < 2) {
        pending_ = std::move(buffer);
        return output;
    }

    const double step = inputRate_ / outputRate_;
    double pos = phase_;

    while (true) {
        const std::size_t i0 = static_cast<std::size_t>(pos);
        if (i0 + 1 >= buffer.size()) break;
        const double frac = pos - static_cast<double>(i0);
        const float s0 = buffer[i0];
        const float s1 = buffer[i0 + 1];
        output.push_back(static_cast<float>(s0 + (s1 - s0) * frac));
        pos += step;
    }

    std::size_t consumedWhole = static_cast<std::size_t>(pos);
    // For a large downsampling ratio relative to the chunk size, `pos` can
    // overshoot past the end of `buffer` in a single step (e.g. step=100 on
    // a 10-sample buffer). Clamp so `pending_` is always a valid (possibly
    // 1-element) slice of `buffer` -- this trades a little phase accuracy in
    // that extreme-ratio/small-chunk case for guaranteed memory safety.
    if (consumedWhole >= buffer.size()) consumedWhole = buffer.size() - 1;

    phase_ = pos - static_cast<double>(consumedWhole);
    pending_.assign(buffer.begin() + static_cast<std::ptrdiff_t>(consumedWhole), buffer.end());
    return output;
}

}  // namespace denoise
