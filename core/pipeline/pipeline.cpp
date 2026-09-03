// SPDX-License-Identifier: Apache-2.0
#include "denoise/pipeline.h"

#include <algorithm>
#include <stdexcept>

namespace denoise {

const char* toString(StageKind kind) {
    switch (kind) {
        case StageKind::Vad: return "vad";
        case StageKind::Denoise: return "denoise";
        case StageKind::SpeakerIsolation: return "speaker_isolation";
    }
    return "unknown";
}

Stage::Stage(StageKind kind, std::string name, std::size_t frameSize)
    : kind_(kind), name_(std::move(name)), frameSize_(frameSize) {}

bool Stage::loadModel(const std::string& modelPath, std::string* errorOut) {
    std::string err;
    if (!model_.load(modelPath, &err)) {
        if (errorOut) *errorOut = err;
        return false;
    }
    if (model_.expectedInputSize() != frameSize_) {
        if (errorOut) {
            *errorOut = "model '" + modelPath + "' expects input size " +
                        std::to_string(model_.expectedInputSize()) + " but pipeline frame size is " +
                        std::to_string(frameSize_);
        }
        return false;
    }
    return true;
}

void Stage::process(FrameContext& ctx) {
    if (!model_.isLoaded()) return;  // passthrough

    std::vector<float> output = model_.run(ctx.samples);

    switch (kind_) {
        case StageKind::Vad: {
            if (output.empty()) {
                throw std::runtime_error("vad stage '" + name_ + "': model produced an empty output tensor");
            }
            ctx.vadProbability = std::clamp(output[0], 0.0f, 1.0f);
            break;
        }
        case StageKind::Denoise: {
            if (output.size() != ctx.samples.size()) {
                throw std::runtime_error("denoise stage '" + name_ + "': output size " +
                                          std::to_string(output.size()) + " != frame size " +
                                          std::to_string(ctx.samples.size()));
            }
            ctx.samples = std::move(output);
            break;
        }
        case StageKind::SpeakerIsolation: {
            if (output.empty()) {
                throw std::runtime_error("speaker_isolation stage '" + name_ +
                                          "': model produced an empty output tensor");
            }
            const float gain = std::clamp(output[0], 0.0f, 1.0f);
            ctx.speakerScore = gain;
            for (float& s : ctx.samples) s *= gain;
            break;
        }
    }
}

DenoisePipeline::DenoisePipeline(std::size_t frameSize) : frameSize_(frameSize) {
    if (frameSize_ == 0) throw std::invalid_argument("DenoisePipeline: frameSize must be > 0");
}

Stage& DenoisePipeline::addStage(StageKind kind, std::string name) {
    stages_.push_back(std::make_unique<Stage>(kind, std::move(name), frameSize_));
    return *stages_.back();
}

Stage* DenoisePipeline::findStage(const std::string& name) {
    for (auto& s : stages_) {
        if (s->name() == name) return s.get();
    }
    return nullptr;
}

FrameContext DenoisePipeline::processFrame(std::vector<float> frame, bool gateByVad) {
    if (frame.size() != frameSize_) {
        throw std::invalid_argument("DenoisePipeline::processFrame: frame size " +
                                     std::to_string(frame.size()) + " != pipeline frame size " +
                                     std::to_string(frameSize_));
    }

    FrameContext ctx;
    ctx.samples = std::move(frame);

    for (auto& stage : stages_) {
        stage->process(ctx);
    }

    if (gateByVad) {
        for (float& s : ctx.samples) s *= ctx.vadProbability;
    }

    return ctx;
}

}  // namespace denoise
