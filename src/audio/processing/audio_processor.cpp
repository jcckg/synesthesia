#include "audio_processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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
    sourceFramePosition = 0;
    capturedFrameCount = 0;
    processedFrameCount = 0;

	workerThread = std::thread(&AudioProcessor::processingThreadFunc, this);
}

void AudioProcessor::stop() {
	if (!running.exchange(false))
		return;

	if (workerThread.joinable()) {
		workerThread.join();
	}
}

// https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html
void AudioProcessor::queueAudioData(const float* buffer, const size_t numSamples,
    const float sampleRate, const size_t numChannels) {
    if (!buffer || !running.load(std::memory_order_acquire) || numChannels == 0 ||
        !std::isfinite(sampleRate) || sampleRate < 8000.0f) return;
    const size_t frames = numSamples / numChannels;
    const size_t capacity = MAX_SAMPLES / numChannels;
    if (capacity == 0) {
        droppedBufferCount.fetch_add(1, std::memory_order_relaxed);
        droppedFrameCount.fetch_add(frames, std::memory_order_relaxed);
        return;
    }
    size_t copied = 0;
    while (copied < frames && capacity > 0) {
        const size_t current = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (current + 1) % QUEUE_SIZE;
        if (next == readIndex.load(std::memory_order_acquire)) break;
        auto& queued = audioQueue[current];
        const size_t count = std::min(capacity, frames - copied);
        queued.sampleRate = sampleRate;
        queued.numChannels = numChannels;
        queued.sampleCount = count * numChannels;
        queued.firstFrame = sourceFramePosition + copied;
        std::copy_n(buffer + copied * numChannels, queued.sampleCount, queued.data.begin());
        writeIndex.store(next, std::memory_order_release);
        copied += count;
    }
    if (copied < frames) {
        droppedBufferCount.fetch_add(1, std::memory_order_relaxed);
        droppedFrameCount.fetch_add(frames - copied, std::memory_order_relaxed);
    }
    sourceFramePosition += frames;
    capturedFrameCount.store(sourceFramePosition, std::memory_order_release);
}

