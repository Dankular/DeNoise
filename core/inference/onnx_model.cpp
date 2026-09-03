// SPDX-License-Identifier: Apache-2.0
#include "denoise/onnx_model.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <stdexcept>

namespace denoise {

namespace {
// ONNX Runtime's own guidance is to create a single Ort::Env per process and
// share it across sessions, rather than one per Session. Shared here as a
// function-local static (constructed on first use, destroyed at process
// exit) instead of a plain global to avoid static-init-order issues.
Ort::Env& sharedEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "denoise");
    return env;
}
}  // namespace

OnnxModel::OnnxModel() = default;
OnnxModel::~OnnxModel() = default;

bool OnnxModel::load(const std::string& path, std::string* errorOut) {
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        session_ = std::make_unique<Ort::Session>(sharedEnv(), path.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;

        const std::size_t inCount = session_->GetInputCount();
        const std::size_t outCount = session_->GetOutputCount();

        inputName_.clear();
        stateInName_.clear();
        for (std::size_t i = 0; i < inCount; ++i) {
            auto namePtr = session_->GetInputNameAllocated(i, allocator);
            const std::string name = namePtr.get();
            auto shape = session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() != 1 || shape[0] < 0) {
                if (errorOut) *errorOut = "input '" + name + "' must be a 1-D tensor with a fixed size";
                session_.reset();
                return false;
            }
            if (name == "input") {
                inputName_ = name;
                inputSize_ = static_cast<std::size_t>(shape[0]);
            } else if (name == "state_in") {
                stateInName_ = name;
                stateSize_ = static_cast<std::size_t>(shape[0]);
            } else {
                if (errorOut) *errorOut = "unexpected input tensor name '" + name + "'";
                session_.reset();
                return false;
            }
        }

        outputName_.clear();
        stateOutName_.clear();
        for (std::size_t i = 0; i < outCount; ++i) {
            auto namePtr = session_->GetOutputNameAllocated(i, allocator);
            const std::string name = namePtr.get();
            if (name == "output") {
                outputName_ = name;
            } else if (name == "state_out") {
                stateOutName_ = name;
            } else {
                if (errorOut) *errorOut = "unexpected output tensor name '" + name + "'";
                session_.reset();
                return false;
            }
        }

        if (inputName_.empty() || outputName_.empty()) {
            if (errorOut) *errorOut = "model must declare an 'input' tensor and an 'output' tensor";
            session_.reset();
            return false;
        }

        hasState_ = !stateInName_.empty() && !stateOutName_.empty();
        if (!stateInName_.empty() != !stateOutName_.empty()) {
            if (errorOut) *errorOut = "model must declare both state_in and state_out, or neither";
            session_.reset();
            return false;
        }

        state_.assign(hasState_ ? stateSize_ : 0, 0.0f);
        return true;
    } catch (const Ort::Exception& e) {
        if (errorOut) *errorOut = e.what();
        session_.reset();
        return false;
    }
}

void OnnxModel::resetState() {
    std::fill(state_.begin(), state_.end(), 0.0f);
}

std::vector<float> OnnxModel::run(const std::vector<float>& input) {
    if (!isLoaded()) {
        throw std::runtime_error("OnnxModel::run: no model loaded");
    }
    if (input.size() != inputSize_) {
        throw std::runtime_error("OnnxModel::run: input size mismatch (expected " +
                                  std::to_string(inputSize_) + ", got " + std::to_string(input.size()) + ")");
    }

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<float> inputCopy = input;  // CreateTensor takes a non-const pointer
    const int64_t inputShape[1] = {static_cast<int64_t>(inputSize_)};

    std::vector<const char*> inputNames;
    std::vector<Ort::Value> inputValues;
    inputNames.push_back(inputName_.c_str());
    inputValues.push_back(Ort::Value::CreateTensor<float>(memInfo, inputCopy.data(), inputCopy.size(),
                                                            inputShape, 1));

    const int64_t stateShape[1] = {static_cast<int64_t>(stateSize_)};
    if (hasState_) {
        inputNames.push_back(stateInName_.c_str());
        inputValues.push_back(
            Ort::Value::CreateTensor<float>(memInfo, state_.data(), state_.size(), stateShape, 1));
    }

    std::vector<const char*> outputNames;
    outputNames.push_back(outputName_.c_str());
    if (hasState_) outputNames.push_back(stateOutName_.c_str());

    Ort::RunOptions runOptions;
    std::vector<Ort::Value> outputs =
        session_->Run(runOptions, inputNames.data(), inputValues.data(), inputValues.size(),
                      outputNames.data(), outputNames.size());

    const float* outData = outputs[0].GetTensorMutableData<float>();
    const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    std::size_t outCount = 1;
    for (auto d : outShape) outCount *= static_cast<std::size_t>(d);
    std::vector<float> result(outData, outData + outCount);

    if (hasState_) {
        const float* newState = outputs[1].GetTensorMutableData<float>();
        std::copy(newState, newState + stateSize_, state_.begin());
    }

    return result;
}

}  // namespace denoise
