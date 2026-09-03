// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

// Forward-declare ONNX Runtime C++ types so this header does not force every
// includer to also pull in onnxruntime_cxx_api.h.
namespace Ort {
struct Session;
}  // namespace Ort

namespace denoise {

// Streaming model I/O contract used throughout this project (this is a
// convention THIS project defines for its own stages -- it is not a claim
// about how any third-party model such as RNNoise/DeepFilterNet/Silero
// exports to ONNX. A third-party model must be wrapped/re-exported to match
// this contract before it can be dropped into the pipeline; see
// docs/MODELS.md).
//
// Required tensors:
//   input  : float32, shape [frame_size]      -- one frame of audio in [-1, 1]
//   output : float32, shape depends on stage kind (see docs/MODELS.md)
// Optional tensors (present together or not at all), for recurrent models
// that carry hidden state between frames:
//   state_in  : float32, shape [state_size]
//   state_out : float32, shape [state_size]
// If a model exposes state_in/state_out, OnnxModel owns the state buffer
// and feeds each call's state_out back in as the next call's state_in,
// initialized to zero on load/reset.
class OnnxModel {
public:
    OnnxModel();
    ~OnnxModel();

    OnnxModel(const OnnxModel&) = delete;
    OnnxModel& operator=(const OnnxModel&) = delete;

    // Loads an ONNX model from `path`. Returns false (and leaves the model
    // unloaded) on any failure -- caller decides whether that means
    // "run this stage in passthrough" or "hard error", see Stage.
    bool load(const std::string& path, std::string* errorOut = nullptr);

    bool isLoaded() const { return session_ != nullptr; }

    // Runs one frame of audio through the model. `input` must have exactly
    // `expectedInputSize()` elements once the model is loaded. Returns the
    // raw contents of the "output" tensor. Throws std::runtime_error on
    // shape mismatch or an ONNX Runtime error.
    std::vector<float> run(const std::vector<float>& input);

    // Size of the "input" tensor's single dimension, once loaded.
    std::size_t expectedInputSize() const { return inputSize_; }

    // Resets any recurrent state to zero (does not reload the model).
    void resetState();

private:
    std::unique_ptr<Ort::Session> session_;

    std::size_t inputSize_ = 0;
    bool hasState_ = false;
    std::size_t stateSize_ = 0;
    std::vector<float> state_;  // persisted state_in/state_out buffer

    std::string inputName_;
    std::string outputName_;
    std::string stateInName_;
    std::string stateOutName_;
};

}  // namespace denoise