void AudioProcessor::processingThreadFunc() {
    AudioBuffer silence;
    std::uint64_t processed = 0;
    float previousRate = 0.0f;
    size_t previousChannels = 0;
    const auto fillGap = [&](std::uint64_t end) {
        if (previousChannels == 0 || previousChannels > MAX_SAMPLES) return;
        silence.numChannels = previousChannels;
        silence.sampleRate = previousRate;
        std::fill(silence.data.begin(), silence.data.end(), 0.0f);
        while (processed < end) {
            const auto frames = std::min<std::uint64_t>(end - processed, MAX_SAMPLES / previousChannels);
            silence.sampleCount = static_cast<size_t>(frames) * previousChannels;
            processBuffer(silence);
            processed += frames;
            processedFrameCount.store(processed, std::memory_order_release);
        }
    };
    for (;;) {
        const size_t current = readIndex.load(std::memory_order_relaxed);
        if (current == writeIndex.load(std::memory_order_acquire)) {
            const auto captured = capturedFrameCount.load(std::memory_order_acquire);
            if (current != writeIndex.load(std::memory_order_acquire)) continue;
            fillGap(captured);
            if (!running.load(std::memory_order_acquire)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto& buffer = audioQueue[current];
        if (previousChannels == 0) {
            previousChannels = buffer.numChannels;
            previousRate = buffer.sampleRate;
        }
        fillGap(buffer.firstFrame);
        processBuffer(buffer);
        previousChannels = buffer.numChannels;
        previousRate = buffer.sampleRate;
        processed = buffer.firstFrame + buffer.sampleCount / buffer.numChannels;
        processedFrameCount.store(processed, std::memory_order_release);
        readIndex.store((current + 1) % QUEUE_SIZE, std::memory_order_release);
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
	bool hasPrimaryAnalysis = false;
	std::vector<float> channelLoudness(buffer.numChannels, -200.0f);
    BufferedFrames presentationFrames(buffer.numChannels);

	for (size_t ch = 0; ch < buffer.numChannels; ++ch) {
		for (size_t i = 0; i < frames; ++i) {
			channelBufferScratch[i] = buffer.data[i * buffer.numChannels + ch];
		}

        if (recordingAnalysis) {
            while (recordingProcessors.size() < buffer.numChannels) {
                auto processor = std::make_unique<FFTProcessor>();
                processor->setHopSize(recordingHop);
                recordingProcessors.push_back(std::move(processor));
            }
            recordingPendingFrames.resize(buffer.numChannels);
            recordingProcessors[ch]->processBuffer(std::span(channelBufferScratch.data(), frames), buffer.sampleRate,
                &recordingPendingFrames[ch]);
            recordingProcessors[ch]->discardBufferedFrames();
        }
		FFTProcessor::AnalysisState analysis{};
		fftProcessors[ch]->processBuffer(std::span(channelBufferScratch.data(), frames), buffer.sampleRate, &presentationFrames[ch]);
		fftProcessors[ch]->copyRawFrame(
			stagingSpectralData.magnitudes[ch],
			stagingSpectralData.phases[ch],
			analysis);
		channelLoudness[ch] = analysis.momentaryLoudnessLUFS;

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
								  static_cast<float>(analysis.fftSize);
			}
		}

		if (!hasPrimaryAnalysis || analysis.totalEnergy > primaryAnalysis.totalEnergy ||
			(analysis.totalEnergy == primaryAnalysis.totalEnergy &&
			 analysis.maxMagnitude > primaryAnalysis.maxMagnitude)) {
			primaryAnalysis = analysis;
			hasPrimaryAnalysis = true;
		}
	}

    std::size_t completed = presentationFrames.empty() ? 0 : presentationFrames.front().size();
    for (const auto& channel : presentationFrames) completed = std::min(completed, channel.size());
    for (std::size_t i = 0; i < completed; ++i) {
        AudioColourSample sample;
        sample.sampleRate = buffer.sampleRate;
        sample.channels = static_cast<std::uint32_t>(buffer.numChannels);
        const auto& frame = presentationFrames[0][i];
        const auto fftSize = 2 * (frame.magnitudes.size() - 1);
        sample.timestamp = (static_cast<double>(frame.frameCounter) * static_cast<double>(frame.hopSize) - static_cast<double>(fftSize) * .5) / static_cast<double>(buffer.sampleRate);
        sample.magnitudes.resize(buffer.numChannels);
        sample.phases.resize(buffer.numChannels);
        sample.bass.resize(buffer.numChannels);
        sample.kWeightedMagnitudes.resize(buffer.numChannels);
        std::vector<float> levels(buffer.numChannels);
        for (std::size_t ch = 0; ch < buffer.numChannels; ++ch) {
            sample.magnitudes[ch] = std::move(presentationFrames[ch][i].magnitudes);
            sample.phases[ch] = std::move(presentationFrames[ch][i].phases);
            sample.bass[ch] = presentationFrames[ch][i].bass;
            sample.kWeightedMagnitudes[ch] = std::move(presentationFrames[ch][i].kWeightedMagnitudes);
            levels[ch] = presentationFrames[ch][i].loudnessLUFS;
        }
        sample.loudnessLUFS = LoudnessMeter::combineChannelLoudness(levels);
        const auto& result = livePresentation.process(sample);
        if (i + 1 == completed) latestPresentation = std::make_shared<SpectralPresentation::LiveResult>(result);
    }
    stagingSpectralData.presentation = latestPresentation;

    if (recordingAnalysis) {
        recordingSampleCount += frames;
        recordingRate = buffer.sampleRate;
    }
	stagingSpectralData.sampleRate = buffer.sampleRate;
	stagingSpectralData.dominantFrequency = maxDominantFreq;
	stagingSpectralData.momentaryLoudnessLUFS = LoudnessMeter::combineChannelLoudness(channelLoudness);
	stagingSpectralData.spectralFlux = latestPresentation ? latestPresentation->frame.smoothingSignals.spectralFlux : 0.0f;
	stagingSpectralData.frameCounter = primaryAnalysis.frameCounter;
	stagingSpectralData.hopSize = primaryAnalysis.hopSize;
	stagingSpectralData.onsetDetected = latestPresentation && latestPresentation->frame.smoothingSignals.onsetDetected;

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

std::shared_ptr<const SpectralPresentation::LiveResult> AudioProcessor::getPresentation() const {
    std::lock_guard lock(resultsMutex);
    return currentSpectralData.presentation;
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

void AudioProcessor::beginRecordingAnalysis(const int hopSize) {
    std::lock_guard lock(processorMutex);
    recordingProcessors.clear(); recordingSampleCount = 0; recordingRate = 0.0f;
    recordingPendingFrames.clear();
    recordingHop = std::max(0, hopSize);
    recordingAnalysis = true;
}

AudioProcessor::BufferedFrames AudioProcessor::drainRecordingFramesLocked(const bool finish) {
    auto frames = std::move(recordingPendingFrames);
    frames.resize(recordingProcessors.size());
    recordingPendingFrames = BufferedFrames(recordingProcessors.size());
    for (size_t ch = 0; ch < frames.size(); ++ch) {
        if (finish) {
            auto tail = recordingProcessors[ch]->finish();
            frames[ch].insert(frames[ch].end(), std::make_move_iterator(tail.begin()),std::make_move_iterator(tail.end()));
        }
    }
    return frames;
}

AudioProcessor::BufferedFrames AudioProcessor::consumeRecordingFrames() {
    std::lock_guard lock(processorMutex);
    return drainRecordingFramesLocked(false);
}

AudioProcessor::BufferedFrames AudioProcessor::finishRecordingAnalysis(std::uint64_t& count, float& rate) {
    const auto end = capturedFrameCount.load(std::memory_order_acquire);
    while (running.load(std::memory_order_acquire) && processedFrameCount.load(std::memory_order_acquire) < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard lock(processorMutex);
    recordingAnalysis = false;
    count = recordingSampleCount; rate = recordingRate;
    auto frames = drainRecordingFramesLocked(true);
    recordingProcessors.clear();
    return frames;
}

void AudioProcessor::cancelRecordingAnalysis() {
    std::lock_guard lock(processorMutex);
    recordingAnalysis = false; recordingProcessors.clear(); recordingPendingFrames.clear();
}

void AudioProcessor::setPresentationSettings(const RSYNPresentationSettings& settings) {
    std::lock_guard lock(processorMutex);
    livePresentation.setSettings(settings);
}

void AudioProcessor::reset(bool preserveColour) {
	std::lock_guard processorLock(processorMutex);
    livePresentation.reset(preserveColour);
    latestPresentation.reset();
	for (auto& processor : fftProcessors) {
		processor->reset();
	}

	{
		std::lock_guard lock(resultsMutex);
		currentSpectralData = {};
		stagingSpectralData = {};
	}
	droppedBufferCount.store(0, std::memory_order_relaxed);
    droppedFrameCount.store(0, std::memory_order_relaxed);
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
