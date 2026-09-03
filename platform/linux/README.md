# Linux: PipeWire virtual-mic filter

`denoise-pipewire-filter` is a `pw_filter`-based PipeWire client with one DSP
input port (`input`) and one DSP output port (`output`), both `32 bit float
mono audio`. It runs the core `DenoisePipeline` on whatever audio arrives at
`input` and republishes the result on `output`.

## What is verified

Built and run against a real PipeWire 1.0.5 + WirePlumber 0.4.17 instance
(no ALSA hardware, `Dummy-Driver`/`Freewheel-Driver` clock):

- The binary registers a node named `denoise-filter` with exactly the two
  DSP ports described above (`pw-dump`, `pw-link -o`/`-i` confirmed).
- Feeding it real audio via `pw-link` + `pw-cat --playback` and capturing its
  output via `pw-link` + `pw-cat --record` produces audio, and with
  `--denoise-model models/test/gain_denoise.onnx` loaded, the recorded
  output's peak amplitude is exactly half the input's -- i.e. the ONNX
  Runtime inference genuinely runs on live audio flowing through PipeWire's
  real-time graph, not just in the offline CLI path.
- `on_process` runs on PipeWire's mainloop thread, not the RT graph thread
  (see the caveat in `denoise_pipewire_filter.cpp` -- `PW_FILTER_FLAG_RT_PROCESS`
  is deliberately not set because ONNX Runtime's `Session::Run()` allocates
  and is not realtime-safe).

## What is NOT verified

- This container has no real microphone/speaker hardware and no desktop
  session, so **whether this node appears as a selectable "microphone" in
  an application's device picker (Zoom, Discord, a browser, `wpctl`'s
  default-source list, etc.) has not been tested.** A `pw_filter` node is a
  `media.category=Filter` node (it has both an input and an output port on
  one node); device pickers generally enumerate `media.class=Audio/Source`
  nodes. Real-world "virtual mic with ML processing" setups (e.g. the
  RNNoise-via-PipeWire community configs this design is informed by)
  achieve the "shows up as a mic" result with **two** linked nodes -- a
  passive capture side and an `Audio/Source`-classed playback side -- via
  `libpipewire-module-filter-chain`, not a single `pw_filter`. Making
  `denoise-pipewire-filter` itself present as `Audio/Source` (e.g. by
  splitting it into two `pw_stream`s the way `module-filter-chain` does, or
  by wrapping its output with `pw-loopback`) is unimplemented follow-up
  work, not something this file currently claims to do.
- No latency numbers are measured. The startup delay visible in the `t=0.00s
  peak=0` .. `t=0.40s peak=0` window during testing was our own test-script
  linking delay (`sleep 0.5`), not a measurement of the filter's inherent
  latency.

## Building

Requires `libpipewire-0.3-dev` (Ubuntu/Debian: `apt install
libpipewire-0.3-dev`). CMake auto-detects it via `pkg-config` and only adds
this target if found.

## Running and wiring it up manually (the verified path)

```sh
# 1. Start the filter (loads whichever stage models you pass; with none,
#    every stage runs in passthrough).
./denoise-pipewire-filter --denoise-model /path/to/denoise.onnx

# 2. In another terminal, see its ports:
pw-link -o | grep denoise-filter   # denoise-filter:output
pw-link -i | grep denoise-filter   # denoise-filter:input

# 3. Link a real capture device's output port to denoise-filter:input, and
#    link denoise-filter:output to whatever should consume the processed
#    audio, e.g.:
pw-link alsa_input.<your-device>:capture_MONO denoise-filter:input
pw-link denoise-filter:output <some-app>:input_MONO

# Or use a GUI patchbay (qpwgraph, Helvum) to draw the same links.
```

For a from-a-file smoke test without any real audio hardware, see how the
project's own tests exercise this with `pw-cat --record`/`--playback` and
`pw-link` (target `0` to prevent PipeWire's own auto-routing, then link the
exact ports by name) -- that is the exact procedure used to produce the
"verified" result above.
