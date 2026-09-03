// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "denoise/onnx_model.h"

namespace denoise {

enum class StageKind {
    Vad,              // sets ctx.vadProbability; does not modify samples
    Denoise,          // replaces ctx.samples with the model's "output" tensor (same length)
    SpeakerIsolation  // multiplies ctx.samples by a scalar gain from the model's "output" tensor
};

const char* toString(StageKind kind);

// State threaded through the ordered list of stages for a single frame.
struct FrameContext {
    std::vector<float> samples;
    float vadProbability = 1.0f;   // last value set by a Vad stage, 1.0 (unknown/assume speech) if none ran
    float speakerScore = 1.0f;     // last gain applied by a SpeakerIsolation stage
};

// One stage in the pipeline. If no model is loaded, process() is a no-op
// (passthrough) -- this lets the pipeline run end-to-end, unchanged, before
// any real trained weights are available (see docs/MODELS.md).
class Stage {
public:
    Stage(StageKind kind, std::string name, std::size_t frameSize);

    // Returns false (and leaves the stage in passthrough) if the file does
    // not exist, fails to parse, or does not match the tensor contract
    // documented in onnx_model.h. `errorOut`, if given, receives the reason.
    bool loadModel(const std::string& modelPath, std::string* errorOut = nullptr);

    bool isLoaded() const { return model_.isLoaded(); }
    StageKind kind() const { return kind_; }
    const std::string& name() const { return name_; }

    void process(FrameContext& ctx);

private:
    StageKind kind_;
    std::string name_;
    std::size_t frameSize_;
    OnnxModel model_;
};

// Runs an ordered list of stages over successive fixed-size frames.
class DenoisePipeline {
public:
    explicit DenoisePipeline(std::size_t frameSize);

    // Stages run in the order they are added.
    Stage& addStage(StageKind kind, std::string name);

    // Convenience: find a previously-added stage by name (nullptr if absent).
    Stage* findStage(const std::string& name);

    std::size_t frameSize() const { return frameSize_; }
    const std::vector<std::unique_ptr<Stage>>& stages() const { return stages_; }

    // Runs every stage, in order, over one frame (size must equal
    // frameSize()). `gateByVad`, if true, multiplies the final samples by
    // ctx.vadProbability after all stages have run.
    FrameContext processFrame(std::vector<float> frame, bool gateByVad = false);

private:
    std::size_t frameSize_;
    std::vector<std::unique_ptr<Stage>> stages_;
};

}  // namespace denoise
