#include "resyne/encoding/spectral/colour_native_codec.h"

#include "colour/colour_core.h"
#include "resyne/encoding/reconstruction/pghi.h"
#include "constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>
#include <vector>
#include <map>

namespace {
constexpr float DB_MIN = -140.0f;
constexpr float DB_MAX = 40.0f;
constexpr float DB_RANGE = DB_MAX - DB_MIN;

constexpr float EPSILON = 1e-6f;
constexpr float DEFAULT_SAMPLE_RATE = 44100.0f;
constexpr float MIN_SAMPLE_RATE = 8000.0f;
constexpr float MAX_SAMPLE_RATE = 384000.0f;
constexpr float MIN_BIN_INTENSITY = 1e-12f;
constexpr std::array<size_t, 6> COMMON_BIN_COUNTS = {257, 513, 1025, 2049, 4097, 8193};

bool isCommonBinCount(const size_t binCount) {
	return std::find(COMMON_BIN_COUNTS.begin(), COMMON_BIN_COUNTS.end(), binCount) !=
		COMMON_BIN_COUNTS.end();
}

float snapToCommonSampleRate(float sampleRate) {
	if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
		return sampleRate;
	}

	constexpr std::array<float, 12> COMMON_SAMPLE_RATES = {
		8000.0f, 11025.0f, 16000.0f, 22050.0f, 32000.0f, 44100.0f,
		48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f, 384000.0f
	};

	for (float nominal : COMMON_SAMPLE_RATES) {
		const float tolerance = std::max(1.0f, nominal * 0.0005f);
		if (std::fabs(sampleRate - nominal) <= tolerance) {
			return nominal;
		}
	}

	return sampleRate;
}

const float LOG_FREQ_MIN = std::log2(synesthesia::constants::MIN_AUDIO_FREQ);
const float LOG_FREQ_MAX = std::log2(synesthesia::constants::MAX_AUDIO_FREQ);
const float LOG_FREQ_RANGE = LOG_FREQ_MAX - LOG_FREQ_MIN;
constexpr float UNIT_SCALE = 0.5f;
constexpr float UNIT_OFFSET = 0.5f;
constexpr float MIN_PHASE_VECTOR = 1e-4f;

float computeRobustMedian(std::vector<float>& values) {
	if (values.empty()) {
		return 0.0f;
	}

	const size_t mid = values.size() / 2;
	using Diff = std::vector<float>::difference_type;
	std::nth_element(values.begin(), values.begin() + static_cast<Diff>(mid), values.end());
	float median = values[mid];

	if (values.size() % 2 == 0) {
		const Diff lowerIndex = static_cast<Diff>(mid) - 1;
		std::nth_element(values.begin(), values.begin() + lowerIndex, values.end());
		median = 0.5f * (median + values[mid - 1]);
	}

	return median;
}

}

ColourNativeImage ColourNativeCodec::encode(const std::vector<AudioColourSample>& samples,
										  const AudioMetadata& metadata,
										  const std::function<void(float)>& onProgress) {
	ColourNativeImage image;
	const size_t numFrames = samples.size();
	const uint32_t numChannels = metadata.channels > 0 ? metadata.channels :
		(!samples.empty() ? samples.front().channels : 1);
	const size_t numBinsPerChannel = metadata.numBins != 0
		? metadata.numBins
		: (samples.empty() || samples.front().magnitudes.empty() ? 0 : samples.front().magnitudes[0].size());

	const size_t totalHeight = numBinsPerChannel * numChannels;

	image.resize(numFrames, totalHeight);
	image.metadata = metadata;
	image.metadata.numFrames = numFrames;
	image.metadata.numBins = numBinsPerChannel;
	image.metadata.channels = numChannels;

	if (onProgress) {
		onProgress(numFrames == 0 ? 1.0f : 0.0f);
	}

	for (size_t frame = 0; frame < numFrames; ++frame) {
		const auto& sample = samples[frame];
		const float frameSampleRate = sample.sampleRate > 0.0f
			? sample.sampleRate
			: metadata.sampleRate;

		for (uint32_t ch = 0; ch < numChannels; ++ch) {
			if (ch >= sample.magnitudes.size() || ch >= sample.phases.size()) {
				continue;
			}

			std::vector<RGBAColour> column;
			const std::vector<float>& rawFrequencies = (ch < sample.frequencies.size()) ? sample.frequencies[ch] : std::vector<float>();
			encodeTimeFrame(sample.magnitudes[ch], sample.phases[ch], rawFrequencies, frameSampleRate, column);

			const size_t yOffset = ch * numBinsPerChannel;
			for (size_t bin = 0; bin < numBinsPerChannel && bin < column.size(); ++bin) {
				image.at(frame, yOffset + bin) = column[bin];
			}
		}

		if (onProgress && numFrames > 0) {
			const float progress = static_cast<float>(frame + 1) /
				static_cast<float>(numFrames);
			onProgress(progress);
		}
	}

	return image;
}

