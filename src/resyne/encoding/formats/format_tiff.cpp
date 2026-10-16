#include "resyne/encoding/formats/format_tiff.h"

#include "resyne/encoding/spectral/colour_native_codec.h"
#include "resyne/encoding/formats/tiff_metadata.h"
#include "resyne/recorder/loudness_utils.h"

#include <tinydng.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace SequenceExporterInternal {

namespace {
constexpr std::array<size_t, 6> COMMON_BIN_COUNTS = {257, 513, 1025, 2049, 4097, 8193};

struct DecodedPixels {
    tinydng_context* context;
    tinydng_pixels data{};

    ~DecodedPixels() {
        tinydng_pixels_free(context, &data);
    }
};

struct OutputSink {
    tinydng_write_io data{};

    ~OutputSink() {
        if (data.close != nullptr) {
            data.close(&data);
        }
    }
};

using DngContext = std::unique_ptr<tinydng_context, decltype(&tinydng_context_destroy)>;

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
	return value;
}

size_t resolveBinCount(const std::vector<AudioColourSample>& samples,
						 const AudioMetadata& metadata) {
	if (metadata.numBins > 0) {
		return metadata.numBins;
	}
	if (samples.empty() || samples.front().magnitudes.empty()) {
		return 0;
	}
	return samples.front().magnitudes.front().size();
}

uint32_t resolveStoredSampleRate(const std::vector<AudioColourSample>& samples,
								   const AudioMetadata& metadata) {
	const float sampleRate = metadata.sampleRate > 0.0f
		? metadata.sampleRate
		: (!samples.empty() ? samples.front().sampleRate : 0.0f);
	if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
		return 0;
	}
	const double roundedSampleRate = std::round(static_cast<double>(sampleRate));
	const double clampedSampleRate = std::clamp(
		roundedSampleRate,
		1.0,
		static_cast<double>(std::numeric_limits<uint32_t>::max()));
	return static_cast<uint32_t>(clampedSampleRate);
}

uint32_t resolveStoredFftSize(const std::vector<AudioColourSample>& samples,
								const AudioMetadata& metadata) {
	if (metadata.fftSize > 0) {
		return static_cast<uint32_t>(metadata.fftSize);
	}
	const size_t numBins = resolveBinCount(samples, metadata);
	if (numBins <= 1) {
		return 2;
	}
	return static_cast<uint32_t>((numBins - 1) * 2);
}

uint32_t resolveStoredHopSize(const AudioMetadata& metadata,
							  const uint32_t storedFftSize) {
	if (metadata.hopSize > 0) {
		return static_cast<uint32_t>(metadata.hopSize);
	}
	return storedFftSize > 0 ? std::max(1u, storedFftSize / 2) : 0u;
}

uint32_t resolveStoredChannels(const std::vector<AudioColourSample>& samples,
								 const AudioMetadata& metadata) {
	if (metadata.channels > 0) {
		return metadata.channels;
	}
	return !samples.empty() ? std::max(1u, samples.front().channels) : 1u;
}

