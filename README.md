# DeNoise

An open-source, cross-platform real-time voice-processing pipeline (noise
suppression, voice activity detection, speaker isolation) built as a
composable stack of small ONNX models behind a shared C++ core -- aiming at
what Krisp does, but inspectable and self-hostable. It ships as:

- a portable C++ core (`core/`) that chains ONNX Runtime models over
  fixed-size audio frames,
- an offline CLI (`tools/denoise_cli`) for testing the pipeline against WAV
  files with no audio hardware needed,
- and per-OS integrations that get that pipeline in front of real audio:
  a **built-and-tested** PipeWire virtual-mic filter on Linux, and
  **unverified skeletons** for macOS (CoreAudio HAL plugin) and Windows
  (Audio Processing Object).

## Status: what's real, what's a sketch

This project ships no trained models (see [docs/MODELS.md](docs/MODELS.md))
and its three platform backends are verified to very different degrees.
Read this table before trusting any part of it:

| Component | Status |
|---|---|
| `core/` (ring buffer, framer, resampler, ONNX Runtime wrapper, pipeline) | Built, unit-tested against real ONNX Runtime inference (5 tests, `ctest`) |
| `tools/denoise_cli` | Built, verified end-to-end on a real WAV file (exact expected gain applied) |
| `platform/linux` (PipeWire filter) | Built and run against a **real PipeWire 1.0.5 + WirePlumber instance**; live audio round-tripped through it via `pw-link`/`pw-cat` with the expected transform applied. Whether it shows up as a selectable "microphone" in an app picker is **not** tested -- see its README. |
| `platform/windows` processing logic (`denoise_apo_processor.*`) | Built and unit-tested (no Windows dependency) |
| `platform/windows` COM/APO shim (`DeNoiseApo.h`) | **Unverified sketch** -- no Windows SDK was available to check it against; see its README |
| `platform/macos` (CoreAudio HAL plugin) | **Unverified skeleton** -- written against a real fetched Apple header but never compiled or loaded; see its README |
| Trained VAD/denoise/speaker-isolation models | **None included.** Only tiny deterministic ONNX test fixtures (`models/test/`) used to verify the pipeline plumbing -- see docs/MODELS.md for the model contract and how to bring your own |

Every "built/tested" claim above was produced by actually running the
build and the tests, not inferred from the code -- see `docs/ARCHITECTURE.md`
for how each piece was verified.

## Quick start (Linux)

```sh
# Dependencies
sudo apt install build-essential cmake pkg-config libpipewire-0.3-dev

# ONNX Runtime (prebuilt release, not built from source)
mkdir -p third_party && cd third_party
curl -sSL -o onnxruntime.tgz \
  https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-linux-x64-1.19.2.tgz
tar xf onnxruntime.tgz && mv onnxruntime-linux-x64-1.19.2 onnxruntime-linux-x64
cd ..

cmake -B build -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-linux-x64"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# Try the pipeline on a WAV file (passthrough with no models loaded):
./build/tools/denoise_cli/denoise-cli --in in.wav --out out.wav

# Run the live PipeWire virtual-mic filter:
./build/platform/linux/denoise-pipewire-filter --denoise-model models/test/gain_denoise.onnx
# see platform/linux/README.md for how to wire its ports up with pw-link
```

## Layout

```
core/                  portable C++ pipeline: audio/ (ring buffer, framer,
                        resampler), inference/ (ONNX Runtime wrapper),
                        pipeline/ (stage orchestration)
tools/denoise_cli/      offline WAV-in -> pipeline -> WAV-out tool
platform/linux/         PipeWire virtual-mic filter (built + tested here)
platform/macos/         CoreAudio HAL plugin (unverified skeleton)
platform/windows/       Audio Processing Object (portable logic tested;
                        COM shim is an unverified sketch)
models/test/            tiny deterministic ONNX fixtures used by the tests
                        (NOT trained models -- see docs/MODELS.md)
scripts/gen_test_models.py  regenerates models/test/*.onnx
tests/                  unit + integration tests (ctest)
docs/                   ARCHITECTURE.md, MODELS.md, PLATFORMS.md
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- how the pieces fit together and how each was verified
- [docs/MODELS.md](docs/MODELS.md) -- the ONNX tensor contract each pipeline stage expects, and how to bring your own trained models
- [docs/PLATFORMS.md](docs/PLATFORMS.md) -- per-OS integration approach and status, expanding on the table above
- [docs/KRISP_RESEARCH.md](docs/KRISP_RESEARCH.md) -- primary-sourced research on what Krisp actually ships, and a gap analysis against this repo
- [docs/ROADMAP.md](docs/ROADMAP.md) -- the prioritized, actionable task list that gap analysis turned into
- [CONTRIBUTING.md](CONTRIBUTING.md)

## License

Apache 2.0 -- see [LICENSE](LICENSE).
