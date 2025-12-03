#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <span>
#include <vector>

#include "equaliser.h"
#include "kiss_fftr.h"
#include "loudness_meter.h"
#include "constants.h"

#ifdef USE_NEON_OPTIMISATIONS
#include "neon/fft_processor_neon.h"
#endif

#if defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include "sse/fft_processor_sse.h"
#endif

class FFTProcessor {
public:
	static constexpr int FFT_SIZE = 2048;
	static constexpr int HOP_SIZE = FFT_SIZE / 2;
	static constexpr float MIN_FREQ = synesthesia::constants::MIN_AUDIO_FREQ;
	static constexpr float MAX_FREQ = synesthesia::constants::MAX_AUDIO_FREQ;
	static constexpr float MAGNITUDE_EPSILON = 1e-6f;
	static constexpr float MEL_SCALE_TRANSITION_FREQ = 1000.0f;
	static constexpr float MEL_LINEAR_WEIGHT = 3.0f;
	static constexpr float MEL_LOG_NUMERATOR = 27.0f;
	static constexpr float MEL_MIN_WEIGHT = 1.0f;

	struct ComplexBin {
		float frequency;
		float magnitude;
		float phase;
	};

	// Glasberg & Moore (1990) - ERB-based critical band for psychoacoustic smoothing
	struct CriticalBand {
		float centerFrequency;
		float lowerFreq;
		float upperFreq;
		size_t startBin;
		size_t endBin;
		float smoothingFactor;
		float smoothedMagnitude;
		float rawMagnitude;

		CriticalBand()
			: centerFrequency(0.0f), lowerFreq(0.0f), upperFreq(0.0f),
			  startBin(0), endBin(0), smoothingFactor(0.2f),
			  smoothedMagnitude(0.0f), rawMagnitude(0.0f) {}
	};

	struct FFTFrame {
		std::vector<float> magnitudes;
		std::vector<float> phases;
		uint64_t frameCounter;
		float sampleRate;
		float loudnessLUFS;
		bool loudnessAssigned;

		FFTFrame() : frameCounter(0), sampleRate(0.0f), loudnessLUFS(-200.0f), loudnessAssigned(false) {}
	};

	FFTProcessor();
	~FFTProcessor();

	FFTProcessor(const FFTProcessor&) = delete;
	FFTProcessor& operator=(const FFTProcessor&) = delete;
	FFTProcessor(FFTProcessor&&) noexcept = delete;
	FFTProcessor& operator=(FFTProcessor&&) noexcept = delete;

	void processBuffer(std::span<const float> buffer, float sampleRate);
	std::vector<float> getMagnitudesBuffer() const;
	std::vector<float> getSpectralEnvelope() const;
	std::vector<float> getPhaseBuffer() const;
	void reset();
	void setEQGains(float low, float mid, float high);
	void setCriticalBandSmoothingEnabled(bool enabled);
	bool getCriticalBandSmoothingEnabled() const { return criticalBandSmoothingEnabled; }
	const std::vector<CriticalBand>& getCriticalBands() const { return criticalBands; }

	std::vector<FFTFrame> getBufferedFrames();
	uint64_t getDroppedFrameCount() const { return droppedFrameCount.load(std::memory_order_relaxed); }
	float getCurrentLoudness() const { return currentLoudness; }
	float getMomentaryLoudnessLUFS() const { return momentaryLoudnessLUFS; }
	float getTotalEnergy() const { return totalEnergy; }
	float getMaxMagnitude() const { return maxMagnitude; }
	float getSpectralFlux() const { return spectralFlux; }
	bool getOnsetDetected() const { return onsetDetected; }

	static float calculateMelWeight(float frequency);
	static float calculateERB(float frequency);
	static float frequencyToERBScale(float frequency);
	static float erbScaleToFrequency(float erbScale);

private:
	kiss_fftr_cfg fft_cfg;
	std::vector<float> fft_in;
	std::vector<kiss_fft_cpx> fft_out;

	// Lock hierarchy: processingMutex → dataMutex → frameBufferMutex
	// Always acquire in this order to prevent deadlocks
	mutable std::mutex processingMutex;

	std::vector<float> hannWindow;
	std::vector<float> overlapBuffer;
	std::vector<float> windowBuffer;
	std::vector<float> inputAccumulator;
	size_t accumulatedSamples;

	std::vector<float> magnitudesBuffer;
	std::vector<float> rawMagnitudesBuffer;
	std::vector<float> processedMagnitudesBuffer;
	std::vector<float> spectralEnvelope;
	std::vector<float> phaseBuffer;

	mutable std::mutex dataMutex;

	Equaliser equaliser;
	LoudnessMeter loudnessMeter;

	float currentLoudness;
	float momentaryLoudnessLUFS;
	float totalEnergy;
	float maxMagnitude;
	float spectralFlux;
	bool onsetDetected;
	std::vector<float> previousMagnitudes;
	std::vector<float> fluxHistory;
	size_t fluxHistoryIndex;
	uint64_t frameCounter;

	std::vector<CriticalBand> criticalBands;
	bool criticalBandSmoothingEnabled;
	static constexpr float LOUDNESS_SMOOTHING = 0.3f;
	static constexpr size_t FLUX_HISTORY_SIZE = 10;
	static constexpr float ONSET_THRESHOLD_MULTIPLIER = 1.5f;
	static constexpr float LUFS_NORMALISATION_OFFSET = 70.0f;

	static constexpr size_t FRAME_BUFFER_SIZE = 128;
	std::vector<FFTFrame> frameRingBuffer;
	size_t frameBufferHead{0};
	size_t frameBufferTail{0};
	std::atomic<uint64_t> droppedFrameCount{0};
	mutable std::mutex frameBufferMutex;

	void applyWindow(std::span<const float> buffer);
	void processOverlappingWindow(float sampleRate);

	void normalizeFFTOutput();
	float updateLoudnessMetrics();
	void updateSpectralData(const std::vector<float>& rawMagnitudes, float sampleRate,
							float frameMaxMagnitude, float frameTotalEnergy, float normalisedLoudness);

	void calculateMagnitudes(std::vector<float>& rawMagnitudes, float sampleRate,
							 float& outMaxMagnitude, float& outTotalEnergy) const;
	void calculatePhases();
	void processMagnitudes(std::vector<float>& magnitudes, float sampleRate, float referenceMaxMagnitude);
	void calculateSpectralFluxAndOnset(const std::vector<float>& currentMagnitudes);
	void pushFrameToBuffer(const std::vector<float>& mags, const std::vector<float>& phases, float sampleRate);

	void initialiseCriticalBands(float sampleRate);
	void applyCriticalBandSmoothing(std::vector<float>& magnitudes);
	float calculatePsychoacousticSmoothingFactor(float frequency) const;
};
