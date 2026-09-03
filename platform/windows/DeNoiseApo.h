// SPDX-License-Identifier: Apache-2.0
//
// Windows Audio Processing Object (APO) COM shim -- UNVERIFIED SKELETON.
//
// This file requires the Windows SDK's Audioenginebaseapo.h/.idl and
// Baseaudioprocessingobject.h (Windows Kits\<ver>\Include\um), none of
// which are available in the Linux container this project was developed
// in. Unlike core/ and platform/linux (built and run against real headers
// in this repo) and even platform/macos (built against a real, fetched
// Apple header), the exact method signatures below for
// IAudioProcessingObject::Initialize/IsInputFormatSupported,
// IAudioProcessingObjectConfiguration::LockForProcess/UnlockForProcess,
// and IAudioProcessingObjectRT::APOProcess were NOT copied from a real
// header -- they are reconstructed from Microsoft's prose documentation
// (https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)
// and are very likely wrong in some parameter detail (APOProcess's actual
// APO_CONNECTION_PROPERTY buffer-array shape in particular). Do not trust
// this file to compile as-is; verify every signature against the real SDK
// headers -- or better, start from Microsoft's own SYSVAD "Swap APO"
// sample (https://github.com/Microsoft/Windows-driver-samples/tree/main/audio/sysvad),
// which is real, buildable, Microsoft-maintained reference code for
// exactly this kind of APO -- before relying on this.
//
// What this DOES get right (see denoise_apo_processor.h/.cpp, which has no
// Windows dependency and is portable/testable): the actual DSP-pipeline
// wiring, and the requirement (directly from Microsoft's own docs) that
// APOProcess must not block/allocate, which is why it only touches
// ApoProcessor's lock-free ring buffers and never calls ONNX Runtime
// directly.
//
// What this does NOT attempt: this is a Stream Effect (SFX) APO, which
// Microsoft's documentation is explicit modifies audio on an EXISTING
// logical device's capture stream -- it is not a new, independently
// selectable virtual microphone the way platform/linux's pw_filter-based
// approach is. Getting a *new* virtual audio device on Windows (what most
// people mean by "a Krisp-style mic you pick in Zoom") needs a kernel-mode
// WDM/KS audio driver (see the SYSVAD sample's other, non-APO components,
// or prior art like the open-source VB-CABLE-alikes) -- a much larger,
// WDK-based undertaking with its own signing requirements, and genuinely
// out of scope here: guessing at kernel driver structures without being
// able to verify them against real headers would be actively risky advice
// (kernel-mode bugs, unlike a userspace ABI mismatch, can crash the
// system), so this project does not attempt it.

#ifndef _WIN32
#error "DeNoiseApo.h is Windows-only (requires the Windows SDK's Audio Processing Object headers)"
#endif

// #include <audioenginebaseapo.h>       // NOT available in this environment -- verify path/name
// #include <baseaudioprocessingobject.h> // NOT available in this environment -- verify path/name

#include "denoise_apo_processor.h"

namespace denoise::windows {

// Sketched as inheriting from CBaseAudioProcessingObject per Microsoft's
// documented approach (it supplies default implementations for most
// IAudioProcessingObject/IAudioProcessingObjectConfiguration/
// IAudioProcessingObjectRT methods, leaving IsInputFormatSupported,
// ValidateAndCacheConnectionInfo, and APOProcess as the ones a custom SFX
// APO must implement) -- commented out because CBaseAudioProcessingObject
// itself is defined in the unavailable Baseaudioprocessingobject.h.
//
// class DeNoiseApoSFX : public CBaseAudioProcessingObject {
// public:
//     DeNoiseApoSFX();
//
//     // IAudioProcessingObject
//     STDMETHOD(IsInputFormatSupported)(IAudioMediaType* pOppositeFormat, IAudioMediaType* pRequestedFormat,
//                                        IAudioMediaType** ppSupportedFormat) override;
//
//     // IAudioProcessingObjectConfiguration (ValidateAndCacheConnectionInfo is the hook
//     // CBaseAudioProcessingObject::LockForProcess calls per Microsoft's docs)
//     HRESULT ValidateAndCacheConnectionInfo(UINT32 u32NumInputConnections,
//                                             APO_CONNECTION_DESCRIPTOR** ppInputConnections,
//                                             UINT32 u32NumOutputConnections,
//                                             APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
//
//     // IAudioProcessingObjectRT -- the real-time entry point. Signature copied from
//     // Microsoft Learn prose, NOT a header -- verify parameter types/order before use.
//     STDMETHOD_(void, APOProcess)(UINT32 u32NumInputConnections,
//                                  APO_CONNECTION_PROPERTY** ppInputConnections,
//                                  UINT32 u32NumOutputConnections,
//                                  APO_CONNECTION_PROPERTY** ppOutputConnections) override;
//
// private:
//     ApoProcessor processor_;  // frameSize_/sampleRate_ must match whatever
//                                // ValidateAndCacheConnectionInfo negotiates,
//                                // not necessarily the 480/48000 default.
// };

}  // namespace denoise::windows
