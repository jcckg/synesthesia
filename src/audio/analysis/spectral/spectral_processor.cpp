#include "spectral_processor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace {

constexpr float MIN_LOUDNESS_DB = -70.0f; // Matches ITU-R BS.1770-4 gate for very quiet passages.
constexpr float MAX_LOUDNESS_DB = 0.0f;
constexpr float RMS_EPSILON = 1e-12f;
constexpr float PERCEPTUAL_REFERENCE_LUFS = -23.0f; // EBU R128 nominal programme level (0 LU).

float soneFromLoudness(const float loudnessDb) {
	// Stevens' power law: loudness in sones doubles for each +10 phon;LU ≈ 1 phon change around reference band.
	const float relative = (loudnessDb - PERCEPTUAL_REFERENCE_LUFS) / 10.0f;
	return std::pow(2.0f, relative);
}

float clampLoudnessDb(const float value) {
	if (!std::isfinite(value)) {
		return MIN_LOUDNESS_DB;
	}
	return std::clamp(value, MIN_LOUDNESS_DB, MAX_LOUDNESS_DB);
}

float calculateLoudnessDbFromEnergy(const float totalEnergy, const size_t binCount) {
	if (binCount == 0) {
		return MIN_LOUDNESS_DB;
	}
	const float meanSquare = totalEnergy / static_cast<float>(binCount);
	if (meanSquare <= RMS_EPSILON) {
		return MIN_LOUDNESS_DB;
	}
	const float rms = std::sqrt(std::max(meanSquare, RMS_EPSILON));
	return 20.0f * std::log10(std::max(rms, RMS_EPSILON));
}

float normaliseLoudness(const float clampedDb) {
	return (clampedDb - MIN_LOUDNESS_DB) / (MAX_LOUDNESS_DB - MIN_LOUDNESS_DB);
}

float logisticSone(const float sone) {
	return sone / (sone + 1.0f);
}

float loudnessToBrightness(const float clampedDb) {
	const float soneValue = soneFromLoudness(clampedDb);
	const float logisticValue = logisticSone(soneValue);
	const float logisticMin = logisticSone(soneFromLoudness(MIN_LOUDNESS_DB));
	const float logisticMax = logisticSone(soneFromLoudness(MAX_LOUDNESS_DB));
	float normalised = 0.0f;
	if (logisticMax - logisticMin > 1e-6f) {
		normalised = (logisticValue - logisticMin) / (logisticMax - logisticMin);
	}
	normalised = std::clamp(normalised, 0.0f, 1.0f);
	constexpr float BRIGHTNESS_RESPONSE_GAMMA = 1.1f;
	return std::pow(normalised, BRIGHTNESS_RESPONSE_GAMMA);
}

}

