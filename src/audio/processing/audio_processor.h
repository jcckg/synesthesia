#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "fft_processor.h"

class AudioProcessor {
public:
	struct SpectralData {
		std::vector<std::vector<float>> magnitudes;
		std::vector<std::vector<float>> phases;
		float dominantFrequency;
		float sampleRate;
	};

	AudioProcessor();
	~AudioProcessor();

	void queueAudioData(const float* buffer, size_t numSamples, float sampleRate, size_t numChannels);

	SpectralData getSpectralData() const;
	void setEQGains(float low, float mid, float high);
	void reset();
	void start();
	void stop();
	uint64_t getDroppedBufferCount() const { return droppedBufferCount.load(std::memory_order_relaxed); }

	FFTProcessor& getFFTProcessor(size_t channel = 0);
	const FFTProcessor& getFFTProcessor(size_t channel = 0) const;
    size_t getChannelCount() const { return fftProcessors.size(); }

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

	std::vector<std::unique_ptr<FFTProcessor>> fftProcessors;

	mutable std::mutex resultsMutex;
	SpectralData currentSpectralData;

	void processingThreadFunc();
	void processBuffer(const AudioBuffer& buffer);
};
