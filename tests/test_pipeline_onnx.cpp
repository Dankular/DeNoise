// SPDX-License-Identifier: Apache-2.0
#include "denoise/pipeline.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "test_util.h"

namespace {
constexpr std::size_t kFrameSize = 480;

std::string modelPath(const char* filename) {
    return std::string(DENOISE_TEST_MODELS_DIR) + "/" + filename;
}

std::vector<float> makeTestFrame(float scale, float offset) {
    std::vector<float> frame(kFrameSize);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        frame[i] = std::sin(static_cast<float>(i) * 0.05f) * scale + offset;
    }
    return frame;
}
}  // namespace

static void testIdentityDenoise() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::Denoise, "denoise");
    std::string err;
    DENOISE_CHECK(stage.loadModel(modelPath("identity_denoise.onnx"), &err));
    DENOISE_CHECK(stage.isLoaded());

    std::vector<float> frame = makeTestFrame(0.1f, 0.0f);
    std::vector<float> original = frame;
    FrameContext ctx = pipeline.processFrame(frame);

    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx.samples[i], original[i], 1e-6);
    }
}

static void testGainDenoise() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::Denoise, "denoise");
    DENOISE_CHECK(stage.loadModel(modelPath("gain_denoise.onnx")));

    std::vector<float> frame = makeTestFrame(0.2f, 0.05f);
    std::vector<float> original = frame;
    FrameContext ctx = pipeline.processFrame(frame);

    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx.samples[i], original[i] * 0.5, 1e-6);
    }
}

static void testMeanAbsVad() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::Vad, "vad");
    DENOISE_CHECK(stage.loadModel(modelPath("mean_abs_vad.onnx")));

    std::vector<float> frame = makeTestFrame(0.3f, 0.0f);
    double expectedMean = 0.0;
    for (float v : frame) expectedMean += std::fabs(v);
    expectedMean /= static_cast<double>(frame.size());

    FrameContext ctx = pipeline.processFrame(frame);

    // Vad stage must not modify the audio.
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx.samples[i], frame[i], 1e-6);
    }
    DENOISE_CHECK_NEAR(ctx.vadProbability, expectedMean, 1e-5);
}

static void testFixedGainSpeaker() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::SpeakerIsolation, "speaker");
    DENOISE_CHECK(stage.loadModel(modelPath("fixed_gain_speaker.onnx")));

    std::vector<float> frame = makeTestFrame(0.4f, 0.0f);
    std::vector<float> original = frame;
    FrameContext ctx = pipeline.processFrame(frame);

    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx.samples[i], original[i] * 0.25, 1e-6);
    }
    DENOISE_CHECK_NEAR(ctx.speakerScore, 0.25, 1e-6);
}

static void testStatefulDiffDenoise() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::Denoise, "denoise");
    DENOISE_CHECK(stage.loadModel(modelPath("stateful_diff_denoise.onnx")));

    std::vector<float> frame1 = makeTestFrame(0.1f, 0.0f);
    std::vector<float> frame2 = makeTestFrame(0.2f, 0.1f);

    FrameContext ctx1 = pipeline.processFrame(frame1);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx1.samples[i], frame1[i], 1e-6);  // state_in was zero
    }

    FrameContext ctx2 = pipeline.processFrame(frame2);
    for (std::size_t i = 0; i < kFrameSize; ++i) {
        DENOISE_CHECK_NEAR(ctx2.samples[i], frame2[i] - frame1[i], 1e-6);
    }
}

static void testLoadModelFailureModes() {
    using namespace denoise;
    DenoisePipeline pipeline(kFrameSize);
    Stage& stage = pipeline.addStage(StageKind::Denoise, "denoise");

    std::string err;
    DENOISE_CHECK(!stage.loadModel("/nonexistent/path/model.onnx", &err));
    DENOISE_CHECK(!err.empty());
    DENOISE_CHECK(!stage.isLoaded());

    // Frame size in the pipeline (480) vs. a model built for a different
    // frame size ought to be rejected explicitly rather than silently
    // truncating/padding.
    DenoisePipeline mismatched(240);
    Stage& stage2 = mismatched.addStage(StageKind::Denoise, "denoise");
    DENOISE_CHECK(!stage2.loadModel(modelPath("identity_denoise.onnx"), &err));
    DENOISE_CHECK(!err.empty());
}

int main() {
    testIdentityDenoise();
    testGainDenoise();
    testMeanAbsVad();
    testFixedGainSpeaker();
    testStatefulDiffDenoise();
    testLoadModelFailureModes();
    std::printf("test_pipeline_onnx: OK\n");
    return 0;
}
