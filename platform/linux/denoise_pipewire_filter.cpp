// SPDX-License-Identifier: Apache-2.0
//
// PipeWire virtual-mic filter: a pw_filter node with one DSP input port and
// one DSP output port. Pipe a real microphone into the input port and
// capture the output port as the processed signal (see README.md in this
// directory for exactly how to wire that up with pw-link, and for what is
// and is NOT verified about this file).
//
// pw_filter API usage here is modeled directly on PipeWire's own
// `src/examples/audio-dsp-filter.c` reference example (fetched from
// https://github.com/PipeWire/pipewire at HEAD while writing this), not
// reconstructed from memory.
//
// IMPORTANT REALTIME CAVEAT: ONNX Runtime's Session::Run() allocates memory
// and is not realtime-safe. This filter therefore does NOT pass
// PW_FILTER_FLAG_RT_PROCESS, so `on_process` runs on PipeWire's mainloop
// thread rather than the hard-realtime graph thread. That is the safe
// choice for correctness but is not the lowest-latency configuration a
// production driver would want; moving inference to a dedicated worker
// thread (feeding the RT thread through lock-free queues) is future work,
// not implemented here.

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <pipewire/pipewire.h>
#include <pipewire/filter.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "denoise/framer.h"
#include "denoise/pipeline.h"
#include "denoise/ring_buffer.h"

namespace {

struct Port {
    // pw_filter_add_port gives back a pointer to a caller-sized blob; we
    // don't need per-port state beyond identifying which port this is, so
    // this struct is intentionally empty (see FilterData for shared state).
};

struct FilterData {
    pw_main_loop* loop = nullptr;
    pw_filter* filter = nullptr;
    Port* inPort = nullptr;
    Port* outPort = nullptr;

    std::size_t frameSize = 480;
    bool gateByVad = false;

    denoise::DenoisePipeline* pipeline = nullptr;
    denoise::Framer* framer = nullptr;
    denoise::RingBuffer* outputRing = nullptr;
};

void onProcess(void* userdata, spa_io_position* position) {
    auto* d = static_cast<FilterData*>(userdata);
    const uint32_t nSamples = position->clock.duration;

    auto* in = static_cast<float*>(pw_filter_get_dsp_buffer(d->inPort, nSamples));
    auto* out = static_cast<float*>(pw_filter_get_dsp_buffer(d->outPort, nSamples));
    if (in == nullptr || out == nullptr) return;

    d->framer->push(in, nSamples);

    std::vector<float> frame;
    while (d->framer->tryGetFrame(frame)) {
        denoise::FrameContext ctx = d->pipeline->processFrame(frame, d->gateByVad);
        d->outputRing->push(ctx.samples.data(), ctx.samples.size());
    }

    const std::size_t avail = d->outputRing->availableToRead();
    if (avail >= nSamples) {
        d->outputRing->pop(out, nSamples);
    } else {
        // Startup/underrun: emit what we have and pad with silence rather
        // than uninitialized memory or a truncated read.
        d->outputRing->pop(out, avail);
        std::fill(out + avail, out + nSamples, 0.0f);
    }
}

const pw_filter_events kFilterEvents = [] {
    pw_filter_events events{};
    events.version = PW_VERSION_FILTER_EVENTS;
    events.process = onProcess;
    return events;
}();

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage: %s [options]\n"
                  "\n"
                  "Creates a PipeWire filter node \"denoise-filter\" with one DSP input\n"
                  "port (\"input\") and one DSP output port (\"output\"). Link a real\n"
                  "microphone's capture port to the input, and have consuming apps read\n"
                  "from the output -- see platform/linux/README.md.\n"
                  "\n"
                  "Options:\n"
                  "  --vad-model <path.onnx>\n"
                  "  --denoise-model <path.onnx>\n"
                  "  --speaker-model <path.onnx>\n"
                  "  --frame-size <N>     samples per pipeline frame (default 480)\n"
                  "  --gate-vad           multiply output by the VAD probability\n",
                  argv0);
}

}  // namespace

int main(int argc, char** argv) {
    FilterData data;
    std::optional<std::string> vadModel, denoiseModel, speakerModel;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string(argv[++i]);
        };
        if (a == "--vad-model") {
            vadModel = next();
        } else if (a == "--denoise-model") {
            denoiseModel = next();
        } else if (a == "--speaker-model") {
            speakerModel = next();
        } else if (a == "--frame-size") {
            auto v = next();
            if (!v) { printUsage(argv[0]); return 2; }
            data.frameSize = static_cast<std::size_t>(std::stoul(*v));
        } else if (a == "--gate-vad") {
            data.gateByVad = true;
        } else if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unrecognized argument: %s\n", a.c_str());
            printUsage(argv[0]);
            return 2;
        }
    }

    denoise::DenoisePipeline pipeline(data.frameSize);
    denoise::Stage& vad = pipeline.addStage(denoise::StageKind::Vad, "vad");
    denoise::Stage& denoiseStage = pipeline.addStage(denoise::StageKind::Denoise, "denoise");
    denoise::Stage& speaker = pipeline.addStage(denoise::StageKind::SpeakerIsolation, "speaker");

    struct Binding { denoise::Stage& stage; const std::optional<std::string>& path; };
    for (auto& b : std::vector<Binding>{{vad, vadModel}, {denoiseStage, denoiseModel}, {speaker, speakerModel}}) {
        if (!b.path) continue;
        std::string err;
        if (!b.stage.loadModel(*b.path, &err)) {
            std::fprintf(stderr, "failed to load model '%s' for stage '%s': %s\n", b.path->c_str(),
                         b.stage.name().c_str(), err.c_str());
            return 1;
        }
        std::fprintf(stderr, "loaded %s stage model: %s\n", b.stage.name().c_str(), b.path->c_str());
    }

    denoise::Framer framer(data.frameSize, data.frameSize);
    // A few frames of headroom so the ring buffer can never be the
    // bottleneck; actual latency is bounded by frameSize/sampleRate either way.
    denoise::RingBuffer outputRing(data.frameSize * 8);

    data.pipeline = &pipeline;
    data.framer = &framer;
    data.outputRing = &outputRing;

    pw_init(&argc, &argv);

    data.loop = pw_main_loop_new(nullptr);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT,
                        [](void* d, int) { pw_main_loop_quit(static_cast<FilterData*>(d)->loop); }, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM,
                        [](void* d, int) { pw_main_loop_quit(static_cast<FilterData*>(d)->loop); }, &data);

    data.filter = pw_filter_new_simple(
        pw_main_loop_get_loop(data.loop), "denoise-filter",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Filter", PW_KEY_MEDIA_ROLE,
                           "DSP", PW_KEY_NODE_DESCRIPTION, "DeNoise Voice Filter", PW_KEY_NODE_PASSIVE,
                           "follow", nullptr),
        &kFilterEvents, &data);

    data.inPort = static_cast<Port*>(pw_filter_add_port(
        data.filter, PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(Port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, "input", nullptr),
        nullptr, 0));

    data.outPort = static_cast<Port*>(pw_filter_add_port(
        data.filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(Port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, "output", nullptr),
        nullptr, 0));

    if (pw_filter_connect(data.filter, PW_FILTER_FLAG_NONE, nullptr, 0) < 0) {
        std::fprintf(stderr, "failed to connect filter\n");
        return 1;
    }

    std::fprintf(stderr, "denoise-filter running (frame size %zu). Ctrl-C to stop.\n", data.frameSize);
    pw_main_loop_run(data.loop);

    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();
    return 0;
}