SpectralProcessor::SpectralColourResult SpectralProcessor::spectrumToColour(
	std::span<const float> magnitudes,
	std::span<const float> /* phases */,
	std::span<const float> frequencies,
	const float sampleRate,
	const float gamma,
	const ColourMapper::ColourSpace colourSpace,
	const bool applyGamutMapping,
	const float overrideLoudnessDb
) {
	SpectralColourResult result{};

	if (magnitudes.empty() || sampleRate <= 0.0f) {
		result.r = result.g = result.b = 0.1f;
		result.X = result.Y = result.Z = 0.0f;
		result.L = result.a = result.b_comp = 0.0f;
		result.loudnessDb = MIN_LOUDNESS_DB;
		result.loudnessNormalised = 0.0f;
		result.brightnessNormalised = 0.0f;
		result.estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + MIN_LOUDNESS_DB;
		result.luminanceCdM2 = 0.0f;
		return result;
	}

	const size_t binCount = magnitudes.size();
	std::vector<float> localFrequencies;
    std::span<const float> effectiveFrequencies;

    if (!frequencies.empty() && frequencies.size() == binCount) {
        effectiveFrequencies = frequencies;
    } else {
        localFrequencies.resize(binCount);
        const float binSize = sampleRate / (2.0f * (binCount - 1));
        for (size_t i = 0; i < binCount; ++i) {
            localFrequencies[i] = static_cast<float>(i) * binSize;
        }
        effectiveFrequencies = localFrequencies;
    }

	// Calculate spectral features on unweighted magnitudes for accurate frequency analysis
	result.spectralCentroid = calculateSpectralCentroid(magnitudes, effectiveFrequencies);
	result.spectralSpread = calculateSpectralSpread(magnitudes, effectiveFrequencies, result.spectralCentroid);
	result.spectralFlatness = calculateSpectralFlatness(magnitudes);
	result.spectralRolloff = calculateSpectralRolloff(magnitudes, effectiveFrequencies);

	float maxMag = 0.0f;
	float totalEnergyLocal = 0.0f;
	for (const float mag : magnitudes) {
		maxMag = std::max(maxMag, mag);
		totalEnergyLocal += mag * mag;
	}
	result.spectralCrestFactor = calculateSpectralCrestFactor(magnitudes, maxMag, totalEnergyLocal);

	const float computedLoudnessDb = calculateLoudnessDbFromEnergy(totalEnergyLocal, binCount);
	const float loudnessDb = std::isfinite(overrideLoudnessDb) ? overrideLoudnessDb : computedLoudnessDb;
	const float clampedLoudnessDb = clampLoudnessDb(loudnessDb);
	const float loudnessNormalised = std::clamp(normaliseLoudness(clampedLoudnessDb), 0.0f, 1.0f);
	const float brightnessGain = loudnessToBrightness(clampedLoudnessDb);
	const float estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + clampedLoudnessDb;
	const float luminanceCdM2 = brightnessGain * synesthesia::constants::REFERENCE_WHITE_LUMINANCE_CDM2;

	// Note: A-weighting and EQ are already applied in FFTProcessor::processMagnitudes
	// Do NOT apply perceptual weighting again as it would be applied twice

	float X_total = 0.0f;
	float Y_total = 0.0f;
	float Z_total = 0.0f;

	integrateSpectrumCIE(magnitudes, effectiveFrequencies, X_total, Y_total, Z_total);

	float chromaX = 0.0f;
	float chromaY = 0.0f;
	float chromaZ = 0.0f;
	const float totalWeight = std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0f);
	if (totalWeight > 1e-6f) {
		const float invWeight = 1.0f / totalWeight;
		chromaX = X_total * invWeight;
		chromaY = Y_total * invWeight;
		chromaZ = Z_total * invWeight;
	}

	const float scaledX = chromaX * brightnessGain;
	const float scaledY = chromaY * brightnessGain;
	const float scaledZ = chromaZ * brightnessGain;

	result.X = scaledX;
	result.Y = scaledY;
	result.Z = scaledZ;
	result.loudnessDb = clampedLoudnessDb;
	result.loudnessNormalised = loudnessNormalised;
	result.brightnessNormalised = brightnessGain;
	result.estimatedSPL = estimatedSPL;
	result.luminanceCdM2 = luminanceCdM2;

	ColourMapper::XYZtoRGB(scaledX, scaledY, scaledZ, result.r, result.g, result.b, colourSpace, true, applyGamutMapping);

	ColourMapper::XYZtoLab(scaledX, scaledY, scaledZ, result.L, result.a, result.b_comp);

	result.dominantFrequency = result.spectralCentroid;
	result.dominantWavelength = ColourMapper::logFrequencyToWavelength(result.dominantFrequency);

	const float clampedGamma = std::clamp(gamma, 0.1f, 5.0f);
	if (applyGamutMapping) {
		result.r = std::pow(std::clamp(result.r, 0.0f, 1.0f), clampedGamma);
		result.g = std::pow(std::clamp(result.g, 0.0f, 1.0f), clampedGamma);
		result.b = std::pow(std::clamp(result.b, 0.0f, 1.0f), clampedGamma);
	} else {
		auto applyCurve = [clampedGamma](float value) {
			if (value <= 0.0f) {
				return value;
			}
			return std::pow(value, clampedGamma);
		};
		result.r = applyCurve(result.r);
		result.g = applyCurve(result.g);
		result.b = applyCurve(result.b);
	}

	return result;
}

