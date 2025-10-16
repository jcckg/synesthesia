#include "resyne/encoding/formats/format_resyne.h"

#include "resyne/encoding/spectral/colour_native_codec.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>

namespace {
constexpr uint32_t RESYNE_MAGIC = 0x5253594E;
constexpr uint32_t RESYNE_VERSION = 3;
}

namespace SequenceExporterInternal {

bool exportToResyne(const std::string& filepath,
                    const std::vector<AudioColourSample>& samples,
                    const AudioMetadata& metadata,
                    const std::function<void(float)>& progress) {
	if (samples.empty()) {
		if (progress) {
			progress(1.0f);
		}
		return false;
	}

	const auto emitProgress = [&](float value) {
		if (!progress) {
			return;
		}
		const float clamped = std::clamp(value, 0.0f, 1.0f);
		progress(clamped);
	};

	emitProgress(0.0f);

	const ColourNativeImage image = ColourNativeCodec::encode(
		samples,
		metadata,
		[&](float encodeProgress) {
			emitProgress(encodeProgress * 0.7f);
		});
	if (image.width == 0 || image.height == 0) {
		emitProgress(1.0f);
		return false;
	}

	std::ofstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		emitProgress(1.0f);
		return false;
	}

	const uint32_t width = static_cast<uint32_t>(image.width);
	const uint32_t height = static_cast<uint32_t>(image.height);
	const float sampleRate = metadata.sampleRate > 0.0f
		? metadata.sampleRate
		: (!samples.empty() ? samples.front().sampleRate : 44100.0f);
	const uint32_t storedFftSize = metadata.fftSize > 0
		? static_cast<uint32_t>(metadata.fftSize)
		: (height > 1 ? static_cast<uint32_t>((height - 1) * 2) : 2u);
	const uint32_t storedHopSize = metadata.hopSize > 0
		? static_cast<uint32_t>(metadata.hopSize)
		: (storedFftSize > 0 ? storedFftSize / 2 : 1u);

	file.write(reinterpret_cast<const char*>(&RESYNE_MAGIC), sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&RESYNE_VERSION), sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&width), sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&height), sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&sampleRate), sizeof(float));
	file.write(reinterpret_cast<const char*>(&storedFftSize), sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&storedHopSize), sizeof(uint32_t));

	for (size_t y = 0; y < image.height; ++y) {
		for (size_t x = 0; x < image.width; ++x) {
			const RGBAColour& pixel = image.at(x, y);
			file.write(reinterpret_cast<const char*>(&pixel.r), sizeof(float));
			file.write(reinterpret_cast<const char*>(&pixel.g), sizeof(float));
			file.write(reinterpret_cast<const char*>(&pixel.b), sizeof(float));
			file.write(reinterpret_cast<const char*>(&pixel.a), sizeof(float));
		}

		if (image.height > 0) {
			const float rowProgress = static_cast<float>(y + 1) /
				static_cast<float>(image.height);
			emitProgress(0.7f + rowProgress * 0.25f);
		}
	}

	const bool ok = file.good();
	emitProgress(1.0f);
	return ok;
}

bool loadFromResyne(const std::string& filepath,
                    std::vector<AudioColourSample>& samples,
                    AudioMetadata& metadata,
                    const std::function<void(float)>& progress,
                    const SequenceFrameCallback& onFrameDecoded) {
	std::ifstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	if (progress) {
		progress(0.02f);
	}

	uint32_t magic = 0;
	uint32_t version = 0;
	uint32_t width = 0;
	uint32_t height = 0;

	file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
	file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
	file.read(reinterpret_cast<char*>(&width), sizeof(uint32_t));
	file.read(reinterpret_cast<char*>(&height), sizeof(uint32_t));

	if (!file.good() || magic != RESYNE_MAGIC || width == 0 || height == 0 || (version != RESYNE_VERSION && version != 2)) {
		return false;
	}

	float storedSampleRate = 0.0f;
	uint32_t storedFftSize = 0;
	uint32_t storedHopSize = 0;

	if (version >= 3) {
		file.read(reinterpret_cast<char*>(&storedSampleRate), sizeof(float));
		file.read(reinterpret_cast<char*>(&storedFftSize), sizeof(uint32_t));
		file.read(reinterpret_cast<char*>(&storedHopSize), sizeof(uint32_t));
		if (!file.good()) {
			return false;
		}
	}

	ColourNativeImage image;
	image.resize(width, height);

	for (size_t y = 0; y < image.height; ++y) {
		for (size_t x = 0; x < image.width; ++x) {
			RGBAColour pixel{};
			file.read(reinterpret_cast<char*>(&pixel.r), sizeof(float));
			file.read(reinterpret_cast<char*>(&pixel.g), sizeof(float));
			file.read(reinterpret_cast<char*>(&pixel.b), sizeof(float));
			file.read(reinterpret_cast<char*>(&pixel.a), sizeof(float));
			if (!file.good()) {
				return false;
			}
			image.at(x, y) = pixel;
		}

		if (progress) {
			const float rowProgress = static_cast<float>(y + 1) / static_cast<float>(image.height);
			progress(0.06f + rowProgress * 0.5f);
		}
	}

	float detectedSampleRate = ColourNativeCodec::detectSampleRate(image);
	const size_t binCount = std::min(image.height, ColourNativeCodec::MAX_BIN_COUNT);
	int resolvedFftSize = binCount > 1 ? static_cast<int>((binCount - 1) * 2) : 2;
	int resolvedHopSize = std::max(1, resolvedFftSize / 2);

	if (version >= 3) {
		if (storedSampleRate > 0.0f) {
			detectedSampleRate = storedSampleRate;
		}
		if (storedFftSize > 0) {
			resolvedFftSize = static_cast<int>(storedFftSize);
		}
		if (storedHopSize > 0) {
			resolvedHopSize = static_cast<int>(storedHopSize);
		}
	}

	image.metadata.sampleRate = detectedSampleRate;
	image.metadata.fftSize = resolvedFftSize;
	image.metadata.hopSize = resolvedHopSize;
	image.metadata.numFrames = image.width;
	image.metadata.numBins = binCount;
	image.metadata.version = "3.0.0";
	image.metadata.windowType = "hann";

	auto decodeProgress = [&](float value) {
		if (!progress) {
			return;
		}
		const float clamped = std::clamp(value, 0.0f, 1.0f);
		progress(0.56f + clamped * 0.4f);
	};
	auto frameCallback = [&](const std::vector<AudioColourSample>& decoded, size_t validCount) {
		if (onFrameDecoded) {
			onFrameDecoded(decoded, validCount);
		}
	};

	samples = ColourNativeCodec::decode(image, detectedSampleRate, resolvedHopSize, frameCallback, decodeProgress);

	if (progress) {
		progress(0.96f);
	}

	image.metadata.sampleRate = detectedSampleRate;
	image.metadata.hopSize = resolvedHopSize;

	const float finalSampleRate = samples.empty()
		? detectedSampleRate
		: samples.front().sampleRate;

	metadata.sampleRate = finalSampleRate;
	metadata.fftSize = resolvedFftSize;
	metadata.hopSize = resolvedHopSize;
	metadata.windowType = "hann";
	metadata.numFrames = samples.size();
	metadata.numBins = binCount;
	metadata.version = "3.0.0";

	if (progress) {
		progress(0.99f);
	}

	return true;
}

}
