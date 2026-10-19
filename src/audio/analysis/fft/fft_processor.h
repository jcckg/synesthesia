#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "real_fft.h"
#include "bass_analyser.h"
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
	static constexpr int MAX_FFT_SIZE = 32768;
	static int fftSizeForSampleRate(float sampleRate);
	static int hopSizeForSampleRate(float sampleRate, int requestedHopSize = 0);
	static constexpr float MIN_FREQ = synesthesia::constants::MIN_AUDIO_FREQ;
	static constexpr float MAX_FREQ = synesthesia::constants::MAX_AUDIO_FREQ;
	static constexpr float MAGNITUDE_EPSILON = 1e-6f;

	struct ComplexBin {
		float frequency;
		float magnitude;
		float phase;
	};

	struct FFTFrame {
		std::vector<float> magnitudes;
		std::vector<float> phases;
        std::vector<float> kWeightedMagnitudes;
		uint64_t frameCounter;
		float sampleRate;
		float loudnessLUFS;
        std::shared_ptr<const AudioAnalysis::BassSpectrum> bass;
		int hopSize = HOP_SIZE;

		FFTFrame() : frameCounter(0), sampleRate(0.0f), loudnessLUFS(-200.0f) {}
	};

	struct AnalysisState {
		uint64_t frameCounter = 0;
		float momentaryLoudnessLUFS = -200.0f;
		float totalEnergy = 0.0f;
		float maxMagnitude = 0.0f;
		int hopSize = HOP_SIZE;
		int fftSize = FFT_SIZE;
	};

	FFTProcessor();
	~FFTProcessor();

	FFTProcessor(const FFTProcessor&) = delete;
	FFTProcessor& operator=(const FFTProcessor&) = delete;
	FFTProcessor(FFTProcessor&&) noexcept = delete;
	FFTProcessor& operator=(FFTProcessor&&) noexcept = delete;

	void processBuffer(std::span<const float> buffer, float sampleRate, std::vector<FFTFrame>* completedFrames = nullptr);
	std::vector<float> getRawMagnitudesBuffer() const;
	std::vector<float> getPhaseBuffer() const;
	void copyRawFrame(std::vector<float>& rawMagnitudes, std::vector<float>& phases, AnalysisState& state) const;
	void reset();
	std::vector<FFTFrame> finish();
	void setHopSize(int hopSize);
	int getHopSize() const;
	int getFftSize() const;

	std::vector<FFTFrame> getBufferedFrames();
    void discardBufferedFrames();
	uint64_t getDroppedFrameCount() const { return droppedFrameCount.load(std::memory_order_relaxed); }
	float getMomentaryLoudnessLUFS() const;
	float getTotalEnergy() const;
	float getMaxMagnitude() const;
	uint64_t getFrameCounter() const;

private:
	std::unique_ptr<AudioAnalysis::RealFFT> transform = std::make_unique<AudioAnalysis::RealFFT>(FFT_SIZE);
    AudioAnalysis::BassAnalyser bassAnalyser;
    std::shared_ptr<const AudioAnalysis::BassSpectrum> bassSpectrum;
    std::vector<float> magnitudeScratch;
	std::vector<float> fft_in;
	std::vector<kiss_fft_cpx> fft_out;

	mutable std::mutex processingMutex;

	std::vector<float> hannWindow;
	std::vector<float> windowBuffer;
	std::vector<float> inputAccumulator;
    std::vector<float> weightedAccumulator;
    std::vector<float> weightedWindow;
    std::vector<float> weightedMagnitudes;
	size_t accumulatedSamples;
	size_t analysisHopSize;
	int requestedHopSize = 0;
	size_t overlapSize;
	float analysisSampleRate = 0.0f;
    bool finished = false;

	std::vector<float> rawMagnitudesBuffer;
	std::vector<float> phaseBuffer;

	mutable std::mutex dataMutex;

	LoudnessMeter loudnessMeter;

	float momentaryLoudnessLUFS;
	float totalEnergy;
	float maxMagnitude;
	uint64_t frameCounter;

	static constexpr size_t FRAME_BUFFER_SIZE = 128;
	std::vector<FFTFrame> frameRingBuffer;
	size_t frameBufferHead{0};
	size_t frameBufferTail{0};
	std::atomic<uint64_t> droppedFrameCount{0};
	mutable std::mutex frameBufferMutex;

	void applyWindow(std::span<const float> buffer);
	void resetState();
	void configureAnalysis(float sampleRate);
	void processOverlappingWindow(float sampleRate);

	void normaliseFFTOutput();
	void updateSpectralData(const std::vector<float>& rawMagnitudes, float sampleRate,
							float frameMaxMagnitude, float frameTotalEnergy);

	void calculateMagnitudes(std::vector<float>& rawMagnitudes, float sampleRate,
							 float& outMaxMagnitude, float& outTotalEnergy) const;
	void calculatePhases();
	void pushFrameToBuffer(const std::vector<float>& mags, const std::vector<float>& phases, float sampleRate);

};
