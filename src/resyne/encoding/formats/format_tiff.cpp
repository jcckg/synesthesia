#include "resyne/encoding/formats/format_tiff.h"

#include "resyne/encoding/spectral/colour_native_codec.h"
#include "resyne/recorder/loudness_utils.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE
#define TINY_DNG_LOADER_IMPLEMENTATION
#define TINY_DNG_WRITER_IMPLEMENTATION
#include <tiny_dng_loader.h>
#include <tiny_dng_writer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace SequenceExporterInternal {

namespace {

float srgbToLinear(const float value) {
	if (value <= 0.04045f) {
		return value / 12.92f;
	}
	return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float sanitiseFloat(const float value) {
	if (!std::isfinite(value)) {
		return 0.0f;
	}
	return std::clamp(value, 0.0f, 1.0f);
}

}

bool exportToTIFF(const std::string& filepath,
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
			emitProgress(encodeProgress * 0.65f);
		});
	if (image.width == 0 || image.height == 0) {
		emitProgress(1.0f);
		return false;
	}

	std::vector<float> imageData(image.width * image.height * 4);
	for (size_t row = 0; row < image.height; ++row) {
		const size_t invertedRow = image.height - 1 - row;
		for (size_t column = 0; column < image.width; ++column) {
			const RGBAColour& pixel = image.at(column, invertedRow);
			const size_t idx = (row * image.width + column) * 4;

			imageData[idx + 0] = pixel.r;
			imageData[idx + 1] = pixel.g;
			imageData[idx + 2] = pixel.b;
			imageData[idx + 3] = pixel.a;
		}

		if (image.height > 0) {
			const float rowProgress = static_cast<float>(row + 1) /
				static_cast<float>(image.height);
			emitProgress(0.65f + rowProgress * 0.25f);
		}
	}

	tinydngwriter::DNGImage dngImage;
	tinydngwriter::DNGWriter dngWriter(false);

	dngImage.SetBigEndian(false);
	dngImage.SetSubfileType(false, false, false);
	dngImage.SetImageWidth(static_cast<uint32_t>(image.width));
	dngImage.SetImageLength(static_cast<uint32_t>(image.height));
	dngImage.SetRowsPerStrip(static_cast<uint32_t>(image.height));
	dngImage.SetSamplesPerPixel(4);

	uint16_t bitsPerSample[4] = {32, 32, 32, 32};
	dngImage.SetBitsPerSample(4, bitsPerSample);

	uint16_t sampleFormat[4] = {
		tinydngwriter::SAMPLEFORMAT_IEEEFP,
		tinydngwriter::SAMPLEFORMAT_IEEEFP,
		tinydngwriter::SAMPLEFORMAT_IEEEFP,
		tinydngwriter::SAMPLEFORMAT_IEEEFP
	};
	dngImage.SetSampleFormat(4, sampleFormat);

	dngImage.SetPhotometric(tinydngwriter::PHOTOMETRIC_RGB);
	dngImage.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
	dngImage.SetCompression(tinydngwriter::COMPRESSION_NONE);
	dngImage.SetXResolution(1.0);
	dngImage.SetYResolution(1.0);
	dngImage.SetResolutionUnit(tinydngwriter::RESUNIT_NONE);

	dngImage.SetImageData(reinterpret_cast<const unsigned char*>(imageData.data()),
						  imageData.size() * sizeof(float));

	dngWriter.AddImage(&dngImage);

	std::string err;
	emitProgress(0.92f);
	const bool ok = dngWriter.WriteToFile(filepath.c_str(), &err);
	emitProgress(ok ? 1.0f : 0.92f);
	return ok;
}

