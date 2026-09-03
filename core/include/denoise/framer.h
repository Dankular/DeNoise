// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace denoise {

// Accumulates an arbitrary-length input stream and slices it into
// fixed-size, fixed-hop frames. hop <= frameSize gives overlapping frames;
// hop == frameSize gives back-to-back non-overlapping frames (the default
// mode used by the current pipeline stages).
class Framer {
public:
    Framer(std::size_t frameSize, std::size_t hopSize);

    void push(const float* samples, std::size_t count);

    // If enough samples are buffered, fills `outFrame` (resized to
    // frameSize) with the next frame and advances the internal buffer by
    // hopSize. Returns true if a frame was produced.
    bool tryGetFrame(std::vector<float>& outFrame);

    std::size_t frameSize() const { return frameSize_; }
    std::size_t hopSize() const { return hopSize_; }
    std::size_t buffered() const { return buffer_.size(); }

private:
    std::size_t frameSize_;
    std::size_t hopSize_;
    std::deque<float> buffer_;
};

// Reassembles a stream of (possibly overlapping) frames back into a
// continuous sample stream via overlap-add. With hop == frameSize and a
// rectangular window (the default), this degenerates to plain
// concatenation.
class OverlapAdder {
public:
    OverlapAdder(std::size_t frameSize, std::size_t hopSize);

    // Adds one processed frame (length must equal frameSize) into the
    // reconstruction buffer and returns the number of newly-finalized
    // samples appended to `outReady` (safe to consume/flush from the front).
    std::size_t addFrame(const std::vector<float>& frame, std::vector<float>& outReady);

private:
    std::size_t frameSize_;
    std::size_t hopSize_;
    std::vector<float> overlapTail_;  // pending samples not yet fully summed
};

}  // namespace denoise
