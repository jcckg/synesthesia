#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "fft_processor.h"

class AudioProcessor {
public:
	struct SpectralData {
		std::vector<std::vector<float>> magnitudes;
		std::vector<std::vector<float>> phases;
		float dominantFrequency = 0.0f;
		float sampleRate = 0.0f;
		float momentaryLoudnessLUFS = -200.0f;
		float spectralFlux = 0.0f;
		uint64_t frameCounter = 0;
		int hopSize = FFTProcessor::HOP_SIZE;
		bool onsetDetected = false;
	};

	using BufferedFrames = std::vector<std::vector<FFTProcessor::FFTFrame>>;

	AudioProcessor();
	~AudioProcessor();

	void queueAudioData(const float* buffer, size_t numSamples, float sampleRate, size_t numChannels);

	SpectralData getSpectralData() const;
	void copySpectralData(SpectralData& out) const;
	BufferedFrames consumeBufferedFrames();
	void discardBufferedFrames();
	void setEQGains(float low, float mid, float high);
	void reset();
	void start();
	void stop();
	uint64_t getDroppedBufferCount() const { return droppedBufferCount.load(std::memory_order_relaxed); }

	FFTProcessor& getFFTProcessor(size_t channel = 0);
	const FFTProcessor& getFFTProcessor(size_t channel = 0) const;
	size_t getChannelCount() const;

private:
	static constexpr size_t QUEUE_SIZE = 16;
	static constexpr size_t MAX_SAMPLES = 4096;

	struct AudioBuffer {
		std::vector<float> data = std::vector<float>(MAX_SAMPLES);
		size_t sampleCount = 0;
		float sampleRate = 44100.0f;
		size_t numChannels = 1;
	};

	std::array<AudioBuffer, QUEUE_SIZE> audioQueue;
	std::atomic<size_t> writeIndex;
	std::atomic<size_t> readIndex;
	std::thread workerThread;
	std::atomic<bool> running;
	std::condition_variable dataAvailable;
	std::mutex queueMutex;
	std::atomic<uint64_t> droppedBufferCount{0};

	mutable std::mutex processorMutex;
	std::vector<std::unique_ptr<FFTProcessor>> fftProcessors;
	size_t activeChannelCount = 0;
	float eqLowGain = 1.0f;
	float eqMidGain = 1.0f;
	float eqHighGain = 1.0f;
	std::vector<float> channelBufferScratch;
	SpectralData stagingSpectralData;

	mutable std::mutex resultsMutex;
	SpectralData currentSpectralData;

	void processingThreadFunc();
	void processBuffer(const AudioBuffer& buffer);
	void ensureProcessorCountLocked(size_t numChannels);
	FFTProcessor* getProcessorForChannel(size_t channel);
	const FFTProcessor* getProcessorForChannel(size_t channel) const;
};
