// SPDX-License-Identifier: Apache-2.0
#include "denoise/framer.h"

#include <algorithm>
#include <stdexcept>

namespace denoise {

Framer::Framer(std::size_t frameSize, std::size_t hopSize)
    : frameSize_(frameSize), hopSize_(hopSize) {
    if (frameSize_ == 0 || hopSize_ == 0 || hopSize_ > frameSize_) {
        throw std::invalid_argument("Framer: require 0 < hopSize <= frameSize");
    }
}

void Framer::push(const float* samples, std::size_t count) {
    buffer_.insert(buffer_.end(), samples, samples + count);
}

bool Framer::tryGetFrame(std::vector<float>& outFrame) {
    if (buffer_.size() < frameSize_) return false;

    outFrame.assign(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frameSize_));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(hopSize_));
    return true;
}

OverlapAdder::OverlapAdder(std::size_t frameSize, std::size_t hopSize)
    : frameSize_(frameSize), hopSize_(hopSize), overlapTail_(frameSize, 0.0f) {
    if (frameSize_ == 0 || hopSize_ == 0 || hopSize_ > frameSize_) {
        throw std::invalid_argument("OverlapAdder: require 0 < hopSize <= frameSize");
    }
}

std::size_t OverlapAdder::addFrame(const std::vector<float>& frame, std::vector<float>& outReady) {
    if (frame.size() != frameSize_) {
        throw std::invalid_argument("OverlapAdder::addFrame: frame size mismatch");
    }

    for (std::size_t i = 0; i < frameSize_; ++i) {
        overlapTail_[i] += frame[i];
    }

    // The first hopSize_ samples can never receive further contributions
    // (the next frame starts hopSize_ samples later), so they are final.
    outReady.insert(outReady.end(), overlapTail_.begin(),
                     overlapTail_.begin() + static_cast<std::ptrdiff_t>(hopSize_));

    std::vector<float> next(frameSize_, 0.0f);
    std::copy(overlapTail_.begin() + static_cast<std::ptrdiff_t>(hopSize_), overlapTail_.end(),
              next.begin());
    overlapTail_.swap(next);

    return hopSize_;
}

}  // namespace denoise
