# Windows: Audio Processing Object (APO) skeleton

**Status: two layers, verified very differently.**

- `denoise_apo_processor.{h,cpp}` (the async worker-thread + ring-buffer
  logic that keeps ONNX Runtime off the realtime callback path) is **plain,
  portable C++ with no Windows dependency. It is built and unit-tested in
  this project's own CI** (`tests/test_apo_processor.cpp`) -- it pushes 20
  distinct constant-valued frames through `ApoProcessor` with a loaded gain
  model and confirms the async pipeline reproduces them, in order, gain-
  applied, exactly.
- `DeNoiseApo.h` (the actual COM/`IAudioProcessingObjectRT` shim that would
  make this loadable by Windows' audio engine) is **an unverified sketch**.
  This project was developed in a Linux container with no Windows SDK or
  MSVC, so unlike the Linux and macOS backends, the exact COM method
  signatures here were never checked against a real header -- see the
  disclaimer at the top of `DeNoiseApo.h` for specifics and for why its
  class body is commented out rather than presented as compilable code.

## What an APO actually is (and isn't)

Per Microsoft's own documentation
([Implementing Audio Processing Objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects)),
a custom APO is an in-process COM object (packaged as a DLL) that Windows'
audio engine inserts into an **existing logical device's** signal
processing graph as a Stream (SFX), Mode (MFX), or Endpoint (EFX) effect.
It "can modify only the audio data that is passed to it" and "cannot
change the settings of the underlying logical device."

That means an APO is the Windows analogue of applying DeNoise's pipeline
*to a device you already have* (the same shape as Windows' own "Voice
Focus"/Studio Effects noise suppression) -- **not** a new, independently
selectable "DeNoise Microphone" device the way `platform/linux`'s
`pw_filter` is. Getting an actual new virtual audio device on Windows (what
most people mean when they compare something to Krisp/VB-Cable) needs a
kernel-mode WDM/KS audio driver, a materially bigger undertaking (Windows
Driver Kit, INF-based install, driver signing) with its own real prior art
(e.g. open-source virtual-cable drivers). This project does not attempt to
sketch kernel driver code: unlike a userspace COM/ABI mismatch, a wrong
guess in kernel-mode driver structures can crash the whole system, and none
of it could be checked against real WDK headers in this environment either.
If a true virtual device is the goal, start from Microsoft's own
[SYSVAD sample](https://github.com/Microsoft/Windows-driver-samples/tree/main/audio/sysvad)
(a real, Microsoft-maintained, buildable reference for both APOs and a full
virtual audio adapter) rather than this skeleton.

## Packaging (documented, not exercised)

Per the same Microsoft doc: an APO DLL is registered via an INF file
(`HKR,AudioEngine\AudioProcessingObjects\<CLSID>,...` plus
`PKEY_FX_StreamEffectClsid`/`PKEY_SFX_ProcessingModes_Supported_For_Streaming`
entries associating it with a device), and Windows enforces real-time
behavior partly by tracking `HRESULT` failures from `CoCreateInstance`,
`IsInputFormatSupported`, `IsOutputFormatSupported`, and `LockForProcess` --
too many failures and the system disables system effects for that endpoint.
None of this INF/registration machinery exists in this directory yet.

## Building

Not wired into the top-level CMake build (there is no
`DENOISE_BUILD_WINDOWS_APO` option) because there is nothing here that
would actually link into a loadable APO DLL yet -- `DeNoiseApo.cpp` doesn't
exist; only the commented-out sketch in `DeNoiseApo.h` does.
`denoise_apo_processor.cpp` itself builds as part of the normal (any-OS)
CMake build via `platform/windows/CMakeLists.txt`, and that's what
`tests/test_apo_processor.cpp` exercises.
