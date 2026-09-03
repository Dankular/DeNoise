// SPDX-License-Identifier: Apache-2.0
//
// See DeNoiseAudioDriver.h for the "written but never compiled" caveat and
// the realtime-safety rationale for how DoIOOperation is structured.
//
// This file implements ENOUGH of the AudioServerPlugInDriverInterface for
// the plug-in to plausibly load and publish one virtual input device with
// one mono float32 stream -- it is NOT a spec-complete HAL plugin. Every
// property handler below is explicitly one of:
//   (a) implemented, backed by real (if minimal) data, or
//   (b) a stub returning kAudioHardwareUnknownPropertyError, marked TODO.
// A non-exhaustive list of what a production driver still needs is at the
// bottom of this file and in README.md -- most notably: a controls list
// (volume/mute), available-format enumeration beyond the single format
// this driver hardcodes, and persistent-storage-backed settings via the
// host's CopyFromStorage/WriteToStorage routines.

#include "DeNoiseAudioDriver.h"

#include <cstring>

using denoise::macos::Driver;
using denoise::macos::kObjectID_Device;
using denoise::macos::kObjectID_PlugIn;
using denoise::macos::kObjectID_Stream_Input;

namespace {

// ---- COM-style ref-counted instance -------------------------------------
// AudioServerPlugInDriverRef is `AudioServerPlugInDriverInterface* __nullable *`
// (see AudioServerPlugIn.h) -- a pointer to the vtable-pointer field. So the
// vtable pointer must be the first member of whatever we hand back.
struct DriverInstance {
    AudioServerPlugInDriverInterface* interfacePtr;
    std::atomic<ULONG> refCount{1};
};

AudioServerPlugInDriverInterface gInterface;  // the vtable, filled in below
DriverInstance gInstance{&gInterface};

Driver& driver() { return Driver::instance(); }

// ---- QueryInterface / AddRef / Release ----------------------------------

HRESULT STDMETHODCALLTYPE QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface) {
    if (outInterface == nullptr) return kAudioHardwareIllegalOperationError;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(nullptr, inUUID);
    bool match = CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID) ||
                 CFEqual(requested, IUnknownUUID);
    CFRelease(requested);
    if (!match) {
        *outInterface = nullptr;
        return E_NOINTERFACE;
    }
    static_cast<DriverInstance*>(inDriver)->refCount.fetch_add(1);
    *outInterface = inDriver;
    return S_OK;
}

ULONG STDMETHODCALLTYPE AddRef(void* inDriver) {
    return static_cast<DriverInstance*>(inDriver)->refCount.fetch_add(1) + 1;
}

ULONG STDMETHODCALLTYPE Release(void* inDriver) {
    auto* inst = static_cast<DriverInstance*>(inDriver);
    ULONG n = inst->refCount.fetch_sub(1) - 1;
    // gInstance is a static, never actually freed here; a dynamic-instance
    // driver would delete `inst` at n == 0.
    return n;
}

// ---- Basic operations -----------------------------------------------------

OSStatus Initialize(AudioServerPlugInDriverRef /*inDriver*/, AudioServerPlugInHostRef /*inHost*/) {
    // TODO: stash inHost for PropertiesChanged()/RequestDeviceConfigurationChange()
    // callbacks once this driver supports runtime reconfiguration (e.g. a
    // model hot-swap) that needs to notify the host.
    return kAudioHardwareNoError;
}

OSStatus CreateDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*,
                       AudioObjectID*) {
    return kAudioHardwareUnsupportedOperationError;  // this driver publishes one static device
}

OSStatus DestroyDevice(AudioServerPlugInDriverRef, AudioObjectID) {
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus AddDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*) {
    return kAudioHardwareNoError;
}

OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*) {
    return kAudioHardwareNoError;
}

OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*) {
    return kAudioHardwareNoError;
}

OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*) {
    return kAudioHardwareNoError;
}

// ---- Property operations ---------------------------------------------------
// Minimal, explicitly-scoped set -- see file header.

bool objectExists(AudioObjectID id) {
    return id == kObjectID_PlugIn || id == kObjectID_Device || id == kObjectID_Stream_Input;
}

