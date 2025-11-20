#include "audio_processor.h"

#include <algorithm>

AudioProcessor::AudioProcessor()
	: writeIndex(0), readIndex(0), running(false), currentSpectralData{} {
    fftProcessors.push_back(std::make_unique<FFTProcessor>());
}

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
									const float sampleRate, const size_t numChannels) {
	if (!buffer || numSamples == 0 || !running || numChannels == 0)
		return;

	const size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
	const size_t nextWrite = (currentWrite + 1) % QUEUE_SIZE;

	if (nextWrite == readIndex.load(std::memory_order_relaxed)) {
		droppedBufferCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	AudioBuffer& queuedBuffer = audioQueue[currentWrite];
	queuedBuffer.sampleRate = sampleRate;
	queuedBuffer.numChannels = numChannels;
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
	if (buffer.numChannels == 0) return;

	if (fftProcessors.size() != buffer.numChannels) {
		fftProcessors.clear();
		for (size_t i = 0; i < buffer.numChannels; ++i) {
			fftProcessors.push_back(std::make_unique<FFTProcessor>());
		}
	}

	const size_t frames = buffer.sampleCount / buffer.numChannels;
	std::vector<float> channelBuffer(frames);

	std::vector<std::vector<float>> allMagnitudes(buffer.numChannels);
	std::vector<std::vector<float>> allPhases(buffer.numChannels);
	float maxDominantFreq = 0.0f;
	float maxMagnitudeVal = 0.0f;

	for (size_t ch = 0; ch < buffer.numChannels; ++ch) {
		for (size_t i = 0; i < frames; ++i) {
			channelBuffer[i] = buffer.data[i * buffer.numChannels + ch];
		}

		fftProcessors[ch]->processBuffer(std::span(channelBuffer.data(), frames), buffer.sampleRate);

		auto magnitudes = fftProcessors[ch]->getMagnitudesBuffer();
		auto phases = fftProcessors[ch]->getPhaseBuffer();

		if (!magnitudes.empty()) {
			const auto maxIt = std::max_element(magnitudes.begin(), magnitudes.end());
			const float currentMaxMag = *maxIt;
			
			if (currentMaxMag > maxMagnitudeVal) {
				maxMagnitudeVal = currentMaxMag;
				const size_t maxIndex = static_cast<size_t>(std::distance(magnitudes.begin(), maxIt));
				maxDominantFreq = static_cast<float>(maxIndex) * buffer.sampleRate /
								  static_cast<float>(FFTProcessor::FFT_SIZE);
			}
		}

		allMagnitudes[ch] = std::move(magnitudes);
		allPhases[ch] = std::move(phases);
	}

	std::lock_guard lock(resultsMutex);
	currentSpectralData.magnitudes = std::move(allMagnitudes);
	currentSpectralData.phases = std::move(allPhases);
	currentSpectralData.sampleRate = buffer.sampleRate;
	currentSpectralData.dominantFrequency = maxDominantFreq;
}

AudioProcessor::SpectralData AudioProcessor::getSpectralData() const {
	std::lock_guard lock(resultsMutex);
	return currentSpectralData;
}

void AudioProcessor::setEQGains(const float low, const float mid, const float high) {
	for (auto& processor : fftProcessors) {
		processor->setEQGains(low, mid, high);
	}
}

void AudioProcessor::reset() {
	for (auto& processor : fftProcessors) {
		processor->reset();
	}

	std::lock_guard lock(resultsMutex);
	currentSpectralData = {};
	droppedBufferCount.store(0, std::memory_order_relaxed);
}

FFTProcessor& AudioProcessor::getFFTProcessor(size_t channel) {
    if (fftProcessors.empty()) {
        fftProcessors.push_back(std::make_unique<FFTProcessor>());
    }
    if (channel >= fftProcessors.size()) {
        return *fftProcessors[0];
    }
    return *fftProcessors[channel];
}

const FFTProcessor& AudioProcessor::getFFTProcessor(size_t channel) const {
    if (fftProcessors.empty()) {
        static FFTProcessor dummy;
        return dummy;
    }
    if (channel >= fftProcessors.size()) {
        return *fftProcessors[0];
    }
    return *fftProcessors[channel];
}
