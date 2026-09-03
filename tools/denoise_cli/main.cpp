// SPDX-License-Identifier: Apache-2.0
//
// Offline WAV-in -> pipeline -> WAV-out tool. This is the primary
// end-to-end verification path for the core pipeline: it needs no audio
// hardware or OS-specific virtual device integration, so it runs anywhere
// the core library builds (and is what CI exercises).

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "denoise/framer.h"
#include "denoise/pipeline.h"
#include "denoise/resampler.h"
#include "wav_io.h"

namespace {

struct Args {
    std::string inputPath;
    std::string outputPath;
    std::optional<std::string> vadModel;
    std::optional<std::string> denoiseModel;
    std::optional<std::string> speakerModel;
    std::size_t frameSize = 480;
    double processingRate = 48000.0;
    bool gateByVad = false;
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage: %s --in <in.wav> --out <out.wav> [options]\n"
                  "\n"
                  "Options:\n"
                  "  --vad-model <path.onnx>       load a VAD stage model\n"
                  "  --denoise-model <path.onnx>   load a denoise stage model\n"
                  "  --speaker-model <path.onnx>   load a speaker-isolation stage model\n"
                  "  --frame-size <N>              samples per frame (default 480)\n"
                  "  --rate <Hz>                   internal processing sample rate (default 48000)\n"
                  "  --gate-vad                    multiply output by the VAD probability\n"
                  "\n"
                  "With no model flags, all stages run in passthrough (the output is the\n"
                  "input, resampled to --rate and re-quantized to 16-bit PCM).\n",
                  argv0);
}

std::optional<Args> parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* flag) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", flag);
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };

        if (a == "--in") {
            auto v = next("--in");
            if (!v) return std::nullopt;
            args.inputPath = *v;
        } else if (a == "--out") {
            auto v = next("--out");
            if (!v) return std::nullopt;
            args.outputPath = *v;
        } else if (a == "--vad-model") {
            auto v = next("--vad-model");
            if (!v) return std::nullopt;
            args.vadModel = *v;
        } else if (a == "--denoise-model") {
            auto v = next("--denoise-model");
            if (!v) return std::nullopt;
            args.denoiseModel = *v;
        } else if (a == "--speaker-model") {
            auto v = next("--speaker-model");
            if (!v) return std::nullopt;
            args.speakerModel = *v;
        } else if (a == "--frame-size") {
            auto v = next("--frame-size");
            if (!v) return std::nullopt;
            args.frameSize = static_cast<std::size_t>(std::stoul(*v));
        } else if (a == "--rate") {
            auto v = next("--rate");
            if (!v) return std::nullopt;
            args.processingRate = std::stod(*v);
        } else if (a == "--gate-vad") {
            args.gateByVad = true;
        } else if (a == "-h" || a == "--help") {
            return std::nullopt;
        } else {
            std::fprintf(stderr, "unrecognized argument: %s\n", a.c_str());
            return std::nullopt;
        }
    }
    if (args.inputPath.empty() || args.outputPath.empty()) {
        std::fprintf(stderr, "--in and --out are required\n");
        return std::nullopt;
    }
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    auto argsOpt = parseArgs(argc, argv);
    if (!argsOpt) {
        printUsage(argv[0]);
        return 2;
    }
    Args args = *argsOpt;

    denoise::cli::WavData input;
    std::string err;
    if (!denoise::cli::readWav(args.inputPath, input, &err)) {
        std::fprintf(stderr, "failed to read '%s': %s\n", args.inputPath.c_str(), err.c_str());
        return 1;
    }
    std::fprintf(stderr, "read '%s': %zu samples @ %u Hz\n", args.inputPath.c_str(), input.samples.size(),
                 input.sampleRate);

    std::vector<float> processingRateSamples;
    if (static_cast<double>(input.sampleRate) == args.processingRate) {
        processingRateSamples = input.samples;
    } else {
        denoise::LinearResampler resampler(static_cast<double>(input.sampleRate), args.processingRate);
        processingRateSamples = resampler.process(input.samples);
        std::fprintf(stderr, "resampled %u Hz -> %.0f Hz (%zu samples)\n", input.sampleRate,
                     args.processingRate, processingRateSamples.size());
    }

    denoise::DenoisePipeline pipeline(args.frameSize);
    denoise::Stage& vad = pipeline.addStage(denoise::StageKind::Vad, "vad");
    denoise::Stage& denoiseStage = pipeline.addStage(denoise::StageKind::Denoise, "denoise");
    denoise::Stage& speaker = pipeline.addStage(denoise::StageKind::SpeakerIsolation, "speaker");

    struct StageBinding {
        denoise::Stage& stage;
        const std::optional<std::string>& modelPath;
    };
    for (auto& binding : std::vector<StageBinding>{
             {vad, args.vadModel}, {denoiseStage, args.denoiseModel}, {speaker, args.speakerModel}}) {
        if (!binding.modelPath) continue;
        std::string loadErr;
        if (!binding.stage.loadModel(*binding.modelPath, &loadErr)) {
            std::fprintf(stderr, "failed to load model '%s' for stage '%s': %s\n",
                         binding.modelPath->c_str(), binding.stage.name().c_str(), loadErr.c_str());
            return 1;
        }
        std::fprintf(stderr, "loaded %s stage model: %s\n", binding.stage.name().c_str(),
                     binding.modelPath->c_str());
    }

    denoise::Framer framer(args.frameSize, args.frameSize);  // non-overlapping frames
    framer.push(processingRateSamples.data(), processingRateSamples.size());

    std::vector<float> output;
    output.reserve(processingRateSamples.size());

    std::size_t framesProcessed = 0;
    std::vector<float> frame;
    while (framer.tryGetFrame(frame)) {
        denoise::FrameContext ctx = pipeline.processFrame(frame, args.gateByVad);
        output.insert(output.end(), ctx.samples.begin(), ctx.samples.end());
        ++framesProcessed;
    }
    if (framer.buffered() > 0) {
        std::fprintf(stderr, "warning: %zu trailing samples did not fill a full frame and were dropped\n",
                     framer.buffered());
    }
    std::fprintf(stderr, "processed %zu frames of %zu samples\n", framesProcessed, args.frameSize);

    if (!denoise::cli::writeWav(args.outputPath, static_cast<uint32_t>(args.processingRate), output, &err)) {
        std::fprintf(stderr, "failed to write '%s': %s\n", args.outputPath.c_str(), err.c_str());
        return 1;
    }
    std::fprintf(stderr, "wrote '%s': %zu samples @ %.0f Hz\n", args.outputPath.c_str(), output.size(),
                 args.processingRate);
    return 0;
}