SpectralProcessor::SpectralColourResult SpectralProcessor::spectrumToColour(
	const std::vector<FFTProcessor::ComplexBin>& spectrum,
	const float gamma,
	const ColourMapper::ColourSpace colourSpace,
	const bool applyGamutMapping,
	const float overrideLoudnessDb
) {
	std::vector<float> magnitudes;
	std::vector<float> phases;
	magnitudes.reserve(spectrum.size());
	phases.reserve(spectrum.size());

	for (const auto& bin : spectrum) {
		magnitudes.push_back(bin.magnitude);
		phases.push_back(bin.phase);
	}

	float sampleRate = 44100.0f;
	if (!spectrum.empty() && spectrum.size() > 1) {
		const float binSize = spectrum[1].frequency - spectrum[0].frequency;
		sampleRate = binSize * 2.0f * (spectrum.size() - 1);
	}

	return spectrumToColour(magnitudes, phases, {}, sampleRate, gamma, colourSpace, applyGamutMapping, overrideLoudnessDb);
}

float SpectralProcessor::calculateSpectralCentroid(
	std::span<const float> magnitudes,
	std::span<const float> frequencies
) {
	float weightedSum = 0.0f;
	float totalWeight = 0.0f;

#ifdef USE_NEON_OPTIMISATIONS
	if (SpectralProcessorNEON::isNEONAvailable() && magnitudes.size() >= 4) {
		std::vector<float> validMags;
		std::vector<float> validFreqs;
		validMags.reserve(magnitudes.size());
		validFreqs.reserve(magnitudes.size());

		for (size_t i = 0; i < magnitudes.size(); ++i) {
			const float mag = magnitudes[i];
			const float freq = frequencies[i];
			if (freq >= MIN_FREQ && freq <= MAX_FREQ && mag > 1e-6f) {
				validMags.push_back(mag);
				validFreqs.push_back(freq);
			}
		}

		if (!validMags.empty()) {
			SpectralProcessorNEON::calculateSpectralCentroid(
				validMags, validFreqs, validMags.size(), weightedSum, totalWeight
			);
			return totalWeight > 1e-6f ? weightedSum / totalWeight : 0.0f;
		}
		return 0.0f;
	}
#elif defined(USE_SSE_OPTIMISATIONS)
	if (SpectralProcessorSSE::isSSEAvailable() && magnitudes.size() >= 4) {
		std::vector<float> validMags;
		std::vector<float> validFreqs;
		validMags.reserve(magnitudes.size());
		validFreqs.reserve(magnitudes.size());

		for (size_t i = 0; i < magnitudes.size(); ++i) {
			const float mag = magnitudes[i];
			const float freq = frequencies[i];
			if (freq >= MIN_FREQ && freq <= MAX_FREQ && mag > 1e-6f) {
				validMags.push_back(mag);
				validFreqs.push_back(freq);
			}
		}

		if (!validMags.empty()) {
			float weightedSum = 0.0f;
			float totalWeight = 0.0f;
			SpectralProcessorSSE::calculateSpectralCentroid(
				validMags, validFreqs, validMags.size(), weightedSum, totalWeight
			);
			return totalWeight > 1e-6f ? weightedSum / totalWeight : 0.0f;
		}
		return 0.0f;
	}
#endif

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float mag = magnitudes[i];
		const float freq = frequencies[i];

		if (freq < MIN_FREQ || freq > MAX_FREQ || mag <= 1e-6f) {
			continue;
		}

		weightedSum += freq * mag;
		totalWeight += mag;
	}

	return totalWeight > 1e-6f ? weightedSum / totalWeight : 0.0f;
}