Boolean HasProperty(AudioServerPlugInDriverRef, AudioObjectID inObjectID, pid_t,
                     const AudioObjectPropertyAddress* inAddress) {
    if (!objectExists(inObjectID)) return false;
    switch (inAddress->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
            return true;
        default:
            break;
    }
    if (inObjectID == kObjectID_Device) {
        switch (inAddress->mSelector) {
            case kAudioDevicePropertyDeviceUID:
            case kAudioDevicePropertyStreams:
            case kAudioDevicePropertyNominalSampleRate:
            case kAudioDevicePropertyIsHidden:
                return true;
            default:
                break;
        }
    }
    if (inObjectID == kObjectID_Stream_Input) {
        switch (inAddress->mSelector) {
            case kAudioStreamPropertyDirection:
            case kAudioStreamPropertyVirtualFormat:
            case kAudioStreamPropertyPhysicalFormat:
                return true;
            default:
                break;
        }
    }
    return false;  // TODO: controls list, available-format arrays, transport type, etc.
}

OSStatus IsPropertySettable(AudioServerPlugInDriverRef, AudioObjectID inObjectID, pid_t,
                             const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable) {
    if (!objectExists(inObjectID)) return kAudioHardwareBadObjectError;
    *outIsSettable = false;  // nothing in this skeleton is host-settable yet
    (void)inAddress;
    return kAudioHardwareNoError;
}

OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID,
                              pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress,
                              UInt32, const void*, UInt32* outDataSize) {
    // Reuses GetPropertyData's logic to avoid duplicating the property
    // table; wasteful (computes the value just to measure it) but simple
    // and correct for the tiny fixed-size properties this driver has.
    UInt8 scratch[256];
    UInt32 used = 0;
    OSStatus status = gInterface.GetPropertyData(inDriver, inObjectID, inClientProcessID, inAddress, 0,
                                                  nullptr, sizeof(scratch), &used, scratch);
    if (status == kAudioHardwareNoError) *outDataSize = used;
    return status;
}

OSStatus GetPropertyData(AudioServerPlugInDriverRef, AudioObjectID inObjectID, pid_t,
                          const AudioObjectPropertyAddress* inAddress, UInt32, const void*,
                          UInt32 inDataSize, UInt32* outDataSize, void* outData) {
    if (!objectExists(inObjectID)) return kAudioHardwareBadObjectError;

    auto put = [&](const void* src, UInt32 size) -> OSStatus {
        if (inDataSize < size) return kAudioHardwareBadPropertySizeError;
        std::memcpy(outData, src, size);
        *outDataSize = size;
        return kAudioHardwareNoError;
    };

    switch (inAddress->mSelector) {
        case kAudioObjectPropertyBaseClass: {
            AudioClassID v = (inObjectID == kObjectID_Stream_Input) ? kAudioStreamClassID
                              : (inObjectID == kObjectID_Device)    ? kAudioDeviceClassID
                                                                     : kAudioPlugInClassID;
            return put(&v, sizeof(v));
        }
        case kAudioObjectPropertyClass: {
            AudioClassID v = (inObjectID == kObjectID_Stream_Input) ? kAudioStreamClassID
                              : (inObjectID == kObjectID_Device)    ? kAudioDeviceClassID
                                                                     : kAudioPlugInClassID;
            return put(&v, sizeof(v));
        }
        case kAudioObjectPropertyOwner: {
            AudioObjectID v = (inObjectID == kObjectID_PlugIn) ? kAudioObjectUnknown
                               : (inObjectID == kObjectID_Device) ? kObjectID_PlugIn
                                                                   : kObjectID_Device;
            return put(&v, sizeof(v));
        }
        case kAudioObjectPropertyName: {
            CFStringRef v = (inObjectID == kObjectID_Device)
                                ? CFSTR("DeNoise Virtual Microphone")
                                : CFSTR("DeNoise");
            return put(&v, sizeof(v));  // caller receives a +0 reference per HAL convention for gettable CFStrings
        }
        case kAudioObjectPropertyManufacturer:
            return put(&(CFStringRef){CFSTR("DeNoise Project")}, sizeof(CFStringRef));
        case kAudioObjectPropertyOwnedObjects: {
            if (inObjectID == kObjectID_PlugIn) {
                AudioObjectID v = kObjectID_Device;
                return put(&v, sizeof(v));
            }
            if (inObjectID == kObjectID_Device) {
                AudioObjectID v = kObjectID_Stream_Input;
                return put(&v, sizeof(v));
            }
            *outDataSize = 0;
            return kAudioHardwareNoError;
        }
        default:
            break;
    }

    if (inObjectID == kObjectID_Device) {
        switch (inAddress->mSelector) {
            case kAudioDevicePropertyDeviceUID:
                return put(&(CFStringRef){CFSTR("com.denoise.virtualmic")}, sizeof(CFStringRef));
            case kAudioDevicePropertyStreams: {
                AudioObjectID v = kObjectID_Stream_Input;
                return put(&v, sizeof(v));
            }
            case kAudioDevicePropertyNominalSampleRate: {
                Float64 v = 48000.0;
                return put(&v, sizeof(v));
            }
            case kAudioDevicePropertyIsHidden: {
                UInt32 v = 0;
                return put(&v, sizeof(v));
            }
            default:
                break;
        }
    }

    if (inObjectID == kObjectID_Stream_Input) {
        switch (inAddress->mSelector) {
            case kAudioStreamPropertyDirection: {
                UInt32 v = 1;  // 1 == input, per AudioStream direction convention
                return put(&v, sizeof(v));
            }
            case kAudioStreamPropertyVirtualFormat:
            case kAudioStreamPropertyPhysicalFormat: {
                AudioStreamBasicDescription v{};
                v.mSampleRate = 48000.0;
                v.mFormatID = kAudioFormatLinearPCM;
                v.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
                v.mBitsPerChannel = 32;
                v.mChannelsPerFrame = 1;
                v.mFramesPerPacket = 1;
                v.mBytesPerFrame = sizeof(float);
                v.mBytesPerPacket = sizeof(float);
                return put(&v, sizeof(v));
            }
            default:
                break;
        }
    }

    return kAudioHardwareUnknownPropertyError;
}

