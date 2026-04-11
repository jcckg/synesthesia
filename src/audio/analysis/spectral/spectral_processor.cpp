#include "spectral_processor.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>

namespace {

constexpr float MIN_LOUDNESS_DB = -70.0f; // Matches ITU-R BS.1770-4 gate for very quiet passages.
constexpr float MAX_LOUDNESS_DB = 0.0f;
constexpr float RMS_EPSILON = 1e-12f;
constexpr float PERCEPTUAL_REFERENCE_LUFS = -23.0f; // EBU R128 nominal programme level (0 LU).

struct SpectralBandBalance {
	float low = 0.0f;
	float mid = 0.0f;
	float high = 0.0f;
	float tilt = 0.0f;
};

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

bool isFiniteColourResult(const SpectralProcessor::SpectralColourResult& result) {
	return std::isfinite(result.r) &&
		std::isfinite(result.g) &&
		std::isfinite(result.b) &&
		std::isfinite(result.X) &&
		std::isfinite(result.Y) &&
		std::isfinite(result.Z) &&
		std::isfinite(result.L) &&
		std::isfinite(result.a) &&
		std::isfinite(result.b_comp);
}

float resolveBrightnessLoudnessDb(
	const float frameLoudnessDb,
	const float slowLoudnessDb,
	const float crestFactor,
	float& outTransientMix
) {
	const float clampedFrame = clampLoudnessDb(frameLoudnessDb);
	if (!std::isfinite(slowLoudnessDb)) {
		outTransientMix = 1.0f;
		return clampedFrame;
	}

	const float clampedSlow = clampLoudnessDb(slowLoudnessDb);
	const float attackDb = std::max(0.0f, clampedFrame - clampedSlow);
	const float crestMix = std::clamp((crestFactor - 1.5f) / 4.5f, 0.0f, 1.0f);
	const float attackMix = std::clamp(attackDb / 12.0f, 0.0f, 1.0f);
	outTransientMix = attackMix * (0.35f + 0.65f * crestMix);
	return clampedSlow + outTransientMix * attackDb;
}

float normaliseLogFrequency(const float frequency) {
	if (!std::isfinite(frequency) || frequency <= synesthesia::constants::MIN_AUDIO_FREQ) {
		return 0.0f;
	}
	const float minLog = std::log(synesthesia::constants::MIN_AUDIO_FREQ);
	const float maxLog = std::log(synesthesia::constants::MAX_AUDIO_FREQ);
	const float freqLog = std::log(std::clamp(
		frequency,
		synesthesia::constants::MIN_AUDIO_FREQ,
		synesthesia::constants::MAX_AUDIO_FREQ));
	return std::clamp((freqLog - minLog) / std::max(maxLog - minLog, 1e-6f), 0.0f, 1.0f);
}

float blendLogFrequencies(const float lhs, const float rhs, const float blend) {
	const float clampedLhs = std::clamp(std::isfinite(lhs) ? lhs : synesthesia::constants::MIN_AUDIO_FREQ,
		synesthesia::constants::MIN_AUDIO_FREQ, synesthesia::constants::MAX_AUDIO_FREQ);
	const float clampedRhs = std::clamp(std::isfinite(rhs) ? rhs : clampedLhs,
		synesthesia::constants::MIN_AUDIO_FREQ, synesthesia::constants::MAX_AUDIO_FREQ);
	return std::exp(std::lerp(std::log(clampedLhs), std::log(clampedRhs), std::clamp(blend, 0.0f, 1.0f)));
}

SpectralBandBalance calculateBandBalance(
	std::span<const float> magnitudes,
	std::span<const float> frequencies
) {
	SpectralBandBalance balance{};
	if (magnitudes.empty() || magnitudes.size() != frequencies.size()) {
		return balance;
	}

	for (size_t i = 0; i < magnitudes.size(); ++i) {
		const float magnitude = magnitudes[i];
		const float frequency = frequencies[i];
		if (!std::isfinite(magnitude) || !std::isfinite(frequency) || magnitude <= 0.0f) {
			continue;
		}

		const float energy = magnitude * magnitude;
		if (frequency < 220.0f) {
			balance.low += energy;
		} else if (frequency < 2200.0f) {
			balance.mid += energy;
		} else {
			balance.high += energy;
		}
	}

	const float total = balance.low + balance.mid + balance.high;
	if (total <= RMS_EPSILON) {
		return balance;
	}

	const float weightedLow = balance.low + 0.5f * balance.mid;
	const float weightedHigh = balance.high + 0.5f * balance.mid;
	balance.tilt = std::clamp((weightedHigh - weightedLow) / total, -1.0f, 1.0f);
	return balance;
}

}