float SpectralProcessor::calculateSpectralSpread(
	std::span<const float> magnitudes,
	std::span<const float> frequencies,
	const float centroid
) {
	float spreadSum = 0.0f;
	float totalWeight = 0.0f;

#ifdef USE_NEON_OPTIMISATIONS
	if (SpectralProcessorNEON::isNEONAvailable() && magnitudes.size() >= 4) {
		std::vector<float> validMags;
		std::vector<float> validFreqs;
		validMags.reserve(magnitudes.size());
		validFreqs.reserve(magnitudes.size());

		for (size_t i = 0; i < magnitudes.size(); ++i) {
			const float mag = magnitudes[i];
			const float freq = frequencies[i];
			if (freq >= MIN_FREQ && freq <= MAX_FREQ && mag > 1e-6f) {
				validMags.push_back(mag);
				validFreqs.push_back(freq);
			}
		}

		if (!validMags.empty()) {
			SpectralProcessorNEON::calculateSpectralSpread(
				validMags, validFreqs, validMags.size(), centroid, spreadSum, totalWeight
			);
			return totalWeight > 1e-6f ? std::sqrt(spreadSum / totalWeight) : 0.0f;
		}
		return 0.0f;
	}
#elif defined(USE_SSE_OPTIMISATIONS)
	if (SpectralProcessorSSE::isSSEAvailable() && magnitudes.size() >= 4) {
		std::vector<float> validMags;
		std::vector<float> validFreqs;
		validMags.reserve(magnitudes.size());
		validFreqs.reserve(magnitudes.size());

		for (size_t i = 0; i < magnitudes.size(); ++i) {
			const float mag = magnitudes[i];
			const float freq = frequencies[i];
			if (freq >= MIN_FREQ && freq <= MAX_FREQ && mag > 1e-6f) {
				validMags.push_back(mag);
				validFreqs.push_back(freq);
			}
		}

		if (!validMags.empty()) {
			float spreadSum = 0.0f;
			float totalWeight = 0.0f;
			SpectralProcessorSSE::calculateSpectralSpread(
				validMags, validFreqs, validMags.size(), centroid, spreadSum, totalWeight
			);
			return totalWeight > 1e-6f ? std::sqrt(spreadSum / totalWeight) : 0.0f;
		}
		return 0.0f;
	}
#endif

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float mag = magnitudes[i];
		const float freq = frequencies[i];

		if (freq < MIN_FREQ || freq > MAX_FREQ || mag <= 1e-6f) {
			continue;
		}

		const float diff = freq - centroid;
		spreadSum += mag * diff * diff;
		totalWeight += mag;
	}

	return totalWeight > 1e-6f ? std::sqrt(spreadSum / totalWeight) : 0.0f;
}

float SpectralProcessor::calculateSpectralFlatness(
	std::span<const float> magnitudes
) {
	float geometricSum = 0.0f;
	float arithmeticSum = 0.0f;
	int count = 0;

#ifdef USE_NEON_OPTIMISATIONS
	if (SpectralProcessorNEON::isNEONAvailable() && magnitudes.size() >= 4) {
		SpectralProcessorNEON::calculateSpectralFlatness(
			magnitudes, magnitudes.size(), geometricSum, arithmeticSum, count
		);

		if (count == 0 || arithmeticSum < 1e-6f) {
			return 0.5f;
		}

		const float geometricMean = std::exp(geometricSum / count);
		const float arithmeticMean = arithmeticSum / count;

		return geometricMean / arithmeticMean;
	}
#elif defined(USE_SSE_OPTIMISATIONS)
	if (SpectralProcessorSSE::isSSEAvailable() && magnitudes.size() >= 4) {
		SpectralProcessorSSE::calculateSpectralFlatness(
			magnitudes, magnitudes.size(), geometricSum, arithmeticSum, count
		);

		if (count == 0 || arithmeticSum < 1e-6f) {
			return 0.5f;
		}

		const float geometricMean = std::exp(geometricSum / count);
		const float arithmeticMean = arithmeticSum / count;

		return geometricMean / arithmeticMean;
	}
#endif

	for (const float mag : magnitudes) {
		if (mag > 1e-6f) {
			geometricSum += std::log(mag);
			arithmeticSum += mag;
			count++;
		}
	}

	if (count == 0 || arithmeticSum < 1e-6f) {
		return 0.5f;
	}

	const float geometricMean = std::exp(geometricSum / count);
	const float arithmeticMean = arithmeticSum / count;

	return geometricMean / arithmeticMean;
}

float SpectralProcessor::calculateSpectralRolloff(
	std::span<const float> magnitudes,
	std::span<const float> frequencies,
	const float threshold
) {
	if (magnitudes.empty() || frequencies.empty() || magnitudes.size() != frequencies.size()) {
		return 0.0f;
	}

	float totalEnergy = 0.0f;
	for (const float mag : magnitudes) {
		totalEnergy += mag * mag;
	}

	if (totalEnergy < 1e-6f) {
		return 0.0f;
	}

	const float targetEnergy = totalEnergy * threshold;
	float cumulativeEnergy = 0.0f;

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		cumulativeEnergy += magnitudes[i] * magnitudes[i];
		if (cumulativeEnergy >= targetEnergy) {
			return frequencies[i];
		}
	}

	return frequencies.back();
}

