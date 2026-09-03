// SPDX-License-Identifier: Apache-2.0
//
// Platform-independent processing logic for the Windows APO, kept separate
// from any COM/Windows-SDK types so it can be built and unit tested on any
// OS (see tests/ -- unlike DeNoiseApo.h, this file has no Windows
// dependency at all).
//
// Realtime-safety note (see also platform/linux and platform/macos, same
// issue): Microsoft's own APO documentation
// (https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
// states real-time APO methods "must not block, use paged memory, or call
// any blocking system routines." ONNX Runtime's Session::Run() allocates
// and is not realtime-safe, so -- exactly as in the other two platform
// backends -- the actual DenoisePipeline::processFrame() calls must not
// happen on the APOProcess call path. This class runs them on a dedicated
// worker thread and hands buffers across via the lock-free RingBuffer.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "denoise/framer.h"
#include "denoise/pipeline.h"
#include "denoise/ring_buffer.h"

namespace denoise::windows {

class ApoProcessor {
public:
    explicit ApoProcessor(std::size_t frameSize = 480, double sampleRate = 48000.0);
    ~ApoProcessor();

    bool loadVadModel(const std::string& path, std::string* errorOut = nullptr);
    bool loadDenoiseModel(const std::string& path, std::string* errorOut = nullptr);
    bool loadSpeakerModel(const std::string& path, std::string* errorOut = nullptr);

    void start();
    void stop();

    // Called from the realtime audio thread (APOProcess, once wired up in
    // DeNoiseApo.h): pushes `frameCount` mono float32 input samples in,
    // and fills `buffer` with `frameCount` samples of the most recently
    // available processed audio -- non-blocking, never calls into the
    // pipeline directly. `buffer` is used for both input and output
    // (in-place), matching how SFX-type APOs typically operate when the
    // input/output format is unchanged; verify this against
    // IAudioProcessingObjectRT::APOProcess's actual connection-buffer
    // semantics in Audioenginebaseapo.h before relying on it.
    void processInPlace(float* buffer, std::uint32_t frameCount);

    double sampleRate() const { return sampleRate_; }
    std::size_t frameSize() const { return frameSize_; }

private:
    void workerLoop();

    std::size_t frameSize_;
    double sampleRate_;

    denoise::DenoisePipeline pipeline_;
    denoise::Framer framer_;
    denoise::RingBuffer inputRing_;
    denoise::RingBuffer outputRing_;

    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace denoise::windows
