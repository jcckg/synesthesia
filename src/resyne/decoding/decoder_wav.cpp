#include "resyne/decoding/decoder_wav.h"
#include "resyne/decoding/wav_decoder_impl.h"

namespace AudioDecoding {

bool decodeWav(const std::string& filepath, DecodedAudio& out, std::string& error) {
    WAVDecoder::DecodedWAV wav;
    if (!WAVDecoder::decodeFile(filepath, wav, error)) {
        return false;
    }
    out.samples = std::move(wav.samples);
    out.sampleRate = wav.sampleRate;
    out.channels = wav.channels;
    if (out.samples.empty()) {
        error = "empty wav";
        return false;
    }
    return true;
}

}