OSStatus SetPropertyData(AudioServerPlugInDriverRef, AudioObjectID, pid_t,
                          const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, const void*) {
    return kAudioHardwareUnsupportedOperationError;  // nothing settable yet
}

// ---- IO operations ----------------------------------------------------------

OSStatus StartIO(AudioServerPlugInDriverRef, AudioObjectID inDeviceObjectID, UInt32) {
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    return driver().startIO();
}

OSStatus StopIO(AudioServerPlugInDriverRef, AudioObjectID inDeviceObjectID, UInt32) {
    if (inDeviceObjectID != kObjectID_Device) return kAudioHardwareBadObjectError;
    return driver().stopIO();
}

OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64* outSampleTime,
                           UInt64* outHostTime, UInt64* outSeed) {
    // Minimal correct-enough implementation: anchor sample time 0 at the
    // current host time, seed constant (this driver never resets its
    // timeline once started). A production driver should track this more
    // carefully to give the host an accurate rate estimate.
    *outSampleTime = 0;
    *outHostTime = mach_absolute_time();
    *outSeed = 1;
    return kAudioHardwareNoError;
}

OSStatus WillDoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32 inOperationID,
                            Boolean* outWillDo, Boolean* outWillDoInPlace) {
    *outWillDo = (inOperationID == kAudioServerPlugInIOOperationReadInput);
    *outWillDoInPlace = true;
    return kAudioHardwareNoError;
}

OSStatus BeginIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
                           const AudioServerPlugInIOCycleInfo*) {
    return kAudioHardwareNoError;
}

OSStatus DoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID inStreamObjectID,
                        UInt32, UInt32 inOperationID, UInt32 inIOBufferFrameSize,
                        const AudioServerPlugInIOCycleInfo*, void* ioMainBuffer, void*) {
    if (inStreamObjectID != kObjectID_Stream_Input) return kAudioHardwareBadObjectError;
    if (inOperationID != kAudioServerPlugInIOOperationReadInput) return kAudioHardwareNoError;

    driver().readProcessedAudio(static_cast<float*>(ioMainBuffer), inIOBufferFrameSize);
    return kAudioHardwareNoError;
}

OSStatus EndIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32,
                         const AudioServerPlugInIOCycleInfo*) {
    return kAudioHardwareNoError;
}

}  // namespace

// ---- Driver implementation --------------------------------------------------

