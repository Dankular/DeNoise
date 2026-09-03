# Architecture

## Layering

```
                    ┌─────────────────────────────────────────┐
                    │              core/ (portable)             │
                    │                                           │
  raw samples  ──►  │  Framer  ──►  DenoisePipeline  ──►  frame │  ──► processed samples
                    │  (fixed-size    (ordered Stages,    out   │
                    │   framing)      each an OnnxModel)         │
                    └─────────────────────────────────────────┘
                             ▲                    │
                             │ RingBuffer (SPSC)   │ RingBuffer (SPSC)
                             │                     ▼
                    ┌─────────────────────────────────────────┐
                    │        per-OS realtime audio callback     │
                    │  (PipeWire on_process / CoreAudio          │
                    │   DoIOOperation / Windows APOProcess)      │
                    └─────────────────────────────────────────┘
```

- **`core/audio`**: `RingBuffer` (lock-free SPSC), `Framer` (slices a
  stream into fixed-size, fixed-hop frames), `OverlapAdder` (reassembles
  overlapping frames back into a stream), `LinearResampler` (documented as
  a deliberately low-quality placeholder -- see its header).
- **`core/inference`**: `OnnxModel`, a thin wrapper around ONNX Runtime's
  C++ API implementing the streaming tensor contract in
  [docs/MODELS.md](MODELS.md) -- including owning and feeding back
  recurrent `state_in`/`state_out` tensors.
- **`core/pipeline`**: `Stage` (one `OnnxModel` plus the `StageKind`-specific
  interpretation of its output -- see MODELS.md) and `DenoisePipeline` (an
  ordered list of `Stage`s run over a `FrameContext` each frame).
- **`tools/denoise_cli`**: the only piece that touches an actual file
  format (`wav_io.{h,cpp}` -- a from-scratch RIFF/WAVE reader/writer, no
  external dependency); everything else in `core/` only ever sees
  `std::vector<float>`.
- **`platform/*`**: OS integration. Each one's realtime callback does the
  same three things: push incoming samples into a `Framer`, drain whatever
  full frames are available through the `DenoisePipeline`, push the result
  into an output `RingBuffer` for the callback to drain from (padding with
  silence on underrun rather than blocking or leaving stale data). See
  "Realtime safety" below for why inference itself is never called
  directly from that callback.

## Realtime safety: the same pattern on all three platforms

`OnnxModel::run` calls into ONNX Runtime's `Session::Run()`, which
allocates memory and takes an unbounded amount of time -- neither property
is acceptable on a hard-realtime audio callback thread (a missed deadline
means an audible glitch or an xrun). All three platform backends handle
this the same way, and it's the central design constraint of this project,
not an afterthought:

- **`platform/linux`**: deliberately does *not* pass `PW_FILTER_FLAG_RT_PROCESS`
  to `pw_filter_connect`, so PipeWire calls its `process` callback from the
  mainloop thread rather than the realtime graph thread. Documented as a
  correctness-over-latency tradeoff in `denoise_pipewire_filter.cpp`.
- **`platform/macos`**: `DoIOOperation` genuinely does run on a realtime
  deadline thread (that's how CoreAudio HAL plugins work; there's no
  "run on the mainloop instead" option). So `Driver` runs the pipeline on
  a dedicated `std::thread`, and `DoIOOperation`/`readProcessedAudio` only
  ever touch lock-free `RingBuffer`s.
- **`platform/windows`**: Microsoft's own APO documentation states
  real-time APO methods "must not block, use paged memory, or call any
  blocking system routines" -- the same constraint as macOS, and for the
  same reason (APOProcess also runs on a deadline thread). `ApoProcessor`
  uses the identical worker-thread-plus-ring-buffer shape as the macOS
  driver, and -- unlike the rest of `platform/windows` -- is portable
  C++ with no Windows dependency, so it's the one part of that backend
  built and tested in this project's own CI
  (`tests/test_apo_processor.cpp`).

## How each piece was actually verified

Not inferred from reading the code -- each of these was run:

- **`core/` + `tools/denoise_cli`**: `ctest` (5 of the 6 suite's tests,
  `test_apo_processor` being the 6th) runs real ONNX Runtime inference,
  including a stateful recurrent model, and checks exact expected outputs.
  The CLI was separately run against a real WAV file with a real gain
  model, and the output's peak amplitude was independently measured
  (Python's `wave`/`struct`) to be exactly half the input's.
- **`platform/linux`**: built against and run against a real PipeWire
  1.0.5 + WirePlumber 0.4.17 instance (no ALSA hardware --
  `Dummy-Driver`/`Freewheel-Driver` clock). The filter's node and two DSP
  ports were confirmed via `pw-dump`/`pw-link`. Audio was played into the
  input port and recorded from the output port using `pw-cat` (linked
  explicitly via `pw-link`, since `--target <name>` auto-routing didn't
  resolve to a `Filter`-category node -- see `platform/linux/README.md`),
  and the recording's peak amplitude was confirmed to be exactly half the
  source's, live, through the real graph -- not just in the offline CLI
  path.
- **`platform/windows`**'s portable half: unit-tested exactly like `core/`
  (see above).
- **`platform/macos`** and **`platform/windows`**'s COM shim: **not** run.
  No macOS or Windows toolchain was available in the environment this
  project was developed in. See each directory's README for exactly what
  is and isn't grounded in a real header vs. reconstructed from
  documentation.

## Why the pipeline frame size is fixed at construction

`DenoisePipeline`'s constructor takes a `frameSize` and every `Stage`
added to it validates that any model it loads declares an `input` tensor
of exactly that size (`Stage::loadModel`). This is a deliberate rigidity:
it means a shape mismatch between the pipeline and a model is caught at
load time with a clear error, not as a runtime crash or (worse) a silent
reinterpretation of the wrong number of samples, deep inside a realtime
callback.