bool resolveImageLayout(const size_t height,
						  const uint32_t preferredChannels,
						  size_t& binCount,
						  uint32_t& channels) {
	auto acceptLayout = [&](const uint32_t candidateChannels) {
		if (candidateChannels == 0 || candidateChannels > 8 || candidateChannels > height) {
			return false;
		}
		if ((height % candidateChannels) != 0) {
			return false;
		}
		const size_t candidateBinCount = height / candidateChannels;
		if (candidateBinCount == 0 || candidateBinCount > ColourNativeCodec::MAX_BIN_COUNT) {
			return false;
		}
		binCount = candidateBinCount;
		channels = candidateChannels;
		return true;
	};

	if (preferredChannels > 0 && acceptLayout(preferredChannels)) {
		return true;
	}

	for (const size_t candidateBinCount : COMMON_BIN_COUNTS) {
		if (candidateBinCount > height || candidateBinCount > ColourNativeCodec::MAX_BIN_COUNT) {
			continue;
		}
		if ((height % candidateBinCount) != 0) {
			continue;
		}

		const uint32_t candidateChannels = static_cast<uint32_t>(height / candidateBinCount);
		if (candidateChannels >= 1 && candidateChannels <= 8) {
			binCount = candidateBinCount;
			channels = candidateChannels;
			return true;
		}
	}

	if (height == 0 || height > ColourNativeCodec::MAX_BIN_COUNT) {
		return false;
	}

	return acceptLayout(1);
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

    if (image.width > std::numeric_limits<uint32_t>::max() ||
        image.height > std::numeric_limits<uint32_t>::max() ||
        image.width > std::numeric_limits<size_t>::max() / 4u / sizeof(float)) {
        return false;
    }

    tinydng_error error{};
    DngContext context(tinydng_context_create(nullptr, &error), tinydng_context_destroy);
    if (!context) {
        return false;
    }
    OutputSink sink;
    if (tinydng_write_io_open_file(context.get(), filepath.c_str(), &sink.data, &error) != TINYDNG_OK) {
        return false;
    }

    tinydng_write_image output{};
    output.width = static_cast<uint32_t>(image.width);
    output.height = static_cast<uint32_t>(image.height);
    output.samples_per_pixel = 4;
    output.bits_per_sample = 32;
    output.sample_format = TINYDNG_SAMPLEFORMAT_IEEEFP;
    output.photometric = 2;

    constexpr size_t rowsPerStrip = 64;
    const size_t stripRows = std::min(image.height, rowsPerStrip);
    if (image.width > std::numeric_limits<size_t>::max() / stripRows / 4u / sizeof(float)) {
        return false;
    }
    std::vector<float> stripPixels(image.width * stripRows * 4u);
    tinydng_write_options options{};
    options.compression = TINYDNG_COMPRESSION_NONE;
    options.bigtiff = image.width > (std::numeric_limits<uint32_t>::max() - 1024u * 1024u) /
        image.height / 4u / sizeof(float);
    tinydng_tiling layout{};
    layout.rows_per_strip = static_cast<uint32_t>(rowsPerStrip);

    tinydng_writer* rawWriter = nullptr;
    if (tinydng_writer_create(context.get(), sink.data, &output, &options, &layout, &rawWriter, &error) != TINYDNG_OK) {
        return false;
    }
    const auto releaseWriter = [](tinydng_writer* writer) { tinydng_writer_finish(writer, nullptr); };
    std::unique_ptr<tinydng_writer, decltype(releaseWriter)> writer(rawWriter, releaseWriter);
    for (size_t firstRow = 0; firstRow < image.height; firstRow += rowsPerStrip) {
        const size_t rowCount = std::min(rowsPerStrip, image.height - firstRow);
        for (size_t row = 0; row < rowCount; ++row) {
            const size_t invertedRow = image.height - 1u - firstRow - row;
            for (size_t column = 0; column < image.width; ++column) {
                const auto& pixel = image.at(column, invertedRow);
                const size_t index = (row * image.width + column) * 4u;
                stripPixels[index] = pixel.r;
                stripPixels[index + 1u] = pixel.g;
                stripPixels[index + 2u] = pixel.b;
                stripPixels[index + 3u] = pixel.a;
            }
        }
        if (tinydng_writer_write_strip(writer.get(), static_cast<uint32_t>(firstRow / rowsPerStrip),
                                       stripPixels.data(), &error) != TINYDNG_OK) {
            return false;
        }
        emitProgress(0.65f + static_cast<float>(firstRow + rowCount) / static_cast<float>(image.height) * 0.25f);
    }

    emitProgress(0.92f);
    const bool pixelsWritten = tinydng_writer_finish(writer.release(), &error) == TINYDNG_OK;
    if (sink.data.close != nullptr) {
        sink.data.close(&sink.data);
        sink.data = {};
    }
    const auto storedFftSize = resolveStoredFftSize(samples, metadata);
    const TiffMetadata::AudioFields storedMetadata{
        resolveStoredSampleRate(samples, metadata),
        storedFftSize,
        resolveStoredHopSize(metadata, storedFftSize),
        resolveStoredChannels(samples, metadata),
        metadata.analysisLeadingSamples,
        static_cast<std::uint32_t>(metadata.originalSampleCount),
        static_cast<std::uint32_t>(metadata.originalSampleCount >> 32u)
    };
    const bool ok = pixelsWritten && TiffMetadata::write(filepath, storedMetadata);
    emitProgress(ok ? 1.0f : 0.92f);
    return ok;
}

