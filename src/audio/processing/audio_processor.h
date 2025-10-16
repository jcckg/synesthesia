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
		std::vector<float> magnitudes;
		std::vector<float> phases;
		float sampleRate;
		float dominantFrequency;
	};

	AudioProcessor();
	~AudioProcessor();

	void queueAudioData(const float* buffer, size_t numSamples, float sampleRate);

	SpectralData getSpectralData() const;
	void setEQGains(float low, float mid, float high);
	void reset();
	void start();
	void stop();
	uint64_t getDroppedBufferCount() const { return droppedBufferCount.load(std::memory_order_relaxed); }

	FFTProcessor& getFFTProcessor() { return fftProcessor; }
	const FFTProcessor& getFFTProcessor() const { return fftProcessor; }

private:
	static constexpr size_t QUEUE_SIZE = 16;
	static constexpr size_t MAX_SAMPLES = 4096;

	struct AudioBuffer {
		std::vector<float> data = std::vector<float>(MAX_SAMPLES);
		size_t sampleCount = 0;
		float sampleRate = 44100.0f;
	};

	std::array<AudioBuffer, QUEUE_SIZE> audioQueue;
	std::atomic<size_t> writeIndex;
	std::atomic<size_t> readIndex;
	std::thread workerThread;
	std::atomic<bool> running;
	std::condition_variable dataAvailable;
	std::mutex queueMutex;
	std::atomic<uint64_t> droppedBufferCount{0};

	FFTProcessor fftProcessor;

	mutable std::mutex resultsMutex;
	SpectralData currentSpectralData;

	void processingThreadFunc();
	void processBuffer(const AudioBuffer& buffer);
};