float ColourNativeCodec::detectSampleRate(const ColourNativeImage& image) {
    if (image.height == 0 || image.width == 0 ||
        image.width > std::numeric_limits<size_t>::max() / image.height ||
        image.pixels.size() != image.width * image.height) return DEFAULT_SAMPLE_RATE;
	size_t binCountPerChannel = 0;

	const uint32_t metadataChannels = image.metadata.channels;
	if (metadataChannels > 0 && metadataChannels <= image.height &&
		image.height % metadataChannels == 0) {
		const size_t candidateBinCount = image.height / metadataChannels;
		if (candidateBinCount > 0 && candidateBinCount <= MAX_BIN_COUNT &&
			(metadataChannels > 1 || isCommonBinCount(candidateBinCount))) {
			binCountPerChannel = candidateBinCount;
		}
	}

	for (size_t candidateBinCount : COMMON_BIN_COUNTS) {
		if (binCountPerChannel != 0) {
			break;
		}
		if (candidateBinCount > MAX_BIN_COUNT || candidateBinCount > image.height) {
			continue;
		}
		if (image.height % candidateBinCount == 0) {
			const uint32_t candidateChannels = static_cast<uint32_t>(image.height / candidateBinCount);
			if (candidateChannels >= 1 && candidateChannels <= 8) {
				binCountPerChannel = candidateBinCount;
				break;
			}
		}
	}

	if (binCountPerChannel == 0) {
		if (image.height == 0 || image.height > MAX_BIN_COUNT) {
			return DEFAULT_SAMPLE_RATE;
		}
		binCountPerChannel = image.height;
	}

	const size_t usableHeight = binCountPerChannel;
	if (usableHeight <= 1) {
		return DEFAULT_SAMPLE_RATE;
	}

	std::vector<float> candidates;
	candidates.reserve(image.width * (usableHeight - 1));

	const float fftSizeF = 2.0f * static_cast<float>(usableHeight - 1);

	const size_t frameStep = std::max<size_t>(1, image.width / 512);

	auto processFrame = [&](size_t frameIndex) {
		for (size_t bin = 1; bin < usableHeight; ++bin) {
			const RGBAColour& pixel = image.at(frameIndex, bin);
			const float magnitudeEncoded = std::clamp(pixel.r, 0.0f, 1.0f);
			const float magnitude = denormaliseMagnitude(magnitudeEncoded);
			if (!std::isfinite(magnitude) || magnitude <= MIN_BIN_INTENSITY) {
				continue;
			}

			const float frequency = denormaliseLogFrequency(pixel.g);
			if (!std::isfinite(frequency) || frequency <= 0.0f) {
				continue;
			}

			const float candidate = frequency * fftSizeF / static_cast<float>(bin);
			if (!std::isfinite(candidate) || candidate < MIN_SAMPLE_RATE * 0.998f || candidate > MAX_SAMPLE_RATE * 1.002f) {
				continue;
			}

			candidates.push_back(candidate);
		}
	};

	for (size_t frame = 0; frame < image.width; frame += frameStep) {
		processFrame(frame);
	}

	if (image.width > 0) {
		const size_t lastFrame = image.width - 1;
		if ((lastFrame % frameStep) != 0) {
			processFrame(lastFrame);
		}
	}

	if (candidates.empty()) {
		return DEFAULT_SAMPLE_RATE;
	}

	struct RateBucket {
		int count = 0;
		double sum = 0.0;
		double sumSq = 0.0;
		float commonRate = 0.0f;
	};

	std::map<int, RateBucket> buckets;
	int totalVotes = 0;

	constexpr std::array<float, 12> COMMON_RATES = {
		8000.0f, 11025.0f, 16000.0f, 22050.0f, 32000.0f, 44100.0f,
		48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f, 384000.0f
	};

	for (float rate : candidates) {
		for (float common : COMMON_RATES) {
			if (std::abs(rate - common) < common * 0.002f) {
				int key = static_cast<int>(common);
				RateBucket& b = buckets[key];
				b.count++;
				b.sum += static_cast<double>(rate);
				b.sumSq += (double)rate * (double)rate;
				b.commonRate = common;
				totalVotes++;
				break;
			}
		}
	}

	float bestRate = 0.0f;
	double minStdDev = std::numeric_limits<double>::max();
	const int minVotesRequired = std::max(10, totalVotes / 100);

	for (const auto& [key, bucket] : buckets) {
		if (bucket.count < minVotesRequired) continue;

		double mean = bucket.sum / bucket.count;
		double variance = (bucket.sumSq / bucket.count) - (mean * mean);
		double stdDev = std::sqrt(std::max(0.0, variance));

		bool isBetter = false;

		if (bestRate == 0.0f) {
			isBetter = true;
		} else {
			if (stdDev < minStdDev) {
				isBetter = true;
			}
		}

		if (isBetter) {
			minStdDev = stdDev;
			bestRate = bucket.commonRate;
		}
	}

	if (bestRate > 0.0f) {
		return std::clamp(bestRate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
	}

	float sampleRate = computeRobustMedian(candidates);
	if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
		return DEFAULT_SAMPLE_RATE;
	}

	sampleRate = snapToCommonSampleRate(sampleRate);
	return std::clamp(sampleRate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
}

std::vector<AudioColourSample> ColourNativeCodec::decode(const ColourNativeImage& image,
														float& sampleRate,
														int& hopSize,
														const SequenceFrameCallback& onFrameDecoded,
														const std::function<void(float)>& onProgress) {
	const uint32_t numChannels = image.metadata.channels > 0 ? image.metadata.channels : 1;
	const bool invalidLayout = numChannels == 0 || numChannels > 8 || image.height == 0 ||
		numChannels > image.height || (image.height % numChannels) != 0 ||
        image.width > std::numeric_limits<size_t>::max() / std::max<size_t>(1, image.height) ||
        image.pixels.size() != image.width * image.height;
	const size_t binCount = invalidLayout ? 0 : (image.height / numChannels);

	std::vector<AudioColourSample> samples;
	const size_t totalFrames = image.width;
	if (binCount < 2 || binCount > ColourNativeCodec::MAX_BIN_COUNT || totalFrames == 0) {
		if (onProgress) {
			onProgress(1.0f);
		}
		return samples;
	}

	samples.resize(image.width);
    const size_t fftSize = binCount > 1 ? (binCount - 1) * 2 : 2;

	if (!std::isfinite(sampleRate) || sampleRate <= EPSILON) {
		sampleRate = DEFAULT_SAMPLE_RATE;
	}

	if (hopSize <= 0) {
		hopSize = std::max<int>(1, static_cast<int>(fftSize / 2));
	}

	std::vector<std::vector<std::vector<float>>> allChannelsMagnitudes(numChannels);
	std::vector<std::vector<std::vector<float>>> allChannelsRawPhases(numChannels);
	std::vector<std::vector<std::vector<float>>> allChannelsFrequencies(numChannels);

	for (uint32_t ch = 0; ch < numChannels; ++ch) {
		allChannelsMagnitudes[ch].resize(totalFrames);
		allChannelsRawPhases[ch].resize(totalFrames);
		allChannelsFrequencies[ch].resize(totalFrames);
	}

	if (onProgress) {
		onProgress(0.0f);
	}

	const size_t numThreads = std::max<size_t>(
		1,
		std::min<size_t>(static_cast<size_t>(std::thread::hardware_concurrency()), 8));
	const size_t framesPerThread = (totalFrames + numThreads - 1) / numThreads;
	std::vector<std::thread> pixelThreads;
	std::atomic<size_t> framesProcessed{0};

	for (size_t t = 0; t < numThreads; ++t) {
		pixelThreads.emplace_back([&, t]() {
			const size_t frameStart = t * framesPerThread;
			const size_t frameEnd = std::min(frameStart + framesPerThread, totalFrames);

			for (size_t frame = frameStart; frame < frameEnd; ++frame) {
				for (uint32_t ch = 0; ch < numChannels; ++ch) {
					const size_t yOffset = ch * binCount;
					std::vector<RGBAColour> column(binCount);
					for (size_t bin = 0; bin < binCount; ++bin) {
						column[bin] = image.at(frame, yOffset + bin);
					}

					std::vector<float> magnitudesFrame, rawPhasesFrame, frequenciesFrame;
					decodeTimeFrame(column, sampleRate, magnitudesFrame, rawPhasesFrame, frequenciesFrame);

					allChannelsMagnitudes[ch][frame] = std::move(magnitudesFrame);
					allChannelsRawPhases[ch][frame] = std::move(rawPhasesFrame);
					allChannelsFrequencies[ch][frame] = std::move(frequenciesFrame);
				}

				const size_t completed = framesProcessed.fetch_add(1, std::memory_order_relaxed) + 1;
				if (onProgress && (completed % 100 == 0)) {
					onProgress(0.15f * static_cast<float>(completed) / static_cast<float>(totalFrames));
				}
			}
		});
	}

	for (auto& thread : pixelThreads) {
		thread.join();
	}

    const size_t callbackStride = std::max<size_t>(1, totalFrames / 200);
    std::vector<std::vector<std::vector<float>>> allChannelsReconstructedPhases(numChannels);
    std::vector<std::thread> channelThreads;
    std::atomic<uint32_t> channelsCompleted{0};

    auto processChannel = [&](uint32_t ch) {
        std::vector<std::vector<bool>> knownPhase(totalFrames, std::vector<bool>(binCount, false));
        for (size_t frame = 0; frame < totalFrames; ++frame) {
            for (size_t bin = 0; bin < binCount; ++bin) {
                const auto& pixel = image.at(frame, static_cast<size_t>(ch) * binCount + bin);
                const float cosine = 2.0f * pixel.b - 1.0f;
                const float sine = 2.0f * pixel.a - 1.0f;
                const float radius = std::hypot(cosine, sine);
                knownPhase[frame][bin] = std::isfinite(pixel.b) && std::isfinite(pixel.a) &&
                    pixel.b >= 0.0f && pixel.b <= 1.0f && pixel.a >= 0.0f && pixel.a <= 1.0f &&
                    std::abs(radius - 1.0f) <= 0.02f;
            }
        }
        allChannelsReconstructedPhases[ch] = PhaseReconstruction::reconstructPhasePGHI(
            allChannelsMagnitudes[ch], allChannelsRawPhases[ch], knownPhase, fftSize, hopSize);
        const uint32_t completed = channelsCompleted.fetch_add(1, std::memory_order_relaxed) + 1;
        if (onProgress) onProgress(0.15f + 0.7f * static_cast<float>(completed) / static_cast<float>(numChannels));
    };

	if (numChannels > 1) {
		channelThreads.reserve(numChannels);
		for (uint32_t ch = 0; ch < numChannels; ++ch) {
			channelThreads.emplace_back(processChannel, ch);
		}
		for (auto& thread : channelThreads) {
			thread.join();
		}
	} else {
		processChannel(0);
	}

	for (size_t frame = 0; frame < totalFrames; ++frame) {
		AudioColourSample sample;
		sample.magnitudes.resize(numChannels);
		sample.phases.resize(numChannels);
		sample.frequencies.resize(numChannels);
		sample.channels = numChannels;

		for (uint32_t ch = 0; ch < numChannels; ++ch) {
			sample.magnitudes[ch] = allChannelsMagnitudes[ch][frame];
			sample.phases[ch] = allChannelsReconstructedPhases[ch][frame];
			sample.frequencies[ch] = allChannelsFrequencies[ch][frame];
		}

		sample.sampleRate = sampleRate;
		sample.timestamp = (sampleRate > EPSILON && hopSize > 0)
			? (static_cast<double>(frame) * hopSize + fftSize * .5 - image.metadata.analysisLeadingSamples) /
				static_cast<double>(sampleRate)
			: 0.0;

		samples[frame] = sample;

		if (onFrameDecoded && ((frame + 1) % callbackStride == 0 || frame + 1 == totalFrames)) {
			onFrameDecoded(samples, frame + 1);
		}
	}

	if (onProgress) {
		onProgress(1.0f);
	}

	return samples;
}

void ColourNativeCodec::encodeTimeFrame(const std::vector<float>& magnitudes,
									   const std::vector<float>& phases,
									   const std::vector<float>& frequencies,
									   const float sampleRate,
									   std::vector<RGBAColour>& column) {
	const size_t numBins = std::min(magnitudes.size(), phases.size());
	column.assign(numBins, {0.0f, 0.0f, 0.5f, 0.5f});

	if (numBins == 0) {
		return;
	}

	const size_t fftSize = numBins > 1 ? (numBins - 1) * 2 : 2;
	const float fftSizeF = static_cast<float>(fftSize);
	const float freqResolution = (sampleRate > 0.0f && fftSizeF > 0.0f)
		? sampleRate / fftSizeF
		: 0.0f;

	for (size_t bin = 0; bin < numBins; ++bin) {
		const float magnitude = magnitudes[bin];
		const float phase = phases[bin];

		float frequency = synesthesia::constants::MIN_AUDIO_FREQ;
		if (bin < frequencies.size() && frequencies[bin] > 0.0f) {
			frequency = frequencies[bin];
		} else {
			frequency = (freqResolution > 0.0f)
				? std::min(freqResolution * static_cast<float>(bin), sampleRate * 0.5f)
				: synesthesia::constants::MIN_AUDIO_FREQ;
		}

		column[bin] = encodeFrequencyBin(frequency, magnitude, phase);
	}
}

void ColourNativeCodec::decodeTimeFrame(const std::vector<RGBAColour>& column,
									   const float sampleRate,
									   std::vector<float>& magnitudes,
									   std::vector<float>& phases,
									   std::vector<float>& frequencies) {
	const size_t numBins = column.size();
	magnitudes.assign(numBins, 0.0f);
	phases.assign(numBins, 0.0f);
	frequencies.assign(numBins, 0.0f);

	if (numBins == 0) {
		return;
	}

	const size_t fftSize = numBins > 1 ? (numBins - 1) * 2 : 2;
	const float fftSizeF = static_cast<float>(fftSize);
	const float freqResolution = (sampleRate > 0.0f && fftSizeF > 0.0f)
		? sampleRate / fftSizeF
		: 0.0f;

	for (size_t bin = 0; bin < numBins; ++bin) {
		const RGBAColour& pixel = column[bin];
		const float magnitude = denormaliseMagnitude(std::clamp(pixel.r, 0.0f, 1.0f));
		if (!std::isfinite(magnitude) || magnitude <= MIN_BIN_INTENSITY) {
			frequencies[bin] = freqResolution > 0.0f
				? freqResolution * static_cast<float>(bin)
				: 0.0f;
			continue;
		}

		magnitudes[bin] = magnitude;
		bool phaseValid = false;
		const float phase = decodePhaseVector(pixel.b, pixel.a, phaseValid);
		phases[bin] = phaseValid ? phase : 0.0f;

		float resolvedFrequency = denormaliseLogFrequency(pixel.g);
		if (!std::isfinite(resolvedFrequency) || resolvedFrequency <= 0.0f) {
			resolvedFrequency = freqResolution > 0.0f
				? freqResolution * static_cast<float>(bin)
				: 0.0f;
		}
		if (bin == 0) {
            resolvedFrequency = freqResolution * static_cast<float>(bin);
        }
		frequencies[bin] = resolvedFrequency;
	}
}

RGBAColour ColourNativeCodec::encodeFrequencyBin(float frequency,
                                                float magnitude,
                                                const float phase) {
	const float magnitudeNormalised = normaliseMagnitude(magnitude);
	const float frequencyNormalised = normaliseLogFrequency(frequency);
	const auto encodedPhase = encodePhaseVector(phase);

	RGBAColour result{};
	result.r = magnitudeNormalised;
	result.g = frequencyNormalised;
	result.b = encodedPhase.first;
	result.a = encodedPhase.second;
	return result;
}

float ColourNativeCodec::normaliseMagnitude(const float magnitude) {
	if (!std::isfinite(magnitude) || magnitude <= 0.0f) {
		return 0.0f;
	}

	const float db = 20.0f * std::log10(magnitude);
	const float normalised = (db - DB_MIN) / DB_RANGE;
	return std::clamp(normalised, 0.0f, 1.0f);
}

float ColourNativeCodec::denormaliseMagnitude(const float normalised) {
    if (!std::isfinite(normalised) || normalised <= 0.0f) return 0.0f;
	const float clamped = std::clamp(normalised, 0.0f, 1.0f);
	const float db = clamped * DB_RANGE + DB_MIN;
	return std::pow(10.0f, db / 20.0f);
}

float ColourNativeCodec::normaliseLogFrequency(const float frequency) {
	if (!std::isfinite(frequency) || frequency <= 0.0f) {
		return 0.0f;
	}
	const float logValue = std::log2(frequency);
	if (LOG_FREQ_RANGE <= EPSILON) {
		return 0.0f;
	}
	const float normalised = (logValue - LOG_FREQ_MIN) / LOG_FREQ_RANGE;
	return normalised;
}

float ColourNativeCodec::denormaliseLogFrequency(const float normalised) {
	if (LOG_FREQ_RANGE <= EPSILON) {
		return synesthesia::constants::MIN_AUDIO_FREQ;
	}
	if (!std::isfinite(normalised)) return 0.0f;
    const float logValue = LOG_FREQ_MIN + normalised * LOG_FREQ_RANGE;
	const float maximumLogValue = std::log2(std::numeric_limits<float>::max()) - 1.0f;
	return std::pow(2.0f, std::min(logValue, maximumLogValue));
}

std::pair<float, float> ColourNativeCodec::encodePhaseVector(const float phase) {
	if (!std::isfinite(phase)) {
		return {UNIT_OFFSET, UNIT_OFFSET};
	}

	const float cosine = std::cos(phase);
	const float sine = std::sin(phase);
	return {cosine * UNIT_SCALE + UNIT_OFFSET, sine * UNIT_SCALE + UNIT_OFFSET};
}

float ColourNativeCodec::decodePhaseVector(const float encodedCosine,
										  const float encodedSine,
										  bool& valid) {
	const float cosine = (std::clamp(encodedCosine, 0.0f, 1.0f) - UNIT_OFFSET) / UNIT_SCALE;
	const float sine = (std::clamp(encodedSine, 0.0f, 1.0f) - UNIT_OFFSET) / UNIT_SCALE;
	const float length = std::sqrt(cosine * cosine + sine * sine);

	if (!(length > MIN_PHASE_VECTOR)) {
		valid = false;
		return 0.0f;
	}

	const float normalisedCosine = cosine / length;
	const float normalisedSine = sine / length;
	valid = true;
	return std::atan2(normalisedSine, normalisedCosine);
}
