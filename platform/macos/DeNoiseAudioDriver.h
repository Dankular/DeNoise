// SPDX-License-Identifier: Apache-2.0
//
// CoreAudio AudioServerPlugIn ("HAL plugin") skeleton for a DeNoise virtual
// microphone on macOS.
//
// STATUS: written against the real CoreAudio/AudioServerPlugIn.h (fetched
// from https://github.com/phracker/MacOSX-SDKs, MacOSX11.3.sdk, while
// writing this -- the vtable shape and method signatures below are copied
// from that header, not reconstructed from memory) but NEVER COMPILED: this
// project has no macOS SDK or Xcode toolchain available in the environment
// it was written in. Treat every line here as unverified until it has
// actually been built and loaded by coreaudiod on macOS. See
// platform/macos/README.md for exactly what is and isn't implemented.

#pragma once

#include <CoreAudio/AudioServerPlugIn.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "denoise/framer.h"
#include "denoise/pipeline.h"
#include "denoise/ring_buffer.h"

namespace denoise::macos {

// AudioObjectIDs this plug-in publishes. kAudioObjectPlugInObject (from
// AudioHardwareBase.h) is mandated by the API to be the plug-in's own ID;
// the rest are arbitrary values private to this driver.
enum ObjectID : AudioObjectID {
    kObjectID_PlugIn = kAudioObjectPlugInObject,
    kObjectID_Device = 2,
    kObjectID_Stream_Input = 3,
};

// State and glue for the one virtual input device this plug-in exposes.
// DoIOOperation() runs on a realtime deadline thread inside coreaudiod's
// plug-in host process; ONNX Runtime's Session::Run() allocates and is not
// realtime-safe (same caveat as platform/linux). So DoIOOperation only
// pushes/pulls fixed-size chunks through lock-free RingBuffers -- the
// actual DenoisePipeline::processFrame() calls happen on a dedicated
// worker thread (workerLoop below), never on the IO thread.
class Driver {
public:
    Driver();
    ~Driver();

    static Driver& instance();  // this plug-in publishes a single device; one Driver suffices

    // Bind the AudioServerPlugInDriverInterface vtable (see
    // DeNoiseAudioDriver.cpp) to a COM-style ref-counted instance and
    // return it as the CFPlugIn factory result. Called from
    // DeNoiseAudioDriverFactory().
    static void* createInterface();

    bool loadDenoiseModel(const std::string& path, std::string* errorOut);

    // IO lifecycle, called from the vtable's StartIO/StopIO.
    OSStatus startIO();
    OSStatus stopIO();

    // Called from the vtable's DoIOOperation for the input stream's
    // "read" operation (kAudioServerPlugInIOOperationReadInput per
    // AudioServerPlugIn.h): fills `outputFrames` (interleaved, mono
    // float32) with `frameCount` frames of processed audio.
    void readProcessedAudio(float* outputFrames, uint32_t frameCount);

private:
    void workerLoop();

    static constexpr std::size_t kFrameSize = 480;   // 10ms @ 48kHz, matches other platforms' default
    static constexpr double kSampleRate = 48000.0;

    denoise::DenoisePipeline pipeline_{kFrameSize};
    denoise::Framer framer_{kFrameSize, kFrameSize};
    denoise::RingBuffer captureRing_{kFrameSize * 16};  // raw samples awaiting a worker frame
    denoise::RingBuffer outputRing_{kFrameSize * 16};   // processed samples awaiting DoIOOperation

    std::atomic<bool> running_{false};
    std::thread worker_;

    // NOTE: this driver currently has no real capture source wired into
    // captureRing_ -- on real hardware this plug-in would need to either
    // (a) itself be a physical-device-backed driver (out of scope for a
    // *virtual* mic), or (b) rely on the host routing another device's
    // output into it, which CoreAudio HAL plugins do not do automatically
    // the way this project's PipeWire filter can via pw-link. Making a
    // CoreAudio virtual mic actually receive another app/device's audio
    // typically means combining this with Aggregate/Multi-Output devices
    // or a second, output-direction stream on this same device that a
    // client (e.g. this project's own capture-and-inject helper) writes
    // into. That piece is unimplemented -- see README.md.
};

}  // namespace denoise::macos

// C entry point CFPlugIn looks up (kAudioServerPlugInTypeUUID factory).
// Must be exported with C linkage from the built bundle.
extern "C" void* DeNoiseAudioDriverFactory(CFAllocatorRef allocator, CFUUIDRef typeUUID);
