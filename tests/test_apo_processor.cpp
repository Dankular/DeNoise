// SPDX-License-Identifier: Apache-2.0
//
// Exercises the portable half of the Windows APO backend
// (platform/windows/denoise_apo_processor.h) -- the async worker-thread +
// ring-buffer architecture that keeps ONNX Runtime off the realtime
// callback path, same shape as platform/linux's filter and
// platform/macos's driver. This has no Windows dependency, so unlike
// DeNoiseApo.h (the actual COM shim) it is built and run right here.

#include "denoise_apo_processor.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "test_util.h"

namespace {
std::string modelPath(const char* filename) {
    return std::string(DENOISE_TEST_MODELS_DIR) + "/" + filename;
}
}  // namespace

int main() {
    using denoise::windows::ApoProcessor;

    constexpr std::size_t kFrameSize = 480;
    constexpr int kNumFrames = 20;

    ApoProcessor processor(kFrameSize, 48000.0);
    std::string err;
    DENOISE_CHECK(processor.loadDenoiseModel(modelPath("gain_denoise.onnx"), &err));

    processor.start();

    // Feed kNumFrames distinct constant-valued frames, one per
    // processInPlace call (matching the pipeline's frame size exactly, as
    // a real APO buffer callback would for a fixed-quantum graph).
    std::vector<float> expectedValues;
    std::vector<float> observedNonSilent;

    auto collect = [&](std::vector<float>& buf) {
        for (float v : buf) {
            if (v != 0.0f) {
                if (observedNonSilent.empty() || observedNonSilent.back() != v) {
                    observedNonSilent.push_back(v);
                }
            }
        }
    };

    for (int i = 0; i < kNumFrames; ++i) {
        float value = 0.01f * static_cast<float>(i + 1);
        expectedValues.push_back(value * 0.5f);  // gain_denoise.onnx multiplies by 0.5

        std::vector<float> buf(kFrameSize, value);
        processor.processInPlace(buf.data(), static_cast<std::uint32_t>(buf.size()));
        collect(buf);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Flush: keep pulling (with silent input) until the worker has caught
    // up or we give up after a generous timeout.
    for (int i = 0; i < 100 && observedNonSilent.size() < expectedValues.size(); ++i) {
        std::vector<float> buf(kFrameSize, 0.0f);
        processor.processInPlace(buf.data(), static_cast<std::uint32_t>(buf.size()));
        collect(buf);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    processor.stop();

    std::printf("expected %zu distinct frame values, observed %zu\n", expectedValues.size(),
                observedNonSilent.size());
    DENOISE_CHECK(observedNonSilent.size() == expectedValues.size());
    for (std::size_t i = 0; i < expectedValues.size(); ++i) {
        DENOISE_CHECK_NEAR(observedNonSilent[i], expectedValues[i], 1e-5);
    }

    std::printf("test_apo_processor: OK\n");
    return 0;
}
