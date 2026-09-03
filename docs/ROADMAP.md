# Roadmap

The actionable, prioritized task list this project is against right now.
Every item here traces back to a gap identified in
[docs/KRISP_RESEARCH.md](KRISP_RESEARCH.md) (primary-sourced research on
what Krisp actually ships) or to a known hole in this repo's own current
state (per [docs/ARCHITECTURE.md](ARCHITECTURE.md),
[docs/MODELS.md](MODELS.md), [docs/PLATFORMS.md](PLATFORMS.md), and the
platform-specific READMEs) -- this file doesn't re-argue *why* each item
matters, it links to where that's already established.

Nothing here is started. Checking an item off means it's been actually
built and verified (per this project's own standard throughout its docs:
"built" means run, not just written -- see the status table in
[README.md](../README.md)), not just attempted.

## Load-bearing: nothing else matters until these exist

- [ ] **Get a real trained model into the `Denoise` stage.** The single
      largest gap (`KRISP_RESEARCH.md`'s gap table, row "Trained models").
      Right now the entire pipeline is provably-correct plumbing around
      zero actual noise suppression -- every test passes because the test
      fixtures are deterministic toy graphs (`models/test/`), not because
      anything removes noise. Needs a scope decision first: train from
      scratch (real data, real compute) vs. find and adapt an existing
      *openly licensed* model with a real ONNX export that fits (or can be
      wrapped to fit) the `docs/MODELS.md` tensor contract. Krisp's own
      weights are confirmed unobtainable -- see `KRISP_RESEARCH.md`'s
      `gst-krisp-audio` findings (licensed `.kef` files, network license
      check).
- [ ] **Get real trained VAD and SpeakerIsolation models.** Same gap, other
      two stage kinds. Krisp trains these as separate modules (NC vs.
      BVC/VIVA vs. Turn-Taking), not one generic model reused three ways --
      see `KRISP_RESEARCH.md`'s SDK module breakdown.

## Architecture gaps vs. Krisp's shipped surface

- [ ] **Add an inbound (speaker-side) processing path.** Krisp ships
      distinct outbound (NC-o, mic) and inbound (NC-i, received audio)
      processing; every current platform backend only touches mic-capture.
      Likely a second `DenoisePipeline` instance per backend, not a new
      core concept.
- [ ] **Redesign `SpeakerIsolation` beyond a single scalar gain.**
      `docs/MODELS.md` documents it as one gain value per frame; Krisp
      splits this concept into separate modules with (presumably) richer
      per-sample or per-band masks. Needs either a new output-tensor shape
      convention or a new `StageKind`.
- [ ] **Add de-reverberation** as a stage or a folded-in part of `Denoise`.
      Krisp bundles it into Noise Cancellation; we have no concept of it
      at all.
- [ ] **Design and implement echo cancellation (AEC).** Missing feature
      category entirely. Krisp's own material leaves the mechanism
      ambiguous (`KRISP_RESEARCH.md`'s "Open questions" -- possibly true
      far-end-reference AEC, possibly bundled de-reverb). True AEC needs a
      reference/loopback signal threaded through the pipeline, which is
      architecturally different from the current single-stream
      `Stage`/`DenoisePipeline` shape (one input frame -> one output
      frame) -- this needs a design decision before any implementation.
- [ ] **Add model-variant/tier support to `Stage`/`OnnxModel`.** Krisp
      ships explicit Small (7x faster) / Big tiers per module for a
      CPU-vs-quality tradeoff. *Blocked by* the two model items above --
      there's nothing to have variants of yet.

## The actual point of the project

- [ ] **Make at least one platform backend appear as a selectable
      microphone.** This is the product-defining feature of "a Krisp
      alternative" and is not achieved on any platform today:
      - Linux: it's a `media.category=Filter` PipeWire node, not
        `Audio/Source` (`platform/linux/README.md`). Needs either a
        two-node `libpipewire-module-filter-chain`-style split, or
        wrapping the existing filter's output with `pw-loopback`. Natural
        first target -- it already has a real, tested processing core
        (`denoise-pipewire-filter`).
      - macOS: the HAL plugin skeleton has no real capture source wired
        into `Driver::captureRing_` at all (`platform/macos/README.md`).
      - Windows: structurally impossible via an APO alone (APOs modify an
        *existing* device's stream, per Microsoft's own docs quoted in
        `platform/windows/README.md`) -- would need a kernel-mode WDM/KS
        driver, explicitly out of scope for the reasons given there.

## Cheaper wins (the data/infra already exists, just not exposed)

- [ ] **Surface `FrameContext`'s existing telemetry**
      (`vadProbability`, `speakerScore`) through the CLI and platform
      backends instead of discarding it after every frame. Krisp exposes
      this as a first-class per-frame + per-session stats API; we already
      compute the numbers internally and just don't report them.
- [ ] **Measure real end-to-end latency on the Linux backend** (the one
      platform with a real running integration -- e.g. an impulse/click
      test through `pw-link`) and document it. Krisp's SDK docs give a
      concrete example (~25ms for a 10ms/16kHz frame); we have never
      measured our own.

## Platform-specific unblocks

- [ ] **Wire a real capture source into the macOS driver**
      (`Driver::captureRing_` has no producer -- see
      `platform/macos/DeNoiseAudioDriver.h`). Needs a companion capture
      helper or an aggregate/multi-output device design. Still separately
      blocked on macOS toolchain access to build/test at all.
- [ ] **Verify `platform/windows/DeNoiseApo.h` against real Windows SDK
      headers.** It's an explicitly unverified sketch reconstructed from
      Microsoft's prose docs (class body deliberately commented out, not
      presented as compilable) -- needs actual SDK access
      (`Audioenginebaseapo.h`/`.idl`, `Baseaudioprocessingobject.h`),
      ideally cross-checked against Microsoft's real SYSVAD Swap APO
      sample.
- [ ] **Packaging, installer, and code signing for at least one
      platform.** Krisp ships a signed, installed consumer app; we have
      none of that on any platform. Linux first (real working filter
      already exists); macOS/Windows blocked on their respective driver
      work landing first.

## Explicitly not on this roadmap (scope calls, not oversights)

Per `KRISP_RESEARCH.md`'s "What we're not chasing": accent conversion,
voice translation, meeting transcription/summaries, CRM integrations,
agenda suggestions, and contact-center-specific accent conversion. These
are real things Krisp ships -- currently their largest product surface by
page count -- but they're a different product (an AI meeting assistant)
layered on top of an audio pipeline, not part of "a Krisp alternative
driver." Their absence isn't tracked as a gap here unless the project's
goal expands to include them.