bool loadFromTIFF(const std::string& filepath,
                 std::vector<AudioColourSample>& samples,
                 AudioMetadata& metadata,
                 const std::function<void(float)>& progress,
                 const SequenceFrameCallback& onFrameDecoded) {
	std::vector<tinydng::DNGImage> images;
	std::vector<tinydng::FieldInfo> customFields;
	std::string warn;
	std::string err;

	if (progress) {
		progress(0.02f);
	}

	if (!tinydng::LoadDNG(filepath.c_str(), customFields, &images, &warn, &err)) {
		return false;
	}

	if (images.empty()) {
		return false;
	}

	const tinydng::DNGImage& image = images[0];
	if (image.width <= 0 || image.height <= 0 || image.samples_per_pixel < 3) {
		return false;
	}

	if (progress) {
		progress(0.06f);
	}

	ColourNativeImage colourImage;
	colourImage.resize(static_cast<size_t>(image.width),
					   static_cast<size_t>(image.height));

	const size_t pixelStride = static_cast<size_t>(image.samples_per_pixel);

	if (image.sample_format == tinydng::SAMPLEFORMAT_IEEEFP) {
		const float* imageData = reinterpret_cast<const float*>(image.data.data());

		for (size_t row = 0; row < colourImage.height; ++row) {
			const size_t invertedRow = colourImage.height - 1 - row;
			for (size_t column = 0; column < colourImage.width; ++column) {
				const size_t idx = (row * colourImage.width + column) * pixelStride;

				const float r = sanitiseFloat(imageData[idx + 0]);
				const float g = sanitiseFloat(imageData[idx + 1]);
				const float b = sanitiseFloat(imageData[idx + 2]);
				const float a = pixelStride > 3 ? sanitiseFloat(imageData[idx + 3]) : 0.5f;

				colourImage.at(column, invertedRow) = {r, g, b, a};
			}

			if (progress) {
				const float rowProgress = static_cast<float>(row + 1) / static_cast<float>(colourImage.height);
				progress(0.08f + rowProgress * 0.48f);
			}
		}
	} else if (image.sample_format == tinydng::SAMPLEFORMAT_UINT) {
		if (image.bits_per_sample != 8 && image.bits_per_sample != 16) {
			return false;
		}

		if (image.bits_per_sample == 8) {
			const unsigned char* imageData = image.data.data();
			for (size_t row = 0; row < colourImage.height; ++row) {
				const size_t invertedRow = colourImage.height - 1 - row;
				for (size_t column = 0; column < colourImage.width; ++column) {
					const size_t idx = (row * colourImage.width + column) * pixelStride;

					const float r = srgbToLinear(static_cast<float>(imageData[idx + 0]) / 255.0f);
					const float g = srgbToLinear(static_cast<float>(imageData[idx + 1]) / 255.0f);
					const float b = srgbToLinear(static_cast<float>(imageData[idx + 2]) / 255.0f);
					const float a = pixelStride > 3
						? static_cast<float>(imageData[idx + 3]) / 255.0f
						: 0.5f;

					colourImage.at(column, invertedRow) = {
						std::clamp(r, 0.0f, 1.0f),
						std::clamp(g, 0.0f, 1.0f),
						std::clamp(b, 0.0f, 1.0f),
						std::clamp(a, 0.0f, 1.0f)
					};
				}

				if (progress) {
					const float rowProgress = static_cast<float>(row + 1) / static_cast<float>(colourImage.height);
					progress(0.08f + rowProgress * 0.48f);
				}
			}
		} else {
			const uint16_t* imageData = reinterpret_cast<const uint16_t*>(image.data.data());
			for (size_t row = 0; row < colourImage.height; ++row) {
				const size_t invertedRow = colourImage.height - 1 - row;
				for (size_t column = 0; column < colourImage.width; ++column) {
					const size_t idx = (row * colourImage.width + column) * pixelStride;

					const float r = srgbToLinear(static_cast<float>(imageData[idx + 0]) / 65535.0f);
					const float g = srgbToLinear(static_cast<float>(imageData[idx + 1]) / 65535.0f);
					const float b = srgbToLinear(static_cast<float>(imageData[idx + 2]) / 65535.0f);
					const float a = pixelStride > 3
						? static_cast<float>(imageData[idx + 3]) / 65535.0f
						: 0.5f;

					colourImage.at(column, invertedRow) = {
						std::clamp(r, 0.0f, 1.0f),
						std::clamp(g, 0.0f, 1.0f),
						std::clamp(b, 0.0f, 1.0f),
						std::clamp(a, 0.0f, 1.0f)
					};
				}

				if (progress) {
					const float rowProgress = static_cast<float>(row + 1) / static_cast<float>(colourImage.height);
					progress(0.08f + rowProgress * 0.48f);
				}
			}
		}
	} else {
		return false;
	}

	float detectedSampleRate = ColourNativeCodec::detectSampleRate(colourImage);

	const std::vector<size_t> commonBinCounts = {257, 513, 1025, 2049, 4097, 8193};

	size_t binCount = 0;
	uint32_t inferredChannels = 1;

	for (size_t candidateBinCount : commonBinCounts) {
		if (candidateBinCount > ColourNativeCodec::MAX_BIN_COUNT) {
			continue;
		}
		if (candidateBinCount > colourImage.height) {
			continue;
		}

		if (colourImage.height % candidateBinCount == 0) {
			const uint32_t candidateChannels = static_cast<uint32_t>(colourImage.height / candidateBinCount);
			if (candidateChannels >= 1 && candidateChannels <= 8) {
				binCount = candidateBinCount;
				inferredChannels = candidateChannels;
				break;
			}
		}
	}

	if (binCount == 0) {
		binCount = std::min(colourImage.height, ColourNativeCodec::MAX_BIN_COUNT);
		inferredChannels = 1;
	}

	const int fftSize = binCount > 1 ? static_cast<int>((binCount - 1) * 2) : 2;
	int hopSize = std::max(1, fftSize / 2);

	colourImage.metadata.sampleRate = detectedSampleRate;
	colourImage.metadata.fftSize = fftSize;
	colourImage.metadata.hopSize = hopSize;
	colourImage.metadata.numFrames = colourImage.width;
	colourImage.metadata.numBins = binCount;
	colourImage.metadata.channels = inferredChannels;
	colourImage.metadata.windowType = "hann";
	colourImage.metadata.version = "3.0.0";

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

	samples = ColourNativeCodec::decode(colourImage, detectedSampleRate, hopSize, frameCallback, decodeProgress);
	ReSyne::LoudnessUtils::calculateLoudnessFromSpectralFrames(samples, colourImage.metadata);

	colourImage.metadata.sampleRate = detectedSampleRate;
	colourImage.metadata.hopSize = hopSize;

	if (progress) {
		progress(0.96f);
	}

	const float finalSampleRate = samples.empty()
		? detectedSampleRate
		: samples.front().sampleRate;

	metadata.sampleRate = finalSampleRate;
	metadata.fftSize = fftSize;
	metadata.hopSize = hopSize;
	metadata.windowType = "hann";
	metadata.numFrames = samples.size();
	metadata.numBins = binCount;
	metadata.channels = inferredChannels;
	metadata.version = "3.0.0";

	if (progress) {
		progress(0.99f);
	}

	return true;
}

}
