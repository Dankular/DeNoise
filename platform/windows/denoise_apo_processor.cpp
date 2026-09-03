// SPDX-License-Identifier: Apache-2.0
#include "denoise_apo_processor.h"

#include <algorithm>

namespace denoise::windows {

ApoProcessor::ApoProcessor(std::size_t frameSize, double sampleRate)
    : frameSize_(frameSize),
      sampleRate_(sampleRate),
      pipeline_(frameSize),
      framer_(frameSize, frameSize),
      inputRing_(frameSize * 16),
      outputRing_(frameSize * 16) {
    pipeline_.addStage(StageKind::Vad, "vad");
    pipeline_.addStage(StageKind::Denoise, "denoise");
    pipeline_.addStage(StageKind::SpeakerIsolation, "speaker");
}

ApoProcessor::~ApoProcessor() { stop(); }

bool ApoProcessor::loadVadModel(const std::string& path, std::string* errorOut) {
    auto* s = pipeline_.findStage("vad");
    return s != nullptr && s->loadModel(path, errorOut);
}
bool ApoProcessor::loadDenoiseModel(const std::string& path, std::string* errorOut) {
    auto* s = pipeline_.findStage("denoise");
    return s != nullptr && s->loadModel(path, errorOut);
}
bool ApoProcessor::loadSpeakerModel(const std::string& path, std::string* errorOut) {
    auto* s = pipeline_.findStage("speaker");
    return s != nullptr && s->loadModel(path, errorOut);
}

void ApoProcessor::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { workerLoop(); });
}

void ApoProcessor::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void ApoProcessor::workerLoop() {
    std::vector<float> raw(frameSize_);
    std::vector<float> frame;
    while (running_.load()) {
        std::size_t got = inputRing_.pop(raw.data(), raw.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        framer_.push(raw.data(), got);
        while (framer_.tryGetFrame(frame)) {
            FrameContext ctx = pipeline_.processFrame(frame);
            outputRing_.push(ctx.samples.data(), ctx.samples.size());
        }
    }
}

void ApoProcessor::processInPlace(float* buffer, std::uint32_t frameCount) {
    inputRing_.push(buffer, frameCount);

    std::size_t got = outputRing_.pop(buffer, frameCount);
    if (got < frameCount) {
        // Startup/underrun: pad with silence rather than leaving stale
        // input samples in the "processed" output.
        std::fill(buffer + got, buffer + frameCount, 0.0f);
    }
}

}  // namespace denoise::windows
