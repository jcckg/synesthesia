#include "resyne/decoding/audio_decoder.h"
#include "resyne/decoding/decoder_wav.h"
#include "resyne/decoding/decoder_flac.h"
#include "resyne/decoding/decoder_mp3.h"
#include "resyne/decoding/decoder_ogg.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace AudioDecoding {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extractExtension(const std::string& path) {
    std::filesystem::path fsPath(path);
    return toLower(fsPath.extension().string());
}

}

bool decodeFile(const std::string& filepath, DecodedAudio& out, std::string& errorMessage) {
    out = DecodedAudio{};

    const std::string extension = extractExtension(filepath);

    if (extension == ".wav") {
        return decodeWav(filepath, out, errorMessage);
    }
    if (extension == ".flac") {
        return decodeFlac(filepath, out, errorMessage);
    }
    if (extension == ".mp3" || extension == ".mpeg3" || extension == ".mpga") {
        return decodeMp3(filepath, out, errorMessage);
    }
    if (extension == ".ogg" || extension == ".oga") {
        return decodeOgg(filepath, out, errorMessage);
    }

    errorMessage = "unsupported format";
    return false;
}

}
