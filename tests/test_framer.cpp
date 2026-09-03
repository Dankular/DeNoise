// SPDX-License-Identifier: Apache-2.0
#include "denoise/framer.h"

#include <cstdio>
#include <vector>

#include "test_util.h"

static void testNonOverlappingFramer() {
    denoise::Framer framer(4, 4);
    std::vector<float> samples = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    framer.push(samples.data(), samples.size());

    std::vector<float> frame;
    DENOISE_CHECK(framer.tryGetFrame(frame));
    DENOISE_CHECK(frame.size() == 4);
    for (int i = 0; i < 4; ++i) DENOISE_CHECK_NEAR(frame[static_cast<std::size_t>(i)], i, 0.0);

    DENOISE_CHECK(framer.tryGetFrame(frame));
    for (int i = 0; i < 4; ++i) DENOISE_CHECK_NEAR(frame[static_cast<std::size_t>(i)], 4 + i, 0.0);

    // Only 2 samples left (8, 9) -- not enough for another full frame.
    DENOISE_CHECK(!framer.tryGetFrame(frame));
    DENOISE_CHECK(framer.buffered() == 2);
}

static void testOverlappingFramer() {
    denoise::Framer framer(4, 2);
    std::vector<float> samples = {0, 1, 2, 3, 4, 5};
    framer.push(samples.data(), samples.size());

    std::vector<float> frame;
    DENOISE_CHECK(framer.tryGetFrame(frame));
    std::vector<float> expected0 = {0, 1, 2, 3};
    DENOISE_CHECK(frame == expected0);

    DENOISE_CHECK(framer.tryGetFrame(frame));
    std::vector<float> expected1 = {2, 3, 4, 5};
    DENOISE_CHECK(frame == expected1);

    DENOISE_CHECK(!framer.tryGetFrame(frame));
}

static void testOverlapAdderRectangularEqualsConcatenation() {
    denoise::OverlapAdder ola(4, 4);
    std::vector<float> ready;

    std::vector<float> f0 = {1, 2, 3, 4};
    std::vector<float> f1 = {5, 6, 7, 8};
    ola.addFrame(f0, ready);
    ola.addFrame(f1, ready);

    std::vector<float> expected = {1, 2, 3, 4, 5, 6, 7, 8};
    DENOISE_CHECK(ready == expected);
}

static void testOverlapAdderOverlappingSum() {
    // frameSize=4, hop=2: verified by hand against addFrame's algorithm.
    denoise::OverlapAdder ola(4, 2);
    std::vector<float> ready;

    std::vector<float> f0 = {1, 1, 1, 1};
    std::vector<float> f1 = {2, 2, 2, 2};
    ola.addFrame(f0, ready);  // finalizes [1, 1]; accumulator becomes [1, 1, 0, 0]
    ola.addFrame(f1, ready);  // accumulator += f1 -> [3, 3, 2, 2]; finalizes [3, 3]

    std::vector<float> expected = {1, 1, 3, 3};
    DENOISE_CHECK(ready == expected);
}

int main() {
    testNonOverlappingFramer();
    testOverlappingFramer();
    testOverlapAdderRectangularEqualsConcatenation();
    testOverlapAdderOverlappingSum();
    std::printf("test_framer: OK\n");
    return 0;
}