SpectralProcessor::SpectralColourResult SpectralProcessor::spectrumToColour(
	std::span<const float> magnitudes,
	std::span<const float> /* phases */,
	std::span<const float> frequencies,
	const float sampleRate,
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
		result.frameLoudnessDb = MIN_LOUDNESS_DB;
		result.brightnessLoudnessDb = MIN_LOUDNESS_DB;
		result.loudnessNormalised = 0.0f;
		result.brightnessNormalised = 0.0f;
		result.transientMix = 0.0f;
		result.estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + MIN_LOUDNESS_DB;
		result.luminanceCdM2 = 0.0f;
		return result;
	}

	const size_t binCount = magnitudes.size();
	std::vector<float> cleanMagnitudes(binCount, 0.0f);
	for (size_t i = 0; i < binCount; ++i) {
		const float magnitude = magnitudes[i];
		cleanMagnitudes[i] = (std::isfinite(magnitude) && magnitude > 0.0f) ? magnitude : 0.0f;
	}
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
	result.spectralCentroid = calculateSpectralCentroid(cleanMagnitudes, effectiveFrequencies);
	result.spectralSpread = calculateSpectralSpread(cleanMagnitudes, effectiveFrequencies, result.spectralCentroid);
	result.spectralFlatness = calculateSpectralFlatness(cleanMagnitudes);
	result.spectralRolloff = calculateSpectralRolloff(cleanMagnitudes, effectiveFrequencies);

	float maxMag = 0.0f;
	float totalEnergyLocal = 0.0f;
	for (const float mag : cleanMagnitudes) {
		maxMag = std::max(maxMag, mag);
		totalEnergyLocal += mag * mag;
	}
	result.spectralCrestFactor = calculateSpectralCrestFactor(cleanMagnitudes, maxMag, totalEnergyLocal);

	const float computedLoudnessDb = calculateLoudnessDbFromEnergy(totalEnergyLocal, binCount);
	const float loudnessDb = std::isfinite(overrideLoudnessDb) ? overrideLoudnessDb : computedLoudnessDb;
	const float clampedLoudnessDb = clampLoudnessDb(loudnessDb);
	const float loudnessNormalised = std::clamp(normaliseLoudness(clampedLoudnessDb), 0.0f, 1.0f);
	float transientMix = 0.0f;
	const float brightnessLoudnessDb = resolveBrightnessLoudnessDb(
		computedLoudnessDb,
		loudnessDb,
		result.spectralCrestFactor,
		transientMix);
	const float centroidNorm = normaliseLogFrequency(result.spectralCentroid);
	const float rolloffNorm = normaliseLogFrequency(result.spectralRolloff);
	const float spreadNorm = std::clamp(
		result.spectralCentroid > 1e-3f ? (result.spectralSpread / result.spectralCentroid) / 1.75f : 0.0f,
		0.0f, 1.0f);
	const float crestNorm = std::clamp(
		std::log2(std::max(result.spectralCrestFactor, 1.0f)) / 3.5f,
		0.0f, 1.0f);
	const float tonalStrength = std::clamp(
		0.55f * (1.0f - result.spectralFlatness) + 0.45f * crestNorm,
		0.0f, 1.0f);
	const SpectralBandBalance bandBalance = calculateBandBalance(cleanMagnitudes, effectiveFrequencies);
	const float transientAccent = transientMix * (0.35f + 0.65f * tonalStrength);
	const float brightnessGain = std::clamp(
		loudnessToBrightness(brightnessLoudnessDb) * (1.0f + 0.18f * transientAccent),
		0.0f, 1.2f);
	const float estimatedSPL = synesthesia::constants::REFERENCE_SPL_AT_0_LUFS + clampedLoudnessDb;

	// Consume the spectrum exactly as supplied. The batch-export path passes the raw FFT
	// magnitudes here, while any psychoacoustic weighting lives in separate display code.

	float X_total = 0.0f;
	float Y_total = 0.0f;
	float Z_total = 0.0f;

	integrateSpectrumCIE(cleanMagnitudes, effectiveFrequencies, X_total, Y_total, Z_total);

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

	// Inharmonicity-gated desaturation: only noise-like signals lose saturation
	// PMC/12673365 (2025): harmonic richness INCREASES saturation — a violin with many
	// harmonics appears more vivid, not less. Only percussive/inharmonic content desaturates.
	// spectralFlatness reliably separates tonal (0) from noisy (1); spectralSpread alone cannot
	// distinguish "wide harmonics" (vivid) from "wide noise" (pastel).
	// Gate: desaturation = flatness * spread, so tonal wide-spread signals stay vivid.
	{
		constexpr float NOISE_DESATURATION_STRENGTH = 0.35f;
		constexpr float REF_X_OVER_Y = synesthesia::constants::CIE_D65_REF_X / synesthesia::constants::CIE_D65_REF_Y;
		constexpr float REF_Z_OVER_Y = synesthesia::constants::CIE_D65_REF_Z / synesthesia::constants::CIE_D65_REF_Y;

		const float flatness = std::clamp(result.spectralFlatness, 0.0f, 1.0f);
		const float spreadRatio = (result.spectralCentroid > 100.0f)
			? result.spectralSpread / result.spectralCentroid
			: 0.0f;
		const float normSpread = std::clamp(spreadRatio, 0.0f, 2.0f) * 0.5f;

		// Only desaturate when BOTH noisy AND spectrally wide
		const float desatAmount = flatness * normSpread * NOISE_DESATURATION_STRENGTH;
		const float saturation = std::clamp(1.0f - desatAmount, 0.4f, 1.0f);

		const float neutralX = chromaY * REF_X_OVER_Y;
		const float neutralZ = chromaY * REF_Z_OVER_Y;
		chromaX = std::lerp(neutralX, chromaX, saturation);
		chromaZ = std::lerp(neutralZ, chromaZ, saturation);
	}

	const float scaledX = chromaX * brightnessGain;
	const float scaledY = chromaY * brightnessGain;
	const float scaledZ = chromaZ * brightnessGain;

	// Nayatani (1997) Helmholtz-Kohlrausch correction
	// Saturated colours appear brighter than their CIE luminance suggests;
	// dividing by the VCC factor reduces luminance for high-chroma colours
	// so that perceived brightness is uniform across hues at equal loudness
	const float hkFactor = ColourMapper::helmholtzKohlrauschCorrection(scaledX, scaledY, scaledZ);
	const float hkX = scaledX / hkFactor;
	const float hkY = scaledY / hkFactor;
	const float hkZ = scaledZ / hkFactor;
	result.loudnessDb = clampedLoudnessDb;
	result.frameLoudnessDb = clampLoudnessDb(computedLoudnessDb);
	result.brightnessLoudnessDb = brightnessLoudnessDb;
	result.loudnessNormalised = loudnessNormalised;
	result.brightnessNormalised = brightnessGain;
	result.transientMix = transientMix;
	result.estimatedSPL = estimatedSPL;

	float labL = 0.0f;
	float labA = 0.0f;
	float labB = 0.0f;
	ColourMapper::XYZtoLab(hkX, hkY, hkZ, labL, labA, labB);

	const float baseRadius = std::hypot(labA, labB);
	const float guidedFrequency = blendLogFrequencies(
		result.spectralCentroid > 1e-3f ? result.spectralCentroid : result.spectralRolloff,
		result.spectralRolloff > 1e-3f ? result.spectralRolloff : result.spectralCentroid,
		0.35f);
	const float guidedWavelength = ColourMapper::logFrequencyToWavelength(guidedFrequency);
	float guidedX = 0.0f;
	float guidedY = 0.0f;
	float guidedZ = 0.0f;
	ColourMapper::interpolateCIE(guidedWavelength, guidedX, guidedY, guidedZ);
	const float guidedScale = guidedY > 1e-6f ? 0.35f / guidedY : 1.0f;
	float guidedL = 0.0f;
	float guidedA = 0.0f;
	float guidedB = 0.0f;
	ColourMapper::XYZtoLab(guidedX * guidedScale, guidedY * guidedScale, guidedZ * guidedScale, guidedL, guidedA, guidedB);

	const float baseNorm = baseRadius > 1e-6f ? 1.0f / baseRadius : 0.0f;
	const float guidedRadius = std::hypot(guidedA, guidedB);
	const float guidedNorm = guidedRadius > 1e-6f ? 1.0f / guidedRadius : 0.0f;

	float hueA = baseRadius > 1e-6f ? labA * baseNorm : guidedA * guidedNorm;
	float hueB = baseRadius > 1e-6f ? labB * baseNorm : guidedB * guidedNorm;
	const float hueGuidance = std::clamp(0.25f + 0.25f * tonalStrength + 0.15f * transientAccent, 0.0f, 0.65f);
	if (guidedRadius > 1e-6f) {
		hueA = std::lerp(hueA, guidedA * guidedNorm, hueGuidance);
		hueB = std::lerp(hueB, guidedB * guidedNorm, hueGuidance);
	}

	hueA += -bandBalance.tilt * 0.18f;
	hueB += -bandBalance.tilt * 0.26f;

	const float hueLength = std::hypot(hueA, hueB);
	if (hueLength > 1e-6f) {
		hueA /= hueLength;
		hueB /= hueLength;
	}

	const float semanticRadiusFloor = brightnessGain * (6.0f + 18.0f * tonalStrength);
	const float radiusSeed = std::max(baseRadius, semanticRadiusFloor);
	const float radiusScale = std::clamp(
		0.72f + 0.58f * tonalStrength + 0.18f * transientAccent - 0.18f * spreadNorm,
		0.45f, 1.45f);
	const float finalRadius = std::clamp(radiusSeed * radiusScale, 0.0f, 96.0f);
	const float finalL = std::clamp(
		labL + transientAccent * (4.0f + 6.0f * brightnessGain) + (centroidNorm - 0.5f) * 2.0f + (rolloffNorm - 0.5f) * 2.5f,
		0.0f, 100.0f);
	const float finalA = hueA * finalRadius;
	const float finalB = hueB * finalRadius;

	ColourMapper::LabtoXYZ(finalL, finalA, finalB, result.X, result.Y, result.Z);
	ColourMapper::XYZtoRGB(result.X, result.Y, result.Z, result.r, result.g, result.b, colourSpace, true, applyGamutMapping);

	result.L = finalL;
	result.a = finalA;
	result.b_comp = finalB;
	result.luminanceCdM2 =
		std::max(0.0f, result.Y) * synesthesia::constants::REFERENCE_WHITE_LUMINANCE_CDM2;

	result.dominantFrequency = result.spectralCentroid;
	result.dominantWavelength = ColourMapper::logFrequencyToWavelength(result.dominantFrequency);

	if (!isFiniteColourResult(result)) {
		std::cerr
			<< "[Synesthesia] Non-finite colour frame:"
			<< " centroid=" << result.spectralCentroid
			<< " rolloff=" << result.spectralRolloff
			<< " flatness=" << result.spectralFlatness
			<< " crest=" << result.spectralCrestFactor
			<< " loudness=" << result.loudnessDb
			<< " brightness=" << result.brightnessNormalised
			<< " transient=" << result.transientMix
			<< " rgb=(" << result.r << "," << result.g << "," << result.b << ")"
			<< " lab=(" << result.L << "," << result.a << "," << result.b_comp << ")"
			<< std::endl;
		result.r = 0.0f;
		result.g = 0.0f;
		result.b = 0.0f;
		result.X = 0.0f;
		result.Y = 0.0f;
		result.Z = 0.0f;
		result.L = 0.0f;
		result.a = 0.0f;
		result.b_comp = 0.0f;
	}

	return result;
}

