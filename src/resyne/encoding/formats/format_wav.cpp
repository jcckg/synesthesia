#include "resyne/encoding/formats/format_wav.h"
#include "resyne/encoding/audio/wav_encoder.h"

#include <algorithm>
#include <iostream>

namespace SequenceExporterInternal {

bool exportToWAV(const std::string& filepath,
                const std::vector<AudioColourSample>& samples,
                const AudioMetadata& metadata,
                const std::function<void(float)>& progress) {
	const auto emitProgress = [&](float value) {
		if (!progress) {
			return;
		}
		const float clamped = std::clamp(value, 0.0f, 1.0f);
		progress(clamped);
	};

	emitProgress(samples.empty() ? 1.0f : 0.0f);


	auto result = WAVEncoder::reconstructFromSpectralData(
		samples,
		metadata.sampleRate,
		metadata.fftSize,
		metadata.hopSize,
        metadata.analysisLeadingSamples, metadata.originalSampleCount, true,
        [&](float fraction) { emitProgress(0.3f + 0.45f * fraction); }
	);

	if (!result.success) {
		emitProgress(1.0f);
		return false;
	}

	emitProgress(0.8f);

	const bool ok = WAVEncoder::exportToWAV(
		filepath,
		result.audioSamples,
		result.sampleRate,
		result.numChannels,
        [&](float fraction) { emitProgress(0.8f + 0.2f * fraction); }
	);

	emitProgress(ok ? 1.0f : 0.9f);

	return ok;
}

}
