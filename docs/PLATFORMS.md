# Platform integrations

Each OS needs a fundamentally different mechanism to get audio through
`core/`'s `DenoisePipeline` in real time. This is a summary and comparison;
each directory's own README has the full detail, exact verification steps,
and unimplemented-work lists -- read those before building against any of
them.

| | [platform/linux](../platform/linux/README.md) | [platform/macos](../platform/macos/README.md) | [platform/windows](../platform/windows/README.md) |
|---|---|---|---|
| Mechanism | `pw_filter` (PipeWire) | CoreAudio `AudioServerPlugIn` (HAL plugin) | Audio Processing Object (`IAudioProcessingObjectRT`) |
| Creates a new selectable device? | Not by itself (see caveat below) | Yes, in principle (a HAL plugin device) | **No** -- modifies an *existing* device's stream (SFX/MFX/EFX) |
| Grounded in | Real headers, built against a running PipeWire daemon | A real Apple SDK header (fetched, not from memory), never compiled | Microsoft's prose docs (no SDK available) -- explicitly a sketch, not compilable as-is |
| Realtime-unsafe inference kept off the RT thread by | Not using `PW_FILTER_FLAG_RT_PROCESS` (runs on the mainloop instead) | A dedicated worker thread + lock-free ring buffers (`DoIOOperation` has no alternative to running on the RT thread) | Same worker-thread pattern as macOS (`APOProcess` also has no alternative) |
| Verified how | Live: real audio round-tripped through a running PipeWire graph via `pw-link`/`pw-cat`, exact expected gain measured | Not run at all | Portable logic layer unit-tested (no OS dependency); the actual COM shim not run at all |

## The "new virtual device" question, concretely

If the goal is "an app like Zoom/Discord lets you pick a `DeNoise
Microphone` from its input dropdown," here's what that actually takes on
each OS, plainly:

- **Linux**: this project's `pw_filter` node is a `media.category=Filter`
  node (it has both an input and output port on one node) -- device
  pickers generally enumerate `media.class=Audio/Source` nodes instead.
  Real "virtual mic with DSP" setups on PipeWire (e.g. the RNNoise-via-
  PipeWire community configs referenced in `platform/linux/README.md`)
  get there with **two** linked nodes via `libpipewire-module-filter-chain`,
  not a single filter. Making this project's own filter present as
  `Audio/Source` (splitting it into two `pw_stream`s, or wrapping it with
  `pw-loopback`) is unimplemented.
- **macOS**: a HAL plugin *can* publish a genuinely new device (that's
  what `platform/macos/DeNoiseAudioDriver.cpp` sketches), but this
  project's skeleton has no producer feeding real microphone audio into
  it yet -- see its README's "What is deliberately unimplemented" section.
- **Windows**: an APO cannot do this at all -- per Microsoft's own
  documentation it only modifies an existing logical device's stream. A
  genuinely new virtual device needs a kernel-mode WDM/KS driver, which
  this project does not attempt (see `platform/windows/README.md` for
  why: unverifiable kernel-mode structure is a materially worse thing to
  get wrong than a userspace ABI mismatch, and no WDK was available to
  check against anyway).

## Common design across all three

Despite the very different platform mechanisms, all three backends share
the same internal shape: a fixed-size `Framer` feeding the same
`DenoisePipeline`, output smoothed through a `RingBuffer`, and (per the
realtime-safety constraint in [ARCHITECTURE.md](ARCHITECTURE.md)) actual
ONNX inference kept off whatever thread the OS calls the realtime audio
callback on. Only the "how does audio physically get in and out" part
differs.
