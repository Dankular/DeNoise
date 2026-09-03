# macOS: CoreAudio AudioServerPlugIn skeleton

**Status: written, never built, never loaded by coreaudiod.** This project
was developed in a Linux container with no macOS SDK or Xcode toolchain
available, so none of this directory has been compiled, let alone tested
against a real macOS audio stack. Everything below is a starting point, not
a working driver.

## What grounds this skeleton

`DeNoiseAudioDriver.h`/`.cpp` implement the `AudioServerPlugInDriverInterface`
vtable copied from Apple's real `CoreAudio/AudioServerPlugIn.h` (fetched from
[phracker/MacOSX-SDKs](https://github.com/phracker/MacOSX-SDKs), a mirror of
Apple's SDK headers, while writing this -- not reconstructed from memory).
The method signatures, the `AudioServerPlugInDriverRef` COM-style
vtable-pointer convention, `kAudioServerPlugInTypeUUID`'s actual UUID bytes,
and the bundle-packaging notes in `Info.plist.in` all come from that header.

## What is deliberately unimplemented (see code comments for the full list)

- **Only a curated subset of HAL properties** is handled (enough to
  describe one device + one input stream's basic identity/format) --
  no controls (volume/mute), no available-format enumeration beyond a
  single hardcoded 48kHz mono float32 format, no persistent settings.
- **No real audio actually reaches the driver.** `Driver::captureRing_` has
  no producer wired up. Making a CoreAudio virtual *microphone* actually
  receive another device's or app's audio is a fundamentally different
  problem from PipeWire's `pw-link`-based routing (see
  `platform/linux/README.md` for that contrast) and needs its own design
  -- e.g. a companion capture helper that opens the real mic via the
  client-side HAL API and writes into this driver via IPC/shared memory,
  or combining this device into a multi-output/aggregate device. Not
  designed here.
- **No packaging has been exercised**: no Xcode project, no code signing,
  no notarization, and `Info.plist.in` is a best-effort, *unverified*
  reading of the header's own packaging notes -- cross-check it against
  [gavv/libASPL](https://github.com/gavv/libASPL), a real, actively
  maintained C++17 library for building CoreAudio AudioServerPlugIns,
  before trusting it.

## Realtime-safety note

`DoIOOperation` runs on a realtime deadline thread inside the plug-in's
host process (coreaudiod), and per `AudioServerPlugIn.h` itself, "the
plug-in must avoid blocking and return as quickly as possible." ONNX
Runtime's `Session::Run()` allocates and is not realtime-safe, so (as with
`platform/linux`) actual pipeline inference happens on a separate
`std::thread` (`Driver::workerLoop`), communicating with the IO thread only
through the lock-free `RingBuffer`s from `core/`. `DoIOOperation` itself
never calls into the pipeline directly.

## Building (once there's a macOS environment to try it on)

```sh
cmake -B build -DDENOISE_BUILD_MACOS_DRIVER=ON
cmake --build build --target DeNoiseAudioDriver
# then, if it actually loads:
sudo cp -r build/platform/macos/DeNoiseAudioDriver.driver /Library/Audio/Plug-Ins/HAL/
sudo killall coreaudiod   # reloads HAL plugins
```

This has not been run. Treat the above as a guess at the right commands,
not a verified procedure.
