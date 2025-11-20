#include "resyne/decoding/wav_decoder_impl.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

namespace WAVDecoder {

namespace {

bool readChunkHeader(std::ifstream& stream, char (&id)[4], uint32_t& size) {
    if (!stream.read(id, 4)) {
        return false;
    }
    if (!stream.read(reinterpret_cast<char*>(&size), sizeof(uint32_t))) {
        return false;
    }
    return true;
}

float readPCMValue(const uint8_t* data, uint16_t bitsPerSample) {
    switch (bitsPerSample) {
        case 8: {
            const float value = static_cast<float>(*data) / 255.0f;
            return value * 2.0f - 1.0f;
        }
        case 16: {
            int16_t sample;
            std::memcpy(&sample, data, sizeof(int16_t));
            return static_cast<float>(sample) / 32768.0f;
        }
        case 24: {
            int32_t value = static_cast<int32_t>(data[0]) |
                            (static_cast<int32_t>(data[1]) << 8) |
                            (static_cast<int32_t>(data[2]) << 16);
            if (value & 0x00800000) {
                value |= ~0x00FFFFFF;
            }
            return static_cast<float>(value) / 8388608.0f;
        }
        case 32: {
            int32_t sample;
            std::memcpy(&sample, data, sizeof(int32_t));
            return static_cast<float>(sample) / 2147483648.0f;
        }
        default:
            return 0.0f;
    }
}

float readFloatValue(const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(float));
    return value;
}

void skipPadding(std::ifstream& stream, uint32_t chunkSize) {
    if (chunkSize % 2 != 0) {
        stream.seekg(1, std::ios::cur);
    }
}

}

bool decodeFile(const std::string& filepath, DecodedWAV& out, std::string& errorMessage) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        errorMessage = "unable to open";
        return false;
    }

    char riff[4];
    if (!file.read(riff, 4) || std::strncmp(riff, "RIFF", 4) != 0) {
        errorMessage = "invalid header";
        return false;
    }

    file.seekg(4, std::ios::cur);

    char wave[4];
    if (!file.read(wave, 4) || std::strncmp(wave, "WAVE", 4) != 0) {
        errorMessage = "not WAVE";
        return false;
    }

    bool fmtFound = false;
    bool dataFound = false;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> dataChunk;

    while (file && !(fmtFound && dataFound)) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        if (!readChunkHeader(file, chunkId, chunkSize)) {
            break;
        }

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            fmtFound = true;

            if (!file.read(reinterpret_cast<char*>(&audioFormat), sizeof(uint16_t)) ||
                !file.read(reinterpret_cast<char*>(&channels), sizeof(uint16_t)) ||
                !file.read(reinterpret_cast<char*>(&sampleRate), sizeof(uint32_t))) {
                errorMessage = "malformed fmt";
                return false;
            }

            file.seekg(6, std::ios::cur);

            if (!file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(uint16_t))) {
                errorMessage = "malformed fmt";
                return false;
            }

            if (chunkSize > 16) {
                const uint16_t extra = static_cast<uint16_t>(chunkSize - 16);
                file.seekg(extra, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataFound = true;
            dataChunk.resize(chunkSize);
            if (!file.read(reinterpret_cast<char*>(dataChunk.data()), chunkSize)) {
                errorMessage = "malformed data";
                return false;
            }
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }

        skipPadding(file, chunkSize);
    }

    if (!fmtFound) {
        errorMessage = "missing fmt";
        return false;
    }
    if (!dataFound) {
        errorMessage = "missing data";
        return false;
    }

    if (audioFormat != 1 && audioFormat != 3) {
        errorMessage = "unsupported format";
        return false;
    }
    if (channels == 0 || sampleRate == 0) {
        errorMessage = "invalid stream";
        return false;
    }
    if (audioFormat == 3 && bitsPerSample != 32) {
        errorMessage = "unsupported float bit depth";
        return false;
    }
    if (audioFormat == 1 && bitsPerSample != 8 && bitsPerSample != 16 &&
        bitsPerSample != 24 && bitsPerSample != 32) {
        errorMessage = "unsupported bit depth";
        return false;
    }

    const uint16_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) {
        errorMessage = "invalid bit depth";
        return false;
    }

    const size_t totalSamples = dataChunk.size() / bytesPerSample;
    if (totalSamples < channels) {
        errorMessage = "no audio";
        return false;
    }

    const size_t frameCount = totalSamples / channels;
    out.channelSamples.clear();
    out.channelSamples.resize(channels);
    for (uint16_t channel = 0; channel < channels; ++channel) {
        out.channelSamples[channel].reserve(frameCount);
    }

    const uint8_t* raw = dataChunk.data();
    for (size_t frame = 0; frame < frameCount; ++frame) {
        for (uint16_t channel = 0; channel < channels; ++channel) {
            const uint8_t* samplePtr = raw + (frame * channels + channel) * bytesPerSample;
            float value = 0.0f;
            if (audioFormat == 3) {
                value = readFloatValue(samplePtr);
            } else {
                value = readPCMValue(samplePtr, bitsPerSample);
            }
            out.channelSamples[channel].push_back(value);
        }
    }

    out.sampleRate = sampleRate;
    out.channels = channels;

    return true;
}

}