bool loadFromTIFF(const std::string& filepath,
                 std::vector<AudioColourSample>& samples,
                 AudioMetadata& metadata,
                 const std::function<void(float)>& progress,
				 const SequenceFrameCallback& onFrameDecoded,
                 std::string* errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (progress) {
        progress(0.02f);
    }

    tinydng_error error{};
    DngContext context(tinydng_context_create(nullptr, &error), tinydng_context_destroy);
    if (!context) {
        return fail(std::string("Could not initialise TIFF decoder: ") + error.message);
    }
    tinydng_document* rawDocument = nullptr;
    tinydng_open_options openOptions{};
    openOptions.flags = TINYDNG_OPEN_PREFER_MMAP;
    if (tinydng_open_file(context.get(), filepath.c_str(), &openOptions, &rawDocument, &error) != TINYDNG_OK) {
        return fail(std::string("Could not open TIFF: ") + error.message);
    }
    const auto releaseDocument = [&](tinydng_document* document) { tinydng_document_destroy(context.get(), document); };
    std::unique_ptr<tinydng_document, decltype(releaseDocument)> document(rawDocument, releaseDocument);
    const tinydng_image_info* image = tinydng_image_get(document.get(), 0);
    if (image == nullptr || image->width == 0 || image->height == 0 || image->samples_per_pixel < 3 ||
        !((image->sample_format == TINYDNG_SAMPLEFORMAT_IEEEFP && image->bits_per_sample == 32) ||
          (image->sample_format == TINYDNG_SAMPLEFORMAT_UINT &&
           (image->bits_per_sample == 8 || image->bits_per_sample == 16)))) {
        return fail("TIFF must contain a 32-bit float or 8/16-bit RGB image");
    }
    const auto embeddedMetadata = TiffMetadata::read(filepath);
    size_t binCount = 0;
    uint32_t inferredChannels = 1;
    if (!resolveImageLayout(image->height, embeddedMetadata.channels, binCount, inferredChannels) ||
        (embeddedMetadata.channels > 0 && inferredChannels != embeddedMetadata.channels) ||
        (embeddedMetadata.fftSize > 0 && static_cast<size_t>(embeddedMetadata.fftSize / 2u) + 1u != binCount) ||
        embeddedMetadata.fftSize > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        embeddedMetadata.hopSize > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return fail("TIFF dimensions do not match its audio metadata");
    }
    ColourNativeImage colourImage;
    if (image->width > colourImage.pixels.max_size() / image->height) {
        return fail("TIFF image is too large to import");
    }
    colourImage.resize(image->width, image->height);
    static_assert(sizeof(RGBAColour) == sizeof(float) * 4u);
    tinydng_decode_options options{};
    if (image->sample_format == TINYDNG_SAMPLEFORMAT_IEEEFP && image->samples_per_pixel == 4) {
        options.dst = colourImage.pixels.data();
        options.dst_capacity = colourImage.pixels.size() * sizeof(RGBAColour);
    }
    DecodedPixels pixels{context.get()};
    if (tinydng_decode_image(context.get(), document.get(), 0, &options, &pixels.data, &error) != TINYDNG_OK) {
        return fail(std::string("Could not decode TIFF: ") + error.message);
    }
    if (progress) {
        progress(0.06f);
    }

    const size_t bytesPerSample = pixels.data.bits_per_sample / 8u;
    const size_t pixelStride = pixels.data.samples_per_pixel;
    if (pixels.data.width != image->width || pixels.data.height != image->height || pixelStride < 3 ||
        !((pixels.data.sample_format == TINYDNG_SAMPLEFORMAT_IEEEFP && pixels.data.bits_per_sample == 32) ||
          (pixels.data.sample_format == TINYDNG_SAMPLEFORMAT_UINT &&
           (pixels.data.bits_per_sample == 8 || pixels.data.bits_per_sample == 16))) ||
        colourImage.pixels.size() > pixels.data.size / pixelStride / bytesPerSample) {
        return fail("TIFF decoder returned an unsupported pixel layout");
    }
    const auto readSample = [&](size_t index) {
        const uint8_t* data = pixels.data.data + index * bytesPerSample;
        if (pixels.data.sample_format == TINYDNG_SAMPLEFORMAT_IEEEFP) {
            float value;
            std::memcpy(&value, data, sizeof(value));
            return sanitiseFloat(value);
        }
        if (pixels.data.bits_per_sample == 8) {
            return static_cast<float>(*data) / 255.0f;
        }
        uint16_t value;
        std::memcpy(&value, data, sizeof(value));
        return static_cast<float>(value) / 65535.0f;
    };
    for (size_t row = 0; row < colourImage.height; ++row) {
        for (size_t column = 0; column < colourImage.width; ++column) {
            const size_t index = (row * colourImage.width + column) * pixelStride;
            RGBAColour pixel{
                std::clamp(readSample(index), 0.0f, 1.0f),
                std::max(readSample(index + 1u), 0.0f),
                std::clamp(readSample(index + 2u), 0.0f, 1.0f),
                pixelStride > 3 ? std::clamp(readSample(index + 3u), 0.0f, 1.0f) : 0.5f
            };
            if (pixels.data.sample_format == TINYDNG_SAMPLEFORMAT_UINT) {
                pixel.r = std::clamp(srgbToLinear(pixel.r), 0.0f, 1.0f);
                pixel.g = std::clamp(srgbToLinear(pixel.g), 0.0f, 1.0f);
                pixel.b = std::clamp(srgbToLinear(pixel.b), 0.0f, 1.0f);
            }
            colourImage.at(column, row) = pixel;
        }
        if (progress) {
            progress(0.08f + static_cast<float>(row + 1u) / static_cast<float>(colourImage.height) * 0.48f);
        }
    }
    tinydng_pixels_free(context.get(), &pixels.data);
    for (size_t row = 0; row < colourImage.height / 2u; ++row) {
        const size_t invertedRow = colourImage.height - 1u - row;
        for (size_t column = 0; column < colourImage.width; ++column) {
            std::swap(colourImage.at(column, row), colourImage.at(column, invertedRow));
        }
    }

	colourImage.metadata.channels = inferredChannels;

	float detectedSampleRate = embeddedMetadata.sampleRate > 0
		? static_cast<float>(embeddedMetadata.sampleRate)
		: ColourNativeCodec::detectSampleRate(colourImage);

	const int fftSize = embeddedMetadata.fftSize > 0
		? static_cast<int>(embeddedMetadata.fftSize)
		: (binCount > 1 ? static_cast<int>((binCount - 1) * 2) : 2);
	int hopSize = embeddedMetadata.hopSize > 0
		? static_cast<int>(embeddedMetadata.hopSize)
		: std::max(1, fftSize / 2);

    colourImage.metadata.analysisLeadingSamples = embeddedMetadata.analysisLeadingSamples;
    colourImage.metadata.originalSampleCount = static_cast<std::uint64_t>(embeddedMetadata.originalSampleCountLow) |
        (static_cast<std::uint64_t>(embeddedMetadata.originalSampleCountHigh) << 32u);
	colourImage.metadata.sampleRate = detectedSampleRate;
	colourImage.metadata.fftSize = fftSize;
	colourImage.metadata.hopSize = hopSize;
	colourImage.metadata.numFrames = colourImage.width;
	colourImage.metadata.numBins = binCount;
	colourImage.metadata.channels = inferredChannels;
	colourImage.metadata.windowType = "hann";
	colourImage.metadata.version = "3.0.0";

    metadata = colourImage.metadata;
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
	if (samples.empty()) {
		return fail("TIFF contains no decodable audio frames");
	}
	ReSyne::LoudnessUtils::calculateLoudnessFromSpectralFrames(samples, colourImage.metadata);

    colourImage.metadata.analysisLeadingSamples = embeddedMetadata.analysisLeadingSamples;
    colourImage.metadata.originalSampleCount = static_cast<std::uint64_t>(embeddedMetadata.originalSampleCountLow) |
        (static_cast<std::uint64_t>(embeddedMetadata.originalSampleCountHigh) << 32u);
	colourImage.metadata.sampleRate = detectedSampleRate;
	colourImage.metadata.hopSize = hopSize;

	if (progress) {
		progress(0.96f);
	}

	const float finalSampleRate = samples.empty()
		? detectedSampleRate
		: samples.front().sampleRate;

	metadata.analysisLeadingSamples = colourImage.metadata.analysisLeadingSamples;
    metadata.originalSampleCount = colourImage.metadata.originalSampleCount;
    metadata.durationSeconds = finalSampleRate > 0.0f ? metadata.originalSampleCount / static_cast<double>(finalSampleRate) : 0.0;
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
