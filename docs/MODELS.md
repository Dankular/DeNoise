# Models

DeNoise ships **no trained models**. It ships a pipeline that runs ONNX
models matching a specific tensor contract, a generator for tiny
deterministic *test* fixtures that satisfy that contract (`models/test/`,
via `scripts/gen_test_models.py`), and this document describing the
contract so real trained models can be wrapped/converted to fit it.

Nothing below is a claim about how any specific third-party project
(RNNoise, DeepFilterNet, Silero VAD, WebRTC's noise suppressor, etc.)
actually exports to ONNX -- none of their exports have been inspected in
this project. Treat "how to bring your own model" as "here is the shape
your ONNX graph must have," not "here is what RNNoise's ONNX export looks
like."

## The contract

This is a convention **this project defines** for its own three stage
kinds (`core/include/denoise/onnx_model.h` and `pipeline.h` are the
normative source -- this doc explains it in prose).

Every model loaded into a `Stage` must have:

- an input tensor named exactly `input`, `float32`, shape `[frame_size]`
  (one frame of mono audio, samples in `[-1, 1]`, `frame_size` fixed at
  the pipeline's construction -- 480 samples/10ms at 48kHz by default in
  the CLI and platform backends, but the pipeline itself takes any
  `frame_size`)
- an output tensor named exactly `output`, `float32`, whose shape depends
  on the stage kind (below)

and may *additionally* have, for models with recurrent hidden state
carried between frames:

- an input tensor named `state_in`, `float32`, shape `[state_size]`
  (`state_size` is whatever the model declares -- read from the model
  itself, not fixed by this project)
- an output tensor named `state_out`, `float32`, same shape as `state_in`

`state_in`/`state_out` must be present together or not at all. When
present, `OnnxModel` owns the state buffer: it's zero-initialized on load
(and by `resetState()`), and each call's `state_out` is fed back in
verbatim as the next call's `state_in` -- the model is responsible for
whatever recurrence that implies (a GRU/LSTM hidden state, a running
buffer, etc.); this project's C++ code just plumbs the tensor through
unmodified. See `models/test/stateful_diff_denoise.onnx` /
`tests/test_pipeline_onnx.cpp`'s `testStatefulDiffDenoise` for a worked
example (`output = input - state_in`, `state_out = input`).

Any input/output tensor name other than these four causes `Stage::loadModel`
to fail (returns `false` with a reason in `errorOut`) rather than silently
ignoring an unexpected tensor -- see `core/inference/onnx_model.cpp`.

### Per-stage-kind output shape

| `StageKind` | `output` shape | What the pipeline does with it |
|---|---|---|
| `Vad` | `[1]` | Clamped to `[0, 1]`, stored as `FrameContext::vadProbability`. Does **not** modify the audio; see `DenoisePipeline::processFrame`'s `gateByVad` parameter if you want the pipeline to multiply the signal by it after all stages run. |
| `Denoise` | `[frame_size]` (must exactly match the input frame's length) | Replaces `FrameContext::samples` outright. |
| `SpeakerIsolation` | `[1]` | Clamped to `[0, 1]`, stored as `FrameContext::speakerScore` **and** multiplied elementwise into every sample of `FrameContext::samples` (a single scalar gain per frame, not a per-sample or per-band mask -- see "Known simplifications" below). |

A stage with no model loaded runs as a true passthrough (the frame is
unmodified, `vadProbability`/`speakerScore` stay at their defaults of
`1.0`) -- this is what lets the pipeline, CLI, and platform backends run
and be tested end-to-end before any real weights exist; see
`tests/test_pipeline_passthrough.cpp`.

### Known simplifications (v1, not fundamental limits of the contract)

- `SpeakerIsolation` applies a single scalar gain per frame. A real
  speaker-isolation model more naturally produces a per-sample or
  per-frequency-band mask; that would need either a new `StageKind` or an
  `output` shape convention richer than `[1]`. Not implemented.
- `Vad`'s effect on the actual audio is opt-in and crude (a flat
  post-pipeline gain multiply via `gateByVad`), not integrated into the
  `Denoise`/`SpeakerIsolation` stages' own processing.
- No stage currently supports multi-channel audio, only mono.

## Getting a real model into this contract

1. Train or obtain a model that operates on fixed-size audio frames (or
   wrap/re-export one that does).
2. Export it to ONNX with input/output tensors named and shaped exactly as
   above for the stage kind you're targeting. If it's recurrent, expose
   its hidden state as explicit `state_in`/`state_out` tensors (most
   training frameworks require you to unroll a single step and pass state
   in/out explicitly to get a valid streaming ONNX export in the first
   place).
3. Point a stage at it: `stage.loadModel("/path/to/model.onnx")` (C++), or
   `--vad-model`/`--denoise-model`/`--speaker-model` (CLI and the Linux
   filter).
4. `Stage::loadModel` validates the tensor names/shapes at load time and
   fails loudly (see above) rather than silently misbehaving on a mismatch
   -- if it rejects your model, the returned error string says exactly
   which tensor/shape didn't match.

## Test fixtures (`models/test/`)

Generated by `scripts/gen_test_models.py` (requires `pip install onnx
numpy`), regenerate with:

```sh
python3 scripts/gen_test_models.py --frame-size 480
```

These are deterministic toy graphs, not trained models -- their entire
purpose is that a C++ test can predict their output exactly:

- `identity_denoise.onnx` -- `Denoise` stage, `output = input`
- `gain_denoise.onnx` -- `Denoise` stage, `output = input * 0.5`
- `mean_abs_vad.onnx` -- `Vad` stage, `output = mean(abs(input))`
- `fixed_gain_speaker.onnx` -- `SpeakerIsolation` stage, `output = 0.25` (constant)
- `stateful_diff_denoise.onnx` -- `Denoise` stage with state: `output = input - state_in`, `state_out = input`

Regenerate and diff before committing a change to the generator, to catch
it drifting from the committed fixtures.
