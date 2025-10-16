#include "audio_processor.h"

#include <algorithm>

AudioProcessor::AudioProcessor()
	: writeIndex(0), readIndex(0), running(false), currentSpectralData{} {}

AudioProcessor::~AudioProcessor() { stop(); }

void AudioProcessor::start() {
	if (running.exchange(true))
		return;
	writeIndex = 0;
	readIndex = 0;

	workerThread = std::thread(&AudioProcessor::processingThreadFunc, this);
}

void AudioProcessor::stop() {
	if (!running.exchange(false))
		return;
	{
		std::lock_guard lock(queueMutex);
		dataAvailable.notify_one();
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}
}

void AudioProcessor::queueAudioData(const float* buffer, const size_t numSamples,
									const float sampleRate) {
	if (!buffer || numSamples == 0 || !running)
		return;

	const size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
	const size_t nextWrite = (currentWrite + 1) % QUEUE_SIZE;

	if (nextWrite == readIndex.load(std::memory_order_relaxed)) {
		droppedBufferCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	AudioBuffer& queuedBuffer = audioQueue[currentWrite];
	queuedBuffer.sampleRate = sampleRate;
	queuedBuffer.sampleCount = std::min(numSamples, MAX_SAMPLES);
	std::copy_n(buffer, queuedBuffer.sampleCount, queuedBuffer.data.begin());

	writeIndex.store(nextWrite, std::memory_order_release);
	{
		std::lock_guard lock(queueMutex);
		dataAvailable.notify_one();
	}
}

void AudioProcessor::processingThreadFunc() {
	while (running) {
		std::unique_lock lock(queueMutex);
		dataAvailable.wait(lock, [this] {
			return !running || readIndex.load(std::memory_order_relaxed) !=
								   writeIndex.load(std::memory_order_relaxed);
		});

		if (!running)
			break;

		lock.unlock();

		while (running) {
			const size_t currentRead = readIndex.load(std::memory_order_relaxed);
			if (currentRead == writeIndex.load(std::memory_order_relaxed)) {
				break;
			}

			processBuffer(audioQueue[currentRead]);
			readIndex.store((currentRead + 1) % QUEUE_SIZE, std::memory_order_release);
		}
	}
}

void AudioProcessor::processBuffer(const AudioBuffer& buffer) {
	fftProcessor.processBuffer(std::span(buffer.data.data(), buffer.sampleCount),
							   buffer.sampleRate);

	auto magnitudes = fftProcessor.getMagnitudesBuffer();
	auto phases = fftProcessor.getPhaseBuffer();

	std::lock_guard lock(resultsMutex);
	currentSpectralData.magnitudes = std::move(magnitudes);
	currentSpectralData.phases = std::move(phases);
	currentSpectralData.sampleRate = buffer.sampleRate;

	if (!currentSpectralData.magnitudes.empty()) {
		const auto maxIt = std::max_element(currentSpectralData.magnitudes.begin(),
		                                     currentSpectralData.magnitudes.end());
		const size_t maxIndex = static_cast<size_t>(std::distance(currentSpectralData.magnitudes.begin(), maxIt));
		currentSpectralData.dominantFrequency =
			static_cast<float>(maxIndex) * buffer.sampleRate /
			static_cast<float>(FFTProcessor::FFT_SIZE);
	} else {
		currentSpectralData.dominantFrequency = 0.0f;
	}
}

AudioProcessor::SpectralData AudioProcessor::getSpectralData() const {
	std::lock_guard lock(resultsMutex);
	return currentSpectralData;
}

void AudioProcessor::setEQGains(const float low, const float mid, const float high) {
	fftProcessor.setEQGains(low, mid, high);
}

void AudioProcessor::reset() {
	fftProcessor.reset();

	std::lock_guard lock(resultsMutex);
	currentSpectralData = {};
	droppedBufferCount.store(0, std::memory_order_relaxed);
}
