// SPDX-License-Identifier: Apache-2.0
#include "denoise/ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace denoise {

std::size_t RingBuffer::nextPowerOfTwo(std::size_t v) {
    if (v < 2) return 2;
    std::size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

RingBuffer::RingBuffer(std::size_t minCapacity)
    : buffer_(nextPowerOfTwo(minCapacity), 0.0f),
      capacity_(nextPowerOfTwo(minCapacity)),
      mask_(capacity_ - 1) {}

std::size_t RingBuffer::push(const float* src, std::size_t count) {
    const std::size_t w = writeIndex_.load(std::memory_order_relaxed);
    const std::size_t r = readIndex_.load(std::memory_order_acquire);
    const std::size_t free = capacity_ - (w - r);
    const std::size_t n = std::min(count, free);

    for (std::size_t i = 0; i < n; ++i) {
        buffer_[(w + i) & mask_] = src[i];
    }
    writeIndex_.store(w + n, std::memory_order_release);
    return n;
}

std::size_t RingBuffer::pop(float* dst, std::size_t count) {
    const std::size_t r = readIndex_.load(std::memory_order_relaxed);
    const std::size_t w = writeIndex_.load(std::memory_order_acquire);
    const std::size_t avail = w - r;
    const std::size_t n = std::min(count, avail);

    for (std::size_t i = 0; i < n; ++i) {
        dst[i] = buffer_[(r + i) & mask_];
    }
    readIndex_.store(r + n, std::memory_order_release);
    return n;
}

std::size_t RingBuffer::availableToRead() const {
    return writeIndex_.load(std::memory_order_acquire) -
           readIndex_.load(std::memory_order_acquire);
}

std::size_t RingBuffer::availableToWrite() const {
    return capacity_ - availableToRead();
}

void RingBuffer::clear() {
    readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
}

}  // namespace denoise
