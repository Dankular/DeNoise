// SPDX-License-Identifier: Apache-2.0
#include "denoise/resampler.h"

#include <cstdio>
#include <vector>

#include "test_util.h"

static void testUnityRatioReproducesInputAcrossChunks() {
    // step == 1.0 exactly: linear interpolation at frac=0 reproduces the
    // original samples exactly. One sample is always held back as
    // look-ahead context for the next call, so N input samples split across
    // calls yield N-1 output samples in total (the very last sample is
    // never emitted because there is no "next" sample to interpolate
    // against) -- this is expected streaming-resampler behavior, not a bug.
    denoise::LinearResampler r(8000.0, 8000.0);

    std::vector<float> chunk1, chunk2;
    for (int i = 0; i < 10; ++i) chunk1.push_back(static_cast<float>(i));       // 0..9
    for (int i = 10; i < 20; ++i) chunk2.push_back(static_cast<float>(i));      // 10..19

    std::vector<float> out1 = r.process(chunk1);
    std::vector<float> out2 = r.process(chunk2);

    DENOISE_CHECK(out1.size() == 9);
    DENOISE_CHECK(out2.size() == 10);

    std::vector<float> all(out1.begin(), out1.end());
    all.insert(all.end(), out2.begin(), out2.end());
    DENOISE_CHECK(all.size() == 19);
    for (int i = 0; i < 19; ++i) {
        DENOISE_CHECK_NEAR(all[static_cast<std::size_t>(i)], i, 1e-6);
    }
}

static void testDownsampleByTwo() {
    // step == 2.0: every other input sample, exactly (frac always 0).
    denoise::LinearResampler r(8000.0, 4000.0);
    std::vector<float> input;
    for (int i = 0; i < 10; ++i) input.push_back(static_cast<float>(i));  // 0..9

    std::vector<float> out = r.process(input);
    std::vector<float> expected = {0, 2, 4, 6, 8};
    DENOISE_CHECK(out.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        DENOISE_CHECK_NEAR(out[i], expected[i], 1e-6);
    }
}

static void testUpsampleInterpolatesLinearly() {
    // step == 0.5: every output sample lands either exactly on an input
    // sample or exactly halfway between two, so the expected value is
    // computable by hand.
    denoise::LinearResampler r(4000.0, 8000.0);
    std::vector<float> input = {0.0f, 10.0f, 20.0f, 30.0f};

    std::vector<float> out = r.process(input);
    // Positions consumed while i0+1 < 4: pos = 0, 0.5, 1, 1.5, 2, 2.5 -> stop at pos=3 (i0=3 has no i0+1)
    std::vector<float> expected = {0, 5, 10, 15, 20, 25};
    DENOISE_CHECK(out.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        DENOISE_CHECK_NEAR(out[i], expected[i], 1e-4);
    }
}

int main() {
    testUnityRatioReproducesInputAcrossChunks();
    testDownsampleByTwo();
    testUpsampleInterpolatesLinearly();
    std::printf("test_resampler: OK\n");
    return 0;
}