float SpectralProcessor::calculateSpectralCrestFactor(
	std::span<const float> magnitudes,
	const float maxMagnitude,
	const float totalEnergy
) {
	if (magnitudes.empty() || maxMagnitude < 1e-6f || totalEnergy < 1e-6f) {
		return 1.0f;
	}

	const float rms = std::sqrt(totalEnergy / static_cast<float>(magnitudes.size()));

	if (rms < 1e-6f) {
		return 1.0f;
	}

	return maxMagnitude / rms;
}

// Integrates audio spectrum into CIE XYZ colour space by:
// 1. Converting each frequency to wavelength (logarithmic mapping)
// 2. Looking up the CIE 2006 2° standard observer colour-matching functions
// 3. Weighting by magnitude and accumulating XYZ tristimulus values
// Note: Uses device-independent CIE XYZ; colour space conversion (sRGB/P3) happens later
void SpectralProcessor::integrateSpectrumCIE(
	std::span<const float> magnitudes,
	std::span<const float> frequencies,
	float& X_total,
	float& Y_total,
	float& Z_total
) {
	X_total = 0.0f;
	Y_total = 0.0f;
	Z_total = 0.0f;

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float magnitude = magnitudes[i];
		const float frequency = frequencies[i];

		if (frequency < MIN_FREQ || frequency > MAX_FREQ || magnitude <= 1e-6f) {
			continue;
		}

		const float wavelength = ColourMapper::logFrequencyToWavelength(frequency);

		float X, Y, Z;
		ColourMapper::interpolateCIE(wavelength, X, Y, Z);

		X_total += magnitude * X;
		Y_total += magnitude * Y;
		Z_total += magnitude * Z;
	}
}

// IEC 61672-1:2013 - Electroacoustics - Sound level meters - Part 1: Specifications
// Applies A-weighting perceptual filter to match human auditory sensitivity
// A-weighting boosts mid-frequencies (1-4 kHz) where human hearing is most sensitive
// and attenuates low (<1 kHz) and high (>8 kHz) frequencies
// Pole frequencies: f1=20.6 Hz, f2=107.7 Hz, f3=737.9 Hz, f4=12194 Hz
// Normalisation: +2.0 dB as specified in IEC 61672-1
// https://en.wikipedia.org/wiki/A-weighting
void SpectralProcessor::applyPerceptualWeighting(
	std::vector<float>& magnitudes,
	std::span<const float> frequencies
) {
#ifdef USE_NEON_OPTIMISATIONS
	if (SpectralProcessorNEON::isNEONAvailable() && magnitudes.size() >= 4) {
		SpectralProcessorNEON::applyPerceptualWeighting(magnitudes, frequencies, magnitudes.size());
		return;
	}
#elif defined(USE_SSE_OPTIMISATIONS)
	if (SpectralProcessorSSE::isSSEAvailable() && magnitudes.size() >= 4) {
		SpectralProcessorSSE::applyPerceptualWeighting(magnitudes, frequencies, magnitudes.size());
		return;
	}
#endif

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float freq = frequencies[i];

		if (freq < MIN_FREQ || freq > MAX_FREQ) {
			magnitudes[i] = 0.0f;
			continue;
		}

		const float f2 = freq * freq;
		const float numerator = 12194.0f * 12194.0f * f2 * f2;
		const float denominator = (f2 + 20.6f * 20.6f) *
								  std::sqrt((f2 + 107.7f * 107.7f) * (f2 + 737.9f * 737.9f)) *
								  (f2 + 12194.0f * 12194.0f);

		const float aWeight = numerator / denominator;
		const float dbAdjustment = 20.0f * std::log10(aWeight) + 2.0f;
		const float perceptualGain = std::exp(dbAdjustment * 0.11512925f);

		magnitudes[i] *= std::clamp(perceptualGain, 0.0f, 4.0f);
	}
}
