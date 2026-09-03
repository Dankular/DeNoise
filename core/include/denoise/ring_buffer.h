// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace denoise {

// Single-producer / single-consumer lock-free ring buffer of float samples.
//
// Intended for handing audio between a realtime callback thread (producer or
// consumer) and a worker thread, without locks or allocation in the hot path.
// Capacity is rounded up to the next power of two so index wraparound is a
// cheap mask instead of a modulo.
//
// Thread-safety: exactly one thread may call push(); exactly one thread
// (which may differ from the push thread) may call pop()/peek(). Calling
// push() from two threads concurrently, or pop() from two threads
// concurrently, is undefined behavior -- this class is SPSC only.
class RingBuffer {
public:
    explicit RingBuffer(std::size_t minCapacity);

    // Writes up to `count` samples from `src`. Returns the number actually
    // written (less than `count` if the buffer is full).
    std::size_t push(const float* src, std::size_t count);

    // Reads up to `count` samples into `dst`, removing them from the buffer.
    // Returns the number actually read (less than `count` if underrun).
    std::size_t pop(float* dst, std::size_t count);

    // Number of samples currently available to read.
    std::size_t availableToRead() const;

    // Number of free slots currently available to write.
    std::size_t availableToWrite() const;

    std::size_t capacity() const { return capacity_; }

    void clear();

private:
    static std::size_t nextPowerOfTwo(std::size_t v);

    std::vector<float> buffer_;
    const std::size_t capacity_;   // power of two
    const std::size_t mask_;
    std::atomic<std::size_t> writeIndex_{0};
    std::atomic<std::size_t> readIndex_{0};
};

}  // namespace denoise
