#include "resyne/encoding/formats/format_wav.h"
#include "resyne/encoding/audio/wav_encoder.h"

#include <algorithm>

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

	std::vector<SpectralSample> spectralSamples;
	spectralSamples.reserve(samples.size());

	for (size_t index = 0; index < samples.size(); ++index) {
		const auto& sample = samples[index];
		SpectralSample spectral;
		spectral.magnitudes = sample.magnitudes;
		spectral.phases = sample.phases;
		spectral.timestamp = sample.timestamp;
		spectral.sampleRate = sample.sampleRate;
		spectralSamples.push_back(spectral);

		if (!samples.empty()) {
			const float progressRatio = static_cast<float>(index + 1) /
				static_cast<float>(samples.size());
			emitProgress(progressRatio * 0.3f);
		}
	}

	auto result = WAVEncoder::reconstructFromSpectralData(
		spectralSamples,
		metadata.sampleRate,
		metadata.fftSize,
		metadata.hopSize
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
		result.numChannels
	);

	emitProgress(ok ? 1.0f : 0.9f);

	return ok;
}

}
