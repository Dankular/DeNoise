// SPDX-License-Identifier: Apache-2.0
#include "denoise/ring_buffer.h"

#include <cstdio>
#include <vector>

#include "test_util.h"

int main() {
    using denoise::RingBuffer;

    // Capacity rounds up to a power of two.
    RingBuffer rb(10);
    DENOISE_CHECK(rb.capacity() == 16);
    DENOISE_CHECK(rb.availableToRead() == 0);
    DENOISE_CHECK(rb.availableToWrite() == 16);

    // Basic push/pop round-trip.
    std::vector<float> in = {1, 2, 3, 4, 5};
    std::size_t written = rb.push(in.data(), in.size());
    DENOISE_CHECK(written == 5);
    DENOISE_CHECK(rb.availableToRead() == 5);

    std::vector<float> out(5, 0.0f);
    std::size_t read = rb.pop(out.data(), out.size());
    DENOISE_CHECK(read == 5);
    for (int i = 0; i < 5; ++i) DENOISE_CHECK_NEAR(out[static_cast<std::size_t>(i)], in[static_cast<std::size_t>(i)], 0.0);
    DENOISE_CHECK(rb.availableToRead() == 0);

    // Overflow: pushing more than capacity only writes what fits.
    std::vector<float> big(20, 7.0f);
    written = rb.push(big.data(), big.size());
    DENOISE_CHECK(written == 16);
    DENOISE_CHECK(rb.availableToWrite() == 0);

    // Underflow: popping more than available only reads what's there, rest of dst untouched.
    rb.clear();
    std::vector<float> few = {9, 9, 9};
    rb.push(few.data(), few.size());
    std::vector<float> dst(10, -1.0f);
    read = rb.pop(dst.data(), dst.size());
    DENOISE_CHECK(read == 3);
    DENOISE_CHECK_NEAR(dst[0], 9.0, 0.0);
    DENOISE_CHECK_NEAR(dst[2], 9.0, 0.0);
    DENOISE_CHECK_NEAR(dst[3], -1.0, 0.0);  // untouched

    // Wraparound: repeatedly push/pop across many cycles (indices run well past
    // capacity many times over), verify FIFO order is preserved and the queue
    // never overflows the bound we intentionally keep it under.
    RingBuffer rb2(8);
    int nextPush = 0;
    int nextExpectedPop = 0;
    for (int cycle = 0; cycle < 1000; ++cycle) {
        float v = static_cast<float>(nextPush++);
        std::size_t w = rb2.push(&v, 1);
        DENOISE_CHECK(w == 1);

        // Keep outstanding (unpopped) samples bounded well under capacity (8)
        // so pushes never fail, while still exercising many index wraps.
        while (nextPush - nextExpectedPop > 5) {
            float got = 0.0f;
            std::size_t r = rb2.pop(&got, 1);
            DENOISE_CHECK(r == 1);
            DENOISE_CHECK_NEAR(got, static_cast<double>(nextExpectedPop), 0.0);
            ++nextExpectedPop;
        }
    }

    std::printf("test_ring_buffer: OK\n");
    return 0;
}
