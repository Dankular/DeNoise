// SPDX-License-Identifier: Apache-2.0
#include "denoise/pipeline.h"

#include <cstdio>
#include <vector>

#include "test_util.h"

int main() {
    using namespace denoise;

    constexpr std::size_t kFrameSize = 480;
    DenoisePipeline pipeline(kFrameSize);
    pipeline.addStage(StageKind::Vad, "vad");
    pipeline.addStage(StageKind::Denoise, "denoise");
    pipeline.addStage(StageKind::SpeakerIsolation, "speaker");

    DENOISE_CHECK(pipeline.stages().size() == 3);
    for (auto& s : pipeline.stages()) {
        DENOISE_CHECK(!s->isLoaded());  // no model loaded -> passthrough
    }

    std::vector<float> frame(kFrameSize);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        frame[i] = static_cast<float>(i % 7) * 0.01f - 0.03f;
    }
    std::vector<float> original = frame;

    FrameContext ctx = pipeline.processFrame(frame);

    DENOISE_CHECK(ctx.samples.size() == kFrameSize);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx.samples[i], original[i], 0.0);  // bit-exact passthrough
    }
    DENOISE_CHECK_NEAR(ctx.vadProbability, 1.0, 0.0);
    DENOISE_CHECK_NEAR(ctx.speakerScore, 1.0, 0.0);

    // Wrong frame size must be rejected.
    bool threw = false;
    try {
        pipeline.processFrame(std::vector<float>(kFrameSize - 1, 0.0f));
    } catch (const std::exception&) {
        threw = true;
    }
    DENOISE_CHECK(threw);

    std::printf("test_pipeline_passthrough: OK\n");
    return 0;
}
