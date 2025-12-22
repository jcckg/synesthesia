#include "resyne/encoding/spectral/colour_native_codec.h"

#include "colour/colour_mapper.h"
#include "resyne/encoding/reconstruction/phase_wrapping.h"
#include "resyne/encoding/reconstruction/transient_detection.h"
#include "resyne/encoding/reconstruction/damage_detection.h"
#include "resyne/encoding/reconstruction/phase_locking.h"
#include "resyne/encoding/reconstruction/phase_vocoder.h"
#include "resyne/encoding/reconstruction/pghi.h"
#include "resyne/encoding/reconstruction/phase_smoothing.h"
#include "resyne/recorder/loudness_utils.h"
#include "constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>
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
constexpr float MAX_SAMPLE_RATE = 192000.0f;
constexpr float MIN_BIN_INTENSITY = 1e-6f;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;

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

const float LOG_FREQ_MIN = std::log2(ColourMapper::MIN_FREQ);
const float LOG_FREQ_MAX = std::log2(ColourMapper::MAX_FREQ);
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
		const float hopRatio = (metadata.fftSize > 0)
			? static_cast<float>(metadata.hopSize) / static_cast<float>(metadata.fftSize)
			: 0.5f;

		for (uint32_t ch = 0; ch < numChannels; ++ch) {
			if (ch >= sample.magnitudes.size() || ch >= sample.phases.size()) {
				continue;
			}

			std::vector<RGBAColour> column;
			encodeTimeFrame(sample.magnitudes[ch], sample.phases[ch], frameSampleRate, hopRatio, column);

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
	const std::vector<size_t> commonBinCounts = {257, 513, 1025, 2049, 4097, 8193};

	size_t binCountPerChannel = 0;

	for (size_t candidateBinCount : commonBinCounts) {
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
		binCountPerChannel = std::min(image.height, MAX_BIN_COUNT);
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

			const float frequencyNormalised = std::clamp(pixel.g, 0.0f, 1.0f);
			const float frequency = denormaliseLogFrequency(frequencyNormalised);
			if (!std::isfinite(frequency) || frequency <= 0.0f) {
				continue;
			}

			const float candidate = frequency * fftSizeF / static_cast<float>(bin);
			if (!std::isfinite(candidate) || candidate < MIN_SAMPLE_RATE || candidate > MAX_SAMPLE_RATE) {
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
	const size_t totalHeight = std::min(image.height, ColourNativeCodec::MAX_BIN_COUNT);
	const size_t binCountPerChannel = totalHeight / numChannels;
	const size_t binCount = binCountPerChannel > 0 ? binCountPerChannel : totalHeight;

	std::vector<AudioColourSample> samples(image.width);
	const size_t totalFrames = image.width;
	if (binCount == 0 || totalFrames == 0) {
		if (onProgress) {
			onProgress(1.0f);
		}
		return samples;
	}

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

	const size_t numThreads = std::min(std::thread::hardware_concurrency(), static_cast<unsigned>(8));
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

	std::vector<std::vector<float>> allChannelsSpectralFlux(numChannels);
	for (uint32_t ch = 0; ch < numChannels; ++ch) {
		const auto& channelMagnitudes = allChannelsMagnitudes[ch];
		allChannelsSpectralFlux[ch].resize(totalFrames, 0.0f);
		for (size_t frame = 1; frame < totalFrames; ++frame) {
			allChannelsSpectralFlux[ch][frame] = PhaseReconstruction::computeSpectralFlux(
				channelMagnitudes[frame], channelMagnitudes[frame - 1]);
		}
	}

	constexpr size_t TRANSIENT_WINDOW_RADIUS = 50;

	const size_t callbackStride = std::max<size_t>(1, totalFrames / 200);
	const float freqResolution = fftSize > 0 ? sampleRate / static_cast<float>(fftSize) : 0.0f;
	constexpr size_t TRANSITION_RADIUS = 3;

	constexpr float ADAPTIVE_DAMPING_FACTOR = 0.95f;
	constexpr float DISCONTINUITY_THRESHOLD = std::numbers::pi_v<float> / 2.0f;

	std::vector<std::vector<std::vector<float>>> allChannelsReconstructedPhases(numChannels);
	std::vector<std::thread> channelThreads;
	std::atomic<uint32_t> channelsCompleted{0};

	auto processChannel = [&](uint32_t ch) {
		allChannelsReconstructedPhases[ch].resize(totalFrames);

		const auto& channelMagnitudes = allChannelsMagnitudes[ch];
		const auto& channelRawPhases = allChannelsRawPhases[ch];
		const auto& channelFrequencies = allChannelsFrequencies[ch];
		const auto& channelSpectralFlux = allChannelsSpectralFlux[ch];

		std::vector<float> prevDecodedPhase(binCount, 0.0f);
		std::vector<float> prevOutputPhase(binCount, 0.0f);
		std::vector<bool> phaseInitialised(binCount, false);
		std::vector<int> silenceFrames(binCount, 0);

		for (size_t frame = 0; frame < totalFrames; ++frame) {
			const size_t windowStart = frame >= TRANSIENT_WINDOW_RADIUS ? frame - TRANSIENT_WINDOW_RADIUS : 0;
			const size_t windowEnd = std::min(frame + TRANSIENT_WINDOW_RADIUS + 1, totalFrames);

			float localMean = 0.0f;
			size_t windowSize = 0;
			for (size_t i = windowStart; i < windowEnd; ++i) {
				localMean += channelSpectralFlux[i];
				++windowSize;
			}
			if (windowSize > 0) {
				localMean /= static_cast<float>(windowSize);
			}

			float localStdDev = 0.0f;
			for (size_t i = windowStart; i < windowEnd; ++i) {
				const float diff = channelSpectralFlux[i] - localMean;
				localStdDev += diff * diff;
			}
			if (windowSize > 0) {
				localStdDev = std::sqrt(localStdDev / static_cast<float>(windowSize));
			}

			const float localTransientThreshold = localMean + 1.5f * localStdDev;
			const bool isTransient = channelSpectralFlux[frame] > localTransientThreshold;

			const std::vector<float>& magnitudesFrame = channelMagnitudes[frame];
			const std::vector<float>& rawPhasesFrame = channelRawPhases[frame];
			const std::vector<float>& frequenciesFrame = channelFrequencies[frame];
			std::vector<float> adjustedPhases(binCount, 0.0f);

			const std::vector<bool> damagedMask = PhaseReconstruction::detectDamagedBins(channelMagnitudes, frame);
			const std::vector<float> damageWeights = PhaseReconstruction::computeDamageBlend(damagedMask, TRANSITION_RADIUS);
			const bool hasDamagedBins = std::any_of(damagedMask.begin(), damagedMask.end(),
				[](bool value) { return value; });
			const bool hasBlendRegions = std::any_of(damageWeights.begin(), damageWeights.end(),
				[](float weight) { return weight > 0.0f; });

			std::vector<float> reconstructedPhases(binCount, 0.0f);
			if (hasBlendRegions) {
				PhaseReconstruction::reconstructPhasePGHI(channelMagnitudes, channelFrequencies, frame, reconstructedPhases, sampleRate, hopSize, &prevOutputPhase);
				PhaseReconstruction::alignReconstructedPhase(reconstructedPhases, prevOutputPhase, frequenciesFrame, damageWeights, sampleRate, hopSize);

				const auto peaks = PhaseReconstruction::findSpectralPeaks(magnitudesFrame, MIN_BIN_INTENSITY * 5.0f);
				PhaseReconstruction::applyPhaseLocking(reconstructedPhases, magnitudesFrame, peaks, damageWeights);
				PhaseReconstruction::smoothPhase(reconstructedPhases, magnitudesFrame, 3);
			}

			for (size_t bin = 0; bin < binCount; ++bin) {
				const float magnitude = magnitudesFrame[bin];
				const float decodedPhase = PhaseReconstruction::wrapToPi(rawPhasesFrame[bin]);
				const float frequency = frequenciesFrame[bin] > 0.0f
					? frequenciesFrame[bin]
					: freqResolution * static_cast<float>(bin);

				const float expectedAdvance = (sampleRate > EPSILON && hopSize > 0)
					? TWO_PI * frequency * static_cast<float>(hopSize) / sampleRate
					: 0.0f;

				const bool binActive = magnitude > MIN_BIN_INTENSITY;
				silenceFrames[bin] = binActive ? 0 : std::min(silenceFrames[bin] + 1, 64);

				float vocoderPhase = prevOutputPhase[bin];

			if (!phaseInitialised[bin]) {
					if (binActive) {
						vocoderPhase = decodedPhase;
						phaseInitialised[bin] = true;
					} else {
						vocoderPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + expectedAdvance);
					}
				} else if (binActive) {
					const float binDamageWeight = hasBlendRegions ? damageWeights[bin] : 0.0f;
					const bool isEditedBin = binDamageWeight >= 0.01f;
					
					if (!isEditedBin) {
						const float phaseError = PhaseReconstruction::wrapToPi(
							decodedPhase - prevDecodedPhase[bin] - expectedAdvance);
						const float correctedAdvance = expectedAdvance + phaseError;
						vocoderPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + correctedAdvance);
					} else if (isTransient) {
						vocoderPhase = decodedPhase;
					} else {
						const float phaseDeviation = std::abs(PhaseReconstruction::wrapToPi(
							decodedPhase - prevDecodedPhase[bin] - expectedAdvance));

						if (phaseDeviation > DISCONTINUITY_THRESHOLD) {
							vocoderPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + expectedAdvance);
						} else if (phaseDeviation > 0.6f) {
							vocoderPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + expectedAdvance);
						} else {
							const float binDamageWeight = hasBlendRegions ? damageWeights[bin] : 0.0f;
							const float phaseError = PhaseReconstruction::wrapToPi(
								decodedPhase - prevDecodedPhase[bin] - expectedAdvance);
							const float correctedAdvance = expectedAdvance + phaseError;
							const float prediction = prevOutputPhase[bin] + correctedAdvance;
							
							const float driftError = PhaseReconstruction::wrapToPi(decodedPhase - prediction);
							const float dampingScale = 1.0f - ADAPTIVE_DAMPING_FACTOR * (1.0f - binDamageWeight);
							vocoderPhase = PhaseReconstruction::wrapToPi(prediction + dampingScale * driftError);
						}
					}
				} else {
					vocoderPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + expectedAdvance);
				}

				const float weight = hasBlendRegions ? std::clamp(damageWeights[bin], 0.0f, 1.0f) : 0.0f;
				float finalPhase = vocoderPhase;

				if (hasDamagedBins && weight > 0.0f) {
					const float reconPhase = reconstructedPhases[bin];
					const float phaseOffset = PhaseReconstruction::wrapToPi(reconPhase - vocoderPhase);
					const float blendedPhase = PhaseReconstruction::wrapToPi(vocoderPhase + weight * phaseOffset);
					const float smoothing = 0.4f + 0.6f * weight;
					const float delta = PhaseReconstruction::wrapToPi(blendedPhase - prevOutputPhase[bin]);
					finalPhase = PhaseReconstruction::wrapToPi(prevOutputPhase[bin] + smoothing * delta);
				}

				adjustedPhases[bin] = finalPhase;

				if (binActive) {
					prevDecodedPhase[bin] = decodedPhase;
				}

				prevOutputPhase[bin] = finalPhase;
			}

			allChannelsReconstructedPhases[ch][frame] = std::move(adjustedPhases);
		}

		channelsCompleted.fetch_add(1, std::memory_order_relaxed);
		if (onProgress) {
			const float channelProgress = 0.15f + 0.7f * (static_cast<float>(channelsCompleted.load(std::memory_order_relaxed)) / static_cast<float>(numChannels));
			onProgress(channelProgress);
		}
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
			? static_cast<double>(frame * static_cast<size_t>(hopSize)) /
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
									   const float sampleRate,
									   const float hopRatio,
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

		const float frequency = (freqResolution > 0.0f)
			? std::min(freqResolution * static_cast<float>(bin), sampleRate * 0.5f)
			: ColourMapper::MIN_FREQ;

		column[bin] = encodeFrequencyBin(frequency, magnitude, phase);
	}

	(void)hopRatio;
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

		float resolvedFrequency = denormaliseLogFrequency(std::clamp(pixel.g, 0.0f, 1.0f));
		if (!std::isfinite(resolvedFrequency) || resolvedFrequency <= 0.0f) {
			resolvedFrequency = freqResolution > 0.0f
				? freqResolution * static_cast<float>(bin)
				: 0.0f;
		}
		if (bin == 0) {
			resolvedFrequency = 0.0f;
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

	const float db = 20.0f * std::log10(std::max(magnitude, EPSILON));
	const float normalised = (db - DB_MIN) / DB_RANGE;
	return std::clamp(normalised, 0.0f, 1.0f);
}

