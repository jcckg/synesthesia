#include "resyne/decoding/decoder_wav.h"
#include "resyne/decoding/wav_decoder_impl.h"

namespace AudioDecoding {

bool decodeWav(const std::string& filepath, DecodedAudio& out, std::string& error) {
    WAVDecoder::DecodedWAV wav;
    if (!WAVDecoder::decodeFile(filepath, wav, error)) {
        return false;
    }
    out.channelSamples = std::move(wav.channelSamples);
    out.sampleRate = wav.sampleRate;
    out.channels = wav.channels;
    if (out.channelSamples.empty()) {
        error = "empty wav";
        return false;
    }
    return true;
}

}