namespace denoise::macos {

Driver::Driver() {
    pipeline_.addStage(StageKind::Vad, "vad");
    pipeline_.addStage(StageKind::Denoise, "denoise");
    pipeline_.addStage(StageKind::SpeakerIsolation, "speaker");
}

Driver::~Driver() { stopIO(); }

Driver& Driver::instance() {
    static Driver d;
    return d;
}

void* Driver::createInterface() { return &gInstance.interfacePtr; }

bool Driver::loadDenoiseModel(const std::string& path, std::string* errorOut) {
    auto* stage = pipeline_.findStage("denoise");
    return stage != nullptr && stage->loadModel(path, errorOut);
}

OSStatus Driver::startIO() {
    if (running_.exchange(true)) return kAudioHardwareNoError;  // already running
    worker_ = std::thread([this] { workerLoop(); });
    return kAudioHardwareNoError;
}

OSStatus Driver::stopIO() {
    if (!running_.exchange(false)) return kAudioHardwareNoError;
    if (worker_.joinable()) worker_.join();
    return kAudioHardwareNoError;
}

void Driver::workerLoop() {
    // NOTE: per the header's caveat, captureRing_ currently has nothing
    // pushing real microphone audio into it (see DeNoiseAudioDriver.h), so
    // this loop only demonstrates the intended shape: drain whatever
    // arrives, run the (potentially slow, allocating) pipeline off the IO
    // thread, and hand the result to outputRing_ for DoIOOperation to
    // consume. Until captureRing_ has a real producer, outputRing_ only
    // ever receives silence via readProcessedAudio()'s underrun path.
    std::vector<float> raw(kFrameSize);
    std::vector<float> frame;
    while (running_.load()) {
        std::size_t got = captureRing_.pop(raw.data(), raw.size());
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

void Driver::readProcessedAudio(float* outputFrames, uint32_t frameCount) {
    std::size_t got = outputRing_.pop(outputFrames, frameCount);
    if (got < frameCount) {
        std::fill(outputFrames + got, outputFrames + frameCount, 0.0f);
    }
}

}  // namespace denoise::macos

// ---- CFPlugIn factory ---------------------------------------------------

extern "C" void* DeNoiseAudioDriverFactory(CFAllocatorRef, CFUUIDRef typeUUID) {
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) return nullptr;

    gInterface.QueryInterface = QueryInterface;
    gInterface.AddRef = AddRef;
    gInterface.Release = Release;
    gInterface.Initialize = Initialize;
    gInterface.CreateDevice = CreateDevice;
    gInterface.DestroyDevice = DestroyDevice;
    gInterface.AddDeviceClient = AddDeviceClient;
    gInterface.RemoveDeviceClient = RemoveDeviceClient;
    gInterface.PerformDeviceConfigurationChange = PerformDeviceConfigurationChange;
    gInterface.AbortDeviceConfigurationChange = AbortDeviceConfigurationChange;
    gInterface.HasProperty = HasProperty;
    gInterface.IsPropertySettable = IsPropertySettable;
    gInterface.GetPropertyDataSize = GetPropertyDataSize;
    gInterface.GetPropertyData = GetPropertyData;
    gInterface.SetPropertyData = SetPropertyData;
    gInterface.StartIO = StartIO;
    gInterface.StopIO = StopIO;
    gInterface.GetZeroTimeStamp = GetZeroTimeStamp;
    gInterface.WillDoIOOperation = WillDoIOOperation;
    gInterface.BeginIOOperation = BeginIOOperation;
    gInterface.DoIOOperation = DoIOOperation;
    gInterface.EndIOOperation = EndIOOperation;

    return Driver::createInterface();
}

// ---- Known-incomplete for a production driver (see also README.md) ------
// - No controls (volume/mute) -- kAudioDevicePropertyControlList unhandled.
// - No available-format enumeration (kAudioStreamPropertyAvailable*Formats);
//   only the single hardcoded 48kHz mono float32 format is exposed.
// - No persistent settings via CopyFromStorage/WriteToStorage.
// - No PropertiesChanged() notifications back to the host (Initialize
//   discards inHost) -- required for anything that can change at runtime.
// - captureRing_ has no real producer (see DeNoiseAudioDriver.h).
// - Packaging (Info.plist, code signing, notarization, the
//   AudioServerPlugIn_LoadingConditions / bundle-suffix-.driver rules
//   described in the top of AudioServerPlugIn.h) is not set up at all --
//   there is no Xcode project or Info.plist in this directory yet.
