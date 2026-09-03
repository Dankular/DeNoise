// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace denoise::cli {

struct WavData {
    uint32_t sampleRate = 0;
    std::vector<float> samples;  // mono, in [-1, 1]
};

// Reads a canonical RIFF/WAVE file. Supports PCM (8/16/24/32-bit int) and
// IEEE-float (32-bit) sample formats, any channel count and sample rate.
// Multi-channel input is downmixed to mono by averaging channels. Returns
// false and sets *errorOut on any parse failure or unsupported format.
bool readWav(const std::string& path, WavData& out, std::string* errorOut = nullptr);

// Writes a mono 16-bit PCM WAV file. Samples are clamped to [-1, 1] before
// quantization. Returns false and sets *errorOut on failure.
bool writeWav(const std::string& path, uint32_t sampleRate, const std::vector<float>& samples,
              std::string* errorOut = nullptr);

}  // namespace denoise::cli