float ColourNativeCodec::denormaliseMagnitude(const float normalised) {
	const float clamped = std::clamp(normalised, 0.0f, 1.0f);
	const float db = clamped * DB_RANGE + DB_MIN;
	return std::pow(10.0f, db / 20.0f);
}

float ColourNativeCodec::normaliseLogFrequency(const float frequency) {
	if (!std::isfinite(frequency) || frequency <= 0.0f) {
		return 0.0f;
	}
	const float clamped = std::clamp(frequency, ColourMapper::MIN_FREQ, ColourMapper::MAX_FREQ);
	const float logValue = std::log2(clamped);
	if (LOG_FREQ_RANGE <= EPSILON) {
		return 0.0f;
	}
	const float normalised = (logValue - LOG_FREQ_MIN) / LOG_FREQ_RANGE;
	return std::clamp(normalised, 0.0f, 1.0f);
}

float ColourNativeCodec::denormaliseLogFrequency(const float normalised) {
	if (LOG_FREQ_RANGE <= EPSILON) {
		return ColourMapper::MIN_FREQ;
	}
	const float clamped = std::clamp(normalised, 0.0f, 1.0f);
	const float logValue = LOG_FREQ_MIN + clamped * LOG_FREQ_RANGE;
	return std::pow(2.0f, logValue);
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
