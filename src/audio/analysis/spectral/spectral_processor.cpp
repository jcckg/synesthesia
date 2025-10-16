#include "spectral_processor.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

SpectralProcessor::SpectralColourResult SpectralProcessor::spectrumToColour(
	std::span<const float> magnitudes,
	std::span<const float> /* phases */,
	const float sampleRate,
	const float gamma,
	const ColourMapper::ColourSpace colourSpace,
	const bool applyGamutMapping
) {
	SpectralColourResult result{};

	if (magnitudes.empty() || sampleRate <= 0.0f) {
		result.r = result.g = result.b = 0.1f;
		return result;
	}

	const size_t binCount = magnitudes.size();
	std::vector<float> frequencies(binCount);

	const float binSize = sampleRate / (2.0f * (binCount - 1));

	for (size_t i = 0; i < binCount; ++i) {
		frequencies[i] = static_cast<float>(i) * binSize;
	}

	// Calculate spectral features on unweighted magnitudes for accurate frequency analysis
	result.spectralCentroid = calculateSpectralCentroid(magnitudes, frequencies);
	result.spectralSpread = calculateSpectralSpread(magnitudes, frequencies, result.spectralCentroid);
	result.spectralFlatness = calculateSpectralFlatness(magnitudes);
	result.spectralRolloff = calculateSpectralRolloff(magnitudes, frequencies);

	float maxMag = 0.0f;
	float totalEnergyLocal = 0.0f;
	for (const float mag : magnitudes) {
		maxMag = std::max(maxMag, mag);
		totalEnergyLocal += mag * mag;
	}
	result.spectralCrestFactor = calculateSpectralCrestFactor(magnitudes, maxMag, totalEnergyLocal);

	// Note: A-weighting and EQ are already applied in FFTProcessor::processMagnitudes
	// Do NOT apply perceptual weighting again as it would be applied twice

	float X_total = 0.0f;
	float Y_total = 0.0f;
	float Z_total = 0.0f;

	integrateSpectrumCIE(magnitudes, frequencies, X_total, Y_total, Z_total);

	// Normalise XYZ by total spectral weight to produce weighted average colour
	// rather than absolute tristimulus values. This ensures consistent brightness
	// independent of overall signal amplitude.
	float totalWeight = std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0f);
	if (totalWeight > 1e-6f) {
		X_total /= totalWeight;
		Y_total /= totalWeight;
		Z_total /= totalWeight;
	}

	result.X = X_total;
	result.Y = Y_total;
	result.Z = Z_total;

	ColourMapper::XYZtoRGB(X_total, Y_total, Z_total, result.r, result.g, result.b, colourSpace, true, applyGamutMapping);

	ColourMapper::XYZtoLab(X_total, Y_total, Z_total, result.L, result.a, result.b_comp);

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
	const bool applyGamutMapping
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

	return spectrumToColour(magnitudes, phases, sampleRate, gamma, colourSpace, applyGamutMapping);
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
