// SPDX-License-Identifier: Apache-2.0
#include "wav_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace denoise::cli {

namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t readU16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void writeU32LE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

}  // namespace

bool readWav(const std::string& path, WavData& out, std::string* errorOut) {
    auto fail = [&](const std::string& msg) {
        if (errorOut) *errorOut = msg;
        return false;
    };

    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("could not open '" + path + "'");

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12) return fail("file too small to be a WAV file");
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0) return fail("missing RIFF header");
    if (std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return fail("missing WAVE tag");

    bool haveFmt = false;
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;

    const uint8_t* dataPtr = nullptr;
    uint32_t dataSize = 0;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, bytes.data() + pos, 4);
        uint32_t chunkSize = readU32LE(bytes.data() + pos + 4);
        std::size_t chunkDataStart = pos + 8;

        if (chunkDataStart + chunkSize > bytes.size()) {
            // Truncated/malformed trailing chunk; stop parsing but keep
            // whatever valid data we already found.
            break;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) return fail("fmt chunk too small");
            const uint8_t* p = bytes.data() + chunkDataStart;
            audioFormat = readU16LE(p + 0);
            numChannels = readU16LE(p + 2);
            sampleRate = readU32LE(p + 4);
            bitsPerSample = readU16LE(p + 14);

            if (audioFormat == kFormatExtensible && chunkSize >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: real sample format is the first two
                // bytes of the SubFormat GUID, at offset 24 within the chunk.
                audioFormat = readU16LE(p + 24);
            }
            haveFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataPtr = bytes.data() + chunkDataStart;
            dataSize = chunkSize;
        }

        pos = chunkDataStart + chunkSize + (chunkSize % 2);  // chunks are word-aligned
    }

    if (!haveFmt) return fail("no fmt chunk found");
    if (dataPtr == nullptr) return fail("no data chunk found");
    if (numChannels == 0) return fail("fmt chunk declares 0 channels");

    const std::size_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) return fail("unsupported bitsPerSample=0");
    const std::size_t frameBytes = bytesPerSample * numChannels;
    if (frameBytes == 0 || dataSize % frameBytes != 0) {
        return fail("data chunk size is not a whole number of sample frames");
    }
    const std::size_t numFrames = dataSize / frameBytes;

    std::vector<float> mono(numFrames, 0.0f);

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        float sum = 0.0f;
        const uint8_t* frameStart = dataPtr + frame * frameBytes;
        for (std::size_t ch = 0; ch < numChannels; ++ch) {
            const uint8_t* s = frameStart + ch * bytesPerSample;
            float v = 0.0f;
            if (audioFormat == kFormatPcm) {
                switch (bitsPerSample) {
                    case 8: {
                        // 8-bit PCM WAV is conventionally unsigned.
                        v = (static_cast<int32_t>(s[0]) - 128) / 128.0f;
                        break;
                    }
                    case 16: {
                        int16_t raw = static_cast<int16_t>(readU16LE(s));
                        v = raw / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t raw = (s[0]) | (s[1] << 8) | (s[2] << 16);
                        if (raw & 0x800000) raw |= static_cast<int32_t>(0xFF000000);
                        v = raw / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t raw = static_cast<int32_t>(readU32LE(s));
                        v = raw / 2147483648.0f;
                        break;
                    }
                    default:
                        return fail("unsupported PCM bitsPerSample=" + std::to_string(bitsPerSample));
                }
            } else if (audioFormat == kFormatIeeeFloat) {
                if (bitsPerSample != 32) {
                    return fail("unsupported IEEE float bitsPerSample=" + std::to_string(bitsPerSample));
                }
                float f;
                std::memcpy(&f, s, sizeof(float));
                v = f;
            } else {
                return fail("unsupported audioFormat=" + std::to_string(audioFormat));
            }
            sum += v;
        }
        mono[frame] = sum / static_cast<float>(numChannels);
    }

    out.sampleRate = sampleRate;
    out.samples = std::move(mono);
    return true;
}

bool writeWav(const std::string& path, uint32_t sampleRate, const std::vector<float>& samples,
              std::string* errorOut) {
    auto fail = [&](const std::string& msg) {
        if (errorOut) *errorOut = msg;
        return false;
    };

    std::ofstream f(path, std::ios::binary);
    if (!f) return fail("could not open '" + path + "' for writing");

    constexpr uint16_t kChannels = 1;
    constexpr uint16_t kBitsPerSample = 16;
    const uint32_t byteRate = sampleRate * kChannels * (kBitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(kChannels * (kBitsPerSample / 8));
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));

    std::vector<uint8_t> header;
    header.reserve(44);
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    writeU32LE(header, 36 + dataSize);
    header.insert(header.end(), {'W', 'A', 'V', 'E'});
    header.insert(header.end(), {'f', 'm', 't', ' '});
    writeU32LE(header, 16);
    writeU16LE(header, kFormatPcm);
    writeU16LE(header, kChannels);
    writeU32LE(header, sampleRate);
    writeU32LE(header, byteRate);
    writeU16LE(header, blockAlign);
    writeU16LE(header, kBitsPerSample);
    header.insert(header.end(), {'d', 'a', 't', 'a'});
    writeU32LE(header, dataSize);

    f.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    std::vector<int16_t> pcm(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        float clamped = std::clamp(samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
    }
    f.write(reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size() * sizeof(int16_t)));

    return f.good();
}

}  // namespace denoise::cli
