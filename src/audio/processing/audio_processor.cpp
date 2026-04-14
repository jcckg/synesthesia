#include "audio_processor.h"

#include <algorithm>

AudioProcessor::AudioProcessor()
	: writeIndex(0), readIndex(0), running(false), currentSpectralData{} {
	fftProcessors.push_back(std::make_unique<FFTProcessor>());
	activeChannelCount = 1;
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

	if (nextWrite == readIndex.load(std::memory_order_acquire)) {
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
	while (running.load(std::memory_order_acquire)) {
		std::unique_lock lock(queueMutex);
		dataAvailable.wait(lock, [this] {
			return !running.load(std::memory_order_acquire) ||
				   readIndex.load(std::memory_order_relaxed) !=
					   writeIndex.load(std::memory_order_acquire);
		});

		if (!running.load(std::memory_order_acquire))
			break;

		lock.unlock();

		while (running.load(std::memory_order_acquire)) {
			const size_t currentRead = readIndex.load(std::memory_order_relaxed);
			if (currentRead == writeIndex.load(std::memory_order_acquire)) {
				break;
			}

			processBuffer(audioQueue[currentRead]);
			readIndex.store((currentRead + 1) % QUEUE_SIZE, std::memory_order_release);
		}
	}
}

void AudioProcessor::processBuffer(const AudioBuffer& buffer) {
	if (buffer.numChannels == 0) {
		return;
	}

	std::lock_guard processorLock(processorMutex);
	ensureProcessorCountLocked(buffer.numChannels);
	activeChannelCount = buffer.numChannels;

	const size_t frames = buffer.sampleCount / buffer.numChannels;
	channelBufferScratch.resize(frames);
	stagingSpectralData.magnitudes.resize(buffer.numChannels);
	stagingSpectralData.phases.resize(buffer.numChannels);
	float maxDominantFreq = 0.0f;
	float maxMagnitudeVal = 0.0f;
	FFTProcessor::AnalysisState primaryAnalysis{};

	for (size_t ch = 0; ch < buffer.numChannels; ++ch) {
		for (size_t i = 0; i < frames; ++i) {
			channelBufferScratch[i] = buffer.data[i * buffer.numChannels + ch];
		}

		FFTProcessor::AnalysisState analysis{};
		fftProcessors[ch]->processBuffer(std::span(channelBufferScratch.data(), frames), buffer.sampleRate);
		fftProcessors[ch]->copyRawFrame(
			stagingSpectralData.magnitudes[ch],
			stagingSpectralData.phases[ch],
			analysis);

		if (!stagingSpectralData.magnitudes[ch].empty()) {
			const auto maxIt = std::max_element(
				stagingSpectralData.magnitudes[ch].begin(),
				stagingSpectralData.magnitudes[ch].end());
			const float currentMaxMag = *maxIt;

			if (currentMaxMag > maxMagnitudeVal) {
				maxMagnitudeVal = currentMaxMag;
				const size_t maxIndex = static_cast<size_t>(
					std::distance(stagingSpectralData.magnitudes[ch].begin(), maxIt));
				maxDominantFreq = static_cast<float>(maxIndex) * buffer.sampleRate /
								  static_cast<float>(FFTProcessor::FFT_SIZE);
			}
		}

		if (ch == 0) {
			primaryAnalysis = analysis;
		}
	}

	stagingSpectralData.sampleRate = buffer.sampleRate;
	stagingSpectralData.dominantFrequency = maxDominantFreq;
	stagingSpectralData.momentaryLoudnessLUFS = primaryAnalysis.momentaryLoudnessLUFS;
	stagingSpectralData.spectralFlux = primaryAnalysis.spectralFlux;
	stagingSpectralData.frameCounter = primaryAnalysis.frameCounter;
	stagingSpectralData.hopSize = primaryAnalysis.hopSize;
	stagingSpectralData.onsetDetected = primaryAnalysis.onsetDetected;

	{
		std::lock_guard lock(resultsMutex);
		std::swap(currentSpectralData, stagingSpectralData);
	}
}

AudioProcessor::SpectralData AudioProcessor::getSpectralData() const {
	SpectralData out;
	copySpectralData(out);
	return out;
}

void AudioProcessor::copySpectralData(SpectralData& out) const {
	std::lock_guard lock(resultsMutex);
	out = currentSpectralData;
}

AudioProcessor::BufferedFrames AudioProcessor::consumeBufferedFrames() {
	std::lock_guard processorLock(processorMutex);
	BufferedFrames frames(activeChannelCount);
	for (size_t ch = 0; ch < activeChannelCount; ++ch) {
		frames[ch] = fftProcessors[ch]->getBufferedFrames();
	}
	return frames;
}

void AudioProcessor::discardBufferedFrames() {
	(void)consumeBufferedFrames();
}

void AudioProcessor::setEQGains(const float low, const float mid, const float high) {
	std::lock_guard processorLock(processorMutex);
	eqLowGain = low;
	eqMidGain = mid;
	eqHighGain = high;
	for (auto& processor : fftProcessors) {
		processor->setEQGains(low, mid, high);
	}
}

void AudioProcessor::reset() {
	std::lock_guard processorLock(processorMutex);
	for (auto& processor : fftProcessors) {
		processor->reset();
	}

	{
		std::lock_guard lock(resultsMutex);
		currentSpectralData = {};
		stagingSpectralData = {};
	}
	droppedBufferCount.store(0, std::memory_order_relaxed);
}

FFTProcessor& AudioProcessor::getFFTProcessor(size_t channel) {
	std::lock_guard processorLock(processorMutex);
	FFTProcessor* processor = getProcessorForChannel(channel);
	return *processor;
}

const FFTProcessor& AudioProcessor::getFFTProcessor(size_t channel) const {
	std::lock_guard processorLock(processorMutex);
	const FFTProcessor* processor = getProcessorForChannel(channel);
	return *processor;
}

size_t AudioProcessor::getChannelCount() const {
	std::lock_guard processorLock(processorMutex);
	return activeChannelCount;
}

void AudioProcessor::ensureProcessorCountLocked(const size_t numChannels) {
	while (fftProcessors.size() < numChannels) {
		auto processor = std::make_unique<FFTProcessor>();
		processor->setEQGains(eqLowGain, eqMidGain, eqHighGain);
		fftProcessors.push_back(std::move(processor));
	}
}

FFTProcessor* AudioProcessor::getProcessorForChannel(const size_t channel) {
	if (fftProcessors.empty()) {
		fftProcessors.push_back(std::make_unique<FFTProcessor>());
		activeChannelCount = std::max<size_t>(activeChannelCount, 1);
	}
	const size_t safeChannel =
		channel < activeChannelCount ? channel : static_cast<size_t>(0);
	return fftProcessors[safeChannel].get();
}

const FFTProcessor* AudioProcessor::getProcessorForChannel(const size_t channel) const {
	if (fftProcessors.empty()) {
		static FFTProcessor dummy;
		return &dummy;
	}
	const size_t safeChannel =
		channel < activeChannelCount ? channel : static_cast<size_t>(0);
	return fftProcessors[safeChannel].get();
}
