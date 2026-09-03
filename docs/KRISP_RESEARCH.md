# Krisp research and gap analysis

Krisp is closed-source and its model weights, training data, and internal
architecture are not published. Nothing in this document claims to know
how Krisp's models work internally. Everything below is either:

- a claim **from a primary Krisp source** (their own product pages, help
  docs, SDK docs, SDK changelog, or a public GitHub repo of theirs), cited
  inline, or
- a claim **from third-party review sites**, explicitly marked as such and
  treated as lower-confidence marketing-adjacent copy, not verified fact.

Where Krisp's own material is itself vague or silent on a mechanism (e.g.
whether "echo cancellation" is true reference-signal AEC), that is stated
as an open question, not resolved by guessing.

## What Krisp actually ships (primary sources)

### Product-level features
Source: [krisp.ai](https://krisp.ai/) (fetched directly)

- Noise cancellation: "removes noise, echo, and cross-talk"
- Meeting transcription (16 languages; 15 non-English languages are
  server-side, English is on-device), recording, AI note-taking/summaries,
  action-item tracking, laughter detection
- Accent conversion (both speaker-side and listener-side), real-time voice
  translation, voice isolation of the primary speaker
- AI-suggested agendas, CRM sync (Salesforce/HubSpot/Slack), integrates
  with 800+ communication apps via virtual mic/speaker device selection
- No latency, CPU usage, or model-architecture detail is given on this
  consumer-facing page at all -- that only shows up in the SDK docs below.

### SDK module breakdown
Source: [sdk-docs.krisp.ai](https://sdk-docs.krisp.ai/) (fetched directly)

Krisp's actual product is decomposed into separately-branded SDK modules,
which is a far more precise picture than the marketing page:

| Module | What it does (Krisp's own description) |
|---|---|
| Noise Cancellation (NC-o / NC-i) | Separate outbound (mic) and inbound (speaker) processing, **with de-reverberation included** |
| Background Voice Cancellation (BVC-o) | Outbound-only: removes *other people's voices* near the primary speaker |
| Voice Isolation for Voice AI (VIVA) | Removes background voices/noise (TV, music, kids, cars) -- framed for voice-AI-agent use cases specifically, not human meetings |
| Turn-Taking for Voice AI (VIVA) | Predicts when a speaker is about to finish talking, for voice AI agents to respond naturally -- **not just speech/silence VAD, but turn-*end* prediction** |
| Accent Conversion (AC-o) | Real-time contact-center-agent accent conversion (India→US, Philippines→US, LatAM→US are the specific pairs named) |

This tells us two things our marketing-page research alone wouldn't: (1)
Krisp treats "noise cancellation" and "remove other people's voices" as
**two distinct models/modules** (NC vs. BVC/VIVA), not one generic
denoiser; and (2) Krisp is explicitly building for the *voice AI agent*
market (turn-taking, agent frameworks) as a separate product line from the
*human meeting* market, with LiveKit/WebRTC/Pipecat framework integrations
to prove it.

### Noise Cancellation module technical detail
Source: [sdk-docs.krisp.ai/docs/noisecancellation](https://sdk-docs.krisp.ai/docs/noisecancellation) (fetched directly)

- **Two model size tiers**: a default "Small" model ("designed to
  integrate into lower-end devices and run 7x faster than the Big
  models"), and an on-demand "Big" model for higher quality at higher
  compute cost. **We ship zero model tiers -- we ship no trained model at
  all.**
- **Frame-in/frame-out contract**: "takes as input the audio frame and
  returns a noiseless frame of the same size" -- this is architecturally
  identical to our own `Denoise` stage contract (`docs/MODELS.md`).
- **Sample rates**: natively handles 8/16/32kHz, resamples other rates
  down and back up. We handle arbitrary rates via `LinearResampler`, but
  ours is explicitly documented as a low-quality placeholder, not a
  matched pair of resample filters.
- **Algorithmic latency example given**: ~25ms for a 10ms frame at 16kHz.
  We have never measured end-to-end latency for any of our three platform
  backends -- see "Gaps" below.
- **Outbound (mic) is explicitly scoped to near-field**: "under 50cm"
  mouth-to-mic distance. Inbound (speaker) explicitly handles "network
  codec degradation, multiple overlapping speakers, low-bandwidth
  landline audio" -- i.e. inbound and outbound are *different problems*
  with *different models*, not the same denoiser run twice.

### A real integration, in the open: `gst-krisp-audio`
Source: [github.com/krispai/gst-krisp-audio README](https://raw.githubusercontent.com/krispai/gst-krisp-audio/main/README.md) (fetched directly -- this is Krisp's own GStreamer plugin wrapping their closed SDK, not a third party)

This is the single most concrete technical artifact found, because it's
real integration code (even though the SDK it wraps is closed):

- Model weights ship as **`.kef` files** -- a proprietary, licensed format.
  Loading them requires **licensing credentials validated over the
  network** (asynchronously, on an SDK-internal thread; failures "post
  warnings rather than halting processing" -- i.e. it degrades to
  passthrough rather than hard-failing, the same passthrough-on-missing-
  model philosophy our own `Stage::loadModel` follows, for a different
  reason: we have no model to license, they have a license check).
  **This confirms Krisp's models are not obtainable at all outside their
  license -- there is no "borrow their weights" path for an open-source
  alternative.**
  Also: bundled dependencies include **libcurl and OpenSSL** in the SDK
  package itself, consistent with that network license check.
- Platform support confirmed concretely: **macOS (arm64, x86_64), Linux
  (x86_64, arm64), Windows (x86_64, MSVC)**.
- Audio contract: mono, S16LE or F32LE, 8kHz-96kHz, tunable frame duration
  **10-32ms** and a 0-100% "noise suppression level" knob.
- Implementation detail: a **"carry-buffer FIFO" to handle arbitrary
  upstream buffer sizes while meeting the SDK's fixed-frame requirement**.
  This is architecturally the same problem our `Framer` solves, and the
  same solution shape (accumulate arbitrary-size pushes, emit fixed-size
  frames) -- useful independent validation that this project's core
  design pattern matches a real production integration, not just our own
  reasoning about it.

### SDK developer platform matrix
Source: [krisp.ai/developers/](https://krisp.ai/developers/) (fetched directly)

- Platforms: **Windows, macOS, Linux, iOS, Android, and Web (JS/WASM)**.
- Language bindings: **C++, Node.js, Python, Go, Rust**.
- Framework integrations: **LiveKit, WebRTC, Pipecat**.
- Access is gated behind a contact-form request + a developer dashboard
  (`developers.krisp.ai`) -- not an open `pip install`/public download.
- VIVA is marketed as "language agnostic... no per-language tuning or
  configuration required."

### Telemetry / call-quality stats
Source: [SDK v6.0 changelog](https://sdk-docs.krisp.ai/changelog/%EF%B8%8F-sdk-v60-desktop-cpu-optimization-noise-statistics) (fetched directly)

The SDK exposes **per-frame voice/noise-removed levels (0-100)** and
**end-of-stream stats** (a 4-bucket noise classification -- none/low/
medium/high -- plus accumulated talk time). This is a real product
feature category: **quality telemetry as a first-class API**, not just
"process audio in, audio out." We expose nothing like this.

## Open questions (Krisp's own material doesn't resolve these)

- **Is "echo cancellation" true reference-signal AEC, or single-channel
  de-reverberation?** Krisp's own [blog post on
  AEC](https://krisp.ai/blog/acoustic-echo-cancellation/) describes the
  generic textbook concept of adaptive-filter AEC, then separately
  mentions "Room Echo Cancellation built right into our bi-directional
  Noise Cancellation" -- these read as two different things (AEC vs.
  de-reverb) bundled under one marketing term, but the article never says
  outright whether Krisp's shipped "echo cancellation" takes a far-end
  reference signal. **We did not resolve this and are not claiming an
  answer either way.**
- No public source found describing model architecture (CNN/RNN/
  transformer/etc.), training data, or parameter counts for any Krisp
  model. Third-party review sites' claim of "recognizes 20,000 distinct
  noise types" and "<10ms"/"<20ms" latency figures were **not found on
  any krisp.ai or sdk-docs.krisp.ai page we fetched** -- they only appear
  on third-party review/SEO sites, so they're recorded here only as
  unverified marketing-adjacent claims, not primary-source facts.

## Gap analysis: Krisp's shipped surface vs. this repo, today

Grounded in `docs/ARCHITECTURE.md`, `docs/MODELS.md`, and
`docs/PLATFORMS.md`'s status tables, which are themselves grounded in
what was actually built and tested in this repo (see those docs for what
"built/tested" means concretely).

| Area | Krisp ships (per above) | This repo | Gap |
|---|---|---|---|
| Trained models | Two size tiers per module, licensed `.kef` weights | **None.** Only deterministic toy ONNX fixtures for testing plumbing | **The single largest gap.** Nothing here removes real noise yet -- see "What to do about it" |
| Noise cancellation direction | Separate NC-o (mic) and NC-i (speaker) models | Only mic-side (`Denoise` stage runs on the local pipeline's input frame; nothing processes received/speaker audio) | No inbound/downlink processing at all |
| Voice isolation vs. noise cancellation | Two distinct modules (NC vs. BVC/VIVA) with presumably different training | One `SpeakerIsolation` `StageKind`, and per `docs/MODELS.md` it applies a single scalar gain per frame, not a mask | Both a modeling gap (no model) and a design gap (scalar gain is a cruder primitive than Krisp's apparent per-module separation) |
| De-reverberation | Bundled into NC | No stage or concept for it at all | Missing feature category |
| Echo cancellation | Shipped (mechanism unclear, see above) | Not implemented at all | Missing feature category |
| Voice activity detection | A whole "Turn-Taking for Voice AI" module beyond plain VAD (predicts turn *end*, not just speech presence) | `Vad` stage outputs a single frame-level probability, no turn-taking concept | Ours is a strict subset even of what a baseline VAD would need for the voice-AI-agent use case |
| Accent conversion / translation | Two shipped products (AC-o, plus a separate voice-translation product) | Not attempted | Out of this project's current scope entirely (see "What we're not chasing") |
| Model size tiers / CPU-quality tradeoff | Explicit Small (7x faster) / Big choice, plus a v6.0 "CPU optimization" pass | No model, so no tiers; `Stage`/`OnnxModel` has no concept of swappable model variants per stage | Would need `Stage` to support choosing a model variant at load time, not just a single path |
| Telemetry / quality stats API | Per-frame voice/noise levels + end-of-session summary | `FrameContext` carries `vadProbability`/`speakerScore` internally but nothing surfaces it to a consumer (CLI, platform backends) | Missing API surface, not a hard technical gap -- see "What to do about it" |
| Platform coverage | Windows, macOS, Linux, iOS, Android, Web/WASM -- all *actually shipped and licensable* | Linux: built and live-tested. macOS: unverified skeleton. Windows: portable logic tested, COM shim unverified sketch. **No mobile, no web/WASM at all** | Two platforms unverified, two platforms (mobile, web) not started |
| "Shows up as a selectable mic" | Confirmed working (their whole distribution model depends on it -- "800+ apps" via virtual device selection) | Explicitly **not** achieved on Linux (`platform/linux/README.md`: it's a `Filter`-category PipeWire node, not `Audio/Source`); macOS skeleton has no real capture source wired in; Windows APOs structurally cannot do this at all (see `docs/PLATFORMS.md`) | This is the actual product-defining feature of a "Krisp alternative" and we do not have it on any platform yet |
| Distribution/installer/signing | A shipped, installed, code-signed consumer app | None -- no installer, no code signing, no packaging beyond raw CMake build output | Not started |
| Language bindings for the core | C++, Node.js, Python, Go, Rust | C++ only | If the goal is also "an SDK others build on," binding surface is a gap; if the goal is just the end-user driver, less relevant |
| Latency measurement | A specific number given for their algorithmic latency | Never measured for any of our three backends | We don't actually know our own latency |

## What we're *not* chasing (scope calls, not gaps)

Being honest about scope matters as much as being honest about gaps:

- **Accent conversion, voice translation, meeting transcription/summaries,
  CRM integrations, agenda suggestions.** These are Krisp's actual biggest
  current product surface by page count on krisp.ai, but they're a
  different product (an AI meeting assistant) layered on top of the audio
  pipeline, not part of "a Krisp alternative driver." Nothing above treats
  their absence as a defect in this repo unless the project's goal
  expands to include them.
- **Contact-center-specific accent conversion (AC-o).** A narrow B2B
  product; not core to a general noise-suppression driver.

## What to do about it

Turned into an actual tracked, prioritized task list in
[docs/ROADMAP.md](ROADMAP.md) rather than duplicated here -- that file is
the up-to-date, checkable version; this document stays focused on the
research and the gap analysis it's based on. The short version: a real
trained model is the one load-bearing gap everything else in that roadmap
sits on top of.