SpectralProcessor::SpectralColourResult SpectralProcessor::spectrumToColour(
	const std::vector<FFTProcessor::ComplexBin>& spectrum,
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

	return spectrumToColour(magnitudes, phases, {}, sampleRate, colourSpace, applyGamutMapping, overrideLoudnessDb);
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
	if (magnitudes.empty() || !std::isfinite(maxMagnitude) || !std::isfinite(totalEnergy) || maxMagnitude < 1e-6f || totalEnergy < 1e-6f) {
		return 1.0f;
	}

	const float rms = std::sqrt(totalEnergy / static_cast<float>(magnitudes.size()));

	if (!std::isfinite(rms) || rms < 1e-6f) {
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

	const size_t count = magnitudes.size();

#if defined(USE_NEON_OPTIMISATIONS) || defined(USE_SSE_OPTIMISATIONS)
	const bool useSIMD =
#ifdef USE_NEON_OPTIMISATIONS
		SpectralProcessorNEON::isNEONAvailable()
#elif defined(USE_SSE_OPTIMISATIONS)
		SpectralProcessorSSE::isSSEAvailable()
#endif
		&& count >= 4;

	if (useSIMD) {
		// Pre-compute CIE XYZ values for each bin (CIE lookup is not vectorisable)
		thread_local std::vector<float> xVals, yVals, zVals;
		xVals.assign(count, 0.0f);
		yVals.assign(count, 0.0f);
		zVals.assign(count, 0.0f);

		for (size_t i = 0; i < count; ++i) {
			const float frequency = frequencies[i];
			const float magnitude = magnitudes[i];
			if (frequency < MIN_FREQ || frequency > MAX_FREQ || magnitude <= 1e-6f) {
				continue;
			}
			const float wavelength = ColourMapper::logFrequencyToWavelength(frequency);
			ColourMapper::interpolateCIE(wavelength, xVals[i], yVals[i], zVals[i]);
		}

#ifdef USE_NEON_OPTIMISATIONS
		SpectralProcessorNEON::integrateSpectrumCIE(
			magnitudes, xVals, yVals, zVals, count, X_total, Y_total, Z_total);
#elif defined(USE_SSE_OPTIMISATIONS)
		SpectralProcessorSSE::integrateSpectrumCIE(
			magnitudes, xVals, yVals, zVals, count, X_total, Y_total, Z_total);
#endif
		return;
	}
#endif

	for (size_t i = 0; i < count; ++i) {
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
