#include "audio_processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include "utilities/cancellation.h"

namespace {
double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

AudioProcessor::AudioProcessor()
	: writeIndex(0), readIndex(0), running(false), currentSpectralData{} {
	fftProcessors.push_back(std::make_unique<FFTProcessor>(FFTProcessor::FrameDelivery::Direct));
	activeChannelCount = 1;
}

AudioProcessor::~AudioProcessor() { stop(); }

void AudioProcessor::setCaptureActive(bool active) {
    { std::lock_guard lock(wakeMutex); captureActive.store(active, std::memory_order_release); }
    wake.notify_all();
}

int AudioProcessor::liveHopSize(const float sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 768000) return FFTProcessor::HOP_SIZE;
    return FFTProcessor::hopSizeForSampleRate(sampleRate, static_cast<int>(std::lround(sampleRate / 120.0f)));
}

void AudioProcessor::start() {
	if (running.exchange(true))
		return;
	writeIndex = 0;
	readIndex = 0;
    sourceFramePosition = 0;
    capturedFrameCount = 0;
    processedFrameCount = 0;
    previousCallbackTime = 0;
    callbackDistribution.reset(); queueDistribution.reset(); processingDistribution.reset(); displayDistribution.reset();
    callbackCount = 0; overflowCount = 0; underflowCount = 0; callbackFrames = 0;
    callbackInterval = 0; captureDeliveryAge = 0; queueAge = 0; processingTime = 0;
    displayedFrameAge = 0; analysisToDisplayAge = 0; captureTimingAvailable = false;

    workerActive.store(true, std::memory_order_release);
    try {
        workerThread = std::thread([this] {
            processingThreadFunc();
            workerActive.store(false, std::memory_order_release);
        });
    } catch (...) {
        workerActive.store(false, std::memory_order_release);
        running.store(false, std::memory_order_release);
        throw;
    }
}

void AudioProcessor::stop() {
	if (!running.exchange(false))
		return;

	{ std::lock_guard lock(wakeMutex); }
    wake.notify_all();
	if (workerThread.joinable()) {
		workerThread.join();
	}
}

// https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html
void AudioProcessor::queueAudioData(const float* buffer, const size_t numSamples,
    const float sampleRate, const size_t numChannels, const AudioCaptureTiming& timing) {
    if (!running.load(std::memory_order_acquire) || numChannels == 0 ||
        !std::isfinite(sampleRate) || sampleRate < 8000.0f) return;
    const size_t frames = numSamples / numChannels;
    const double now = monotonicSeconds();
    callbackCount.fetch_add(1, std::memory_order_relaxed);
    callbackFrames.store(frames, std::memory_order_relaxed);
    callbackInterval.store(previousCallbackTime > 0 ? std::max(0.0, now - previousCallbackTime) : 0, std::memory_order_relaxed);
    if (previousCallbackTime > 0) callbackDistribution.record(now - previousCallbackTime);
    previousCallbackTime = now;
    if (timing.overflow) overflowCount.fetch_add(1, std::memory_order_relaxed);
    if (timing.underflow) underflowCount.fetch_add(1, std::memory_order_relaxed);
    const bool timed = std::isfinite(timing.adcTime) && std::isfinite(timing.callbackTime) && timing.callbackTime >= timing.adcTime;
    const double captureStart = timed ? now + timing.adcTime - timing.callbackTime : 0;
    captureTimingAvailable.store(timed, std::memory_order_relaxed);
    captureDeliveryAge.store(timed ? std::max(0.0, now - captureStart - static_cast<double>(frames) / static_cast<double>(sampleRate)) : 0, std::memory_order_relaxed);
    if (!buffer) return;
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
        queued.queuedTime = now;
        queued.captureEndTime = timed ? captureStart + static_cast<double>(copied + count) / static_cast<double>(sampleRate) : 0;
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
            if (!captureActive.load(std::memory_order_acquire)) {
                std::unique_lock lock(wakeMutex);
                wake.wait(lock, [&] { return !running.load(std::memory_order_acquire) || captureActive.load(std::memory_order_acquire); });
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto& buffer = audioQueue[current];
        if (previousChannels == 0) {
            previousChannels = buffer.numChannels;
            previousRate = buffer.sampleRate;
        }
        fillGap(buffer.firstFrame);
        const double started = monotonicSeconds();
        queueAge.store(std::max(0.0, started - buffer.queuedTime), std::memory_order_relaxed);
        queueDistribution.record(std::max(0.0, started - buffer.queuedTime));
        processBuffer(buffer);
        processingDistribution.record(monotonicSeconds() - started);
        processingTime.store(monotonicSeconds() - started, std::memory_order_relaxed);
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
    RSYNPresentationSettings settings;
    {
        std::lock_guard settingsLock(presentationSettingsMutex);
        settings = pendingPresentationSettings;
    }
    livePresentation.setSettings(settings);
    const int requested = requestedChannel.load(std::memory_order_relaxed);
    const int selected = requested >= 0 && static_cast<std::size_t>(requested) < buffer.numChannels ? requested : -1;
    if (selected != presentationChannel) {
        livePresentation.reset(true);
        presentationChannel = selected;
    }
    const bool formatChanged = liveAnalysisRate != buffer.sampleRate || activeChannelCount != buffer.numChannels;
    if (formatChanged) {
        for (auto& processor : fftProcessors) processor->reset();
        analysisFramePosition = 0;
        liveAnalysisRate = buffer.sampleRate;
    }
	ensureProcessorCountLocked(buffer.numChannels);
	activeChannelCount = buffer.numChannels;
    if (formatChanged) {
        const int hop = liveHopSize(buffer.sampleRate);
        for (std::size_t ch = 0; ch < buffer.numChannels; ++ch) fftProcessors[ch]->setHopSize(hop);
    }

	const size_t frames = buffer.sampleCount / buffer.numChannels;
    analysisFramePosition += frames;
	channelBufferScratch.resize(frames);
    presentationFrames.resize(buffer.numChannels);
    for (auto& channel : presentationFrames) channel.clear();

	for (size_t ch = 0; ch < buffer.numChannels; ++ch) {
		for (size_t i = 0; i < frames; ++i) {
			channelBufferScratch[i] = buffer.data[i * buffer.numChannels + ch];
		}

        if (recordingAnalysis) {
            while (recordingProcessors.size() < buffer.numChannels) {
                auto processor = std::make_unique<FFTProcessor>(FFTProcessor::FrameDelivery::Direct);
                processor->setHopSize(recordingHop);
                recordingProcessors.push_back(std::move(processor));
            }
            recordingPendingFrames.resize(buffer.numChannels);
            recordingProcessors[ch]->processBuffer(std::span(channelBufferScratch.data(), frames), buffer.sampleRate,
                &recordingPendingFrames[ch]);
        }
        fftProcessors[ch]->processBuffer(std::span(channelBufferScratch.data(), frames), buffer.sampleRate, &presentationFrames[ch]);
    }
    if (recordingAnalysis) {
        recordingSampleCount += frames;
        recordingRate = buffer.sampleRate;
    }
    std::size_t completed = presentationFrames.empty() ? 0 : presentationFrames.front().size();
    for (const auto& channel : presentationFrames) completed = std::min(completed, channel.size());
    if (completed == 0 && latestPresentation) return;

    stagingSpectralData.magnitudes.resize(buffer.numChannels);
    stagingSpectralData.phases.resize(buffer.numChannels);
    float maxDominantFreq = 0.0f;
    float maxMagnitudeVal = 0.0f;
    FFTProcessor::AnalysisState primaryAnalysis{};
    bool hasPrimaryAnalysis = false;
    auto& channelLoudness = channelLoudnessScratch;
    channelLoudness.assign(buffer.numChannels, -200.0f);
    for (size_t ch = 0; ch < buffer.numChannels; ++ch) {
        FFTProcessor::AnalysisState analysis{};
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

    for (std::size_t i = 0; i < completed; ++i) {
        AudioColourSample sample;
        sample.sampleRate = buffer.sampleRate;
        const auto channelCount = selected < 0 ? buffer.numChannels : std::size_t{1};
        sample.channels = static_cast<std::uint32_t>(channelCount);
        const auto& frame = presentationFrames[0][i];
        const auto fftSize = 2 * (frame.magnitudes.size() - 1);
        sample.timestamp = (static_cast<double>(frame.frameCounter) * static_cast<double>(frame.hopSize) - static_cast<double>(fftSize) * .5) / static_cast<double>(buffer.sampleRate);
        sample.magnitudes.resize(channelCount);
        sample.phases.resize(channelCount);
        sample.bass.resize(channelCount);
        sample.kWeightedMagnitudes.resize(channelCount);
        for (std::size_t ch = 0; ch < channelCount; ++ch) {
            const auto source = selected < 0 ? ch : static_cast<std::size_t>(selected);
            sample.magnitudes[ch] = std::move(presentationFrames[source][i].magnitudes);
            sample.phases[ch] = std::move(presentationFrames[source][i].phases);
            sample.bass[ch] = presentationFrames[source][i].bass;
            sample.kWeightedMagnitudes[ch] = std::move(presentationFrames[source][i].kWeightedMagnitudes);
            if (selected < 0) channelLoudness[ch] = presentationFrames[source][i].loudnessLUFS;
        }
        sample.loudnessLUFS = selected < 0 ? LoudnessMeter::combineChannelLoudness(channelLoudness) : presentationFrames[static_cast<std::size_t>(selected)][i].loudnessLUFS;
        const auto& result = livePresentation.process(sample);
        if (i + 1 == completed) {
            auto published = std::make_shared<SpectralPresentation::LiveResult>(result);
            published->selectedChannel = selected;
            if (buffer.captureEndTime > 0) {
                const double centre = static_cast<double>(frame.frameCounter) * frame.hopSize - static_cast<double>(fftSize) * .5;
                published->captureCentreTime = buffer.captureEndTime + (centre - static_cast<double>(analysisFramePosition)) / static_cast<double>(buffer.sampleRate);
            }
            published->analysisCompletedTime = monotonicSeconds();
            latestPresentation = std::move(published);
        }
    }
    stagingSpectralData.presentation = latestPresentation;

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

AudioProcessor::TimingStatistics AudioProcessor::getTimingStatistics() const {
    TimingStatistics stats;
    stats.callbacks = callbackCount.load(std::memory_order_relaxed);
    stats.overflows = overflowCount.load(std::memory_order_relaxed);
    stats.underflows = underflowCount.load(std::memory_order_relaxed);
    stats.droppedFrames = droppedFrameCount.load(std::memory_order_relaxed);
    stats.callbackFrames = callbackFrames.load(std::memory_order_relaxed);
    stats.callbackInterval = callbackInterval.load(std::memory_order_relaxed);
    stats.captureDeliveryAge = captureDeliveryAge.load(std::memory_order_relaxed);
    stats.queueAge = queueAge.load(std::memory_order_relaxed);
    stats.processingTime = processingTime.load(std::memory_order_relaxed);
    stats.displayedFrameAge = displayedFrameAge.load(std::memory_order_relaxed);
    stats.analysisToDisplayAge = analysisToDisplayAge.load(std::memory_order_relaxed);
    stats.captureTimingAvailable = captureTimingAvailable.load(std::memory_order_relaxed);
    stats.callbackDistribution = callbackDistribution.snapshot();
    stats.queueDistribution = queueDistribution.snapshot();
    stats.processingDistribution = processingDistribution.snapshot();
    stats.displayDistribution = displayDistribution.snapshot();
    return stats;
}

void AudioProcessor::markPresentationDisplayed(const SpectralPresentation::LiveResult& frame) {
    const double now = monotonicSeconds();
    if (frame.captureCentreTime > 0) displayDistribution.record(std::max(0.0, now - frame.captureCentreTime));
    displayedFrameAge.store(frame.captureCentreTime > 0 ? std::max(0.0, now - frame.captureCentreTime) : 0, std::memory_order_relaxed);
    analysisToDisplayAge.store(frame.analysisCompletedTime > 0 ? std::max(0.0, now - frame.analysisCompletedTime) : 0, std::memory_order_relaxed);
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
	std::lock_guard processorLock(processorMutex);
	for (auto& processor : fftProcessors) processor->discardBufferedFrames();
}

void AudioProcessor::beginRecordingAnalysis(const int hopSize) {
    std::lock_guard lock(processorMutex);
    recordingProcessors.clear(); recordingSampleCount = 0; recordingRate = 0.0f;
    recordingPendingFrames.clear();
    recordingHop = std::max(0, hopSize);
    ++recordingGeneration;
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
    std::uint64_t generation;
    {
        std::lock_guard lock(processorMutex);
        Utilities::checkCancellation();
        generation = recordingGeneration;
    }
    const auto end = capturedFrameCount.load(std::memory_order_acquire);
    try {
        while (workerActive.load(std::memory_order_acquire) && processedFrameCount.load(std::memory_order_acquire) < end) {
            Utilities::checkCancellation();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Utilities::checkCancellation();
    } catch (...) {
        std::lock_guard lock(processorMutex);
        if (generation == recordingGeneration) {
            ++recordingGeneration;
            recordingAnalysis = false;
            recordingProcessors.clear();
            recordingPendingFrames.clear();
        }
        throw;
    }
    std::lock_guard lock(processorMutex);
    if (generation != recordingGeneration) { count = 0; rate = 0; return {}; }
    recordingAnalysis = false;
    count = recordingSampleCount;
    rate = recordingRate;
    auto frames = drainRecordingFramesLocked(true);
    recordingProcessors.clear();
    return frames;
}

void AudioProcessor::cancelRecordingAnalysis() {
    std::lock_guard lock(processorMutex);
    ++recordingGeneration;
    recordingAnalysis = false;
    recordingProcessors.clear();
    recordingPendingFrames.clear();
}

void AudioProcessor::setPresentationSettings(const RSYNPresentationSettings& settings) {
    std::lock_guard lock(presentationSettingsMutex);
    pendingPresentationSettings = settings;
}

void AudioProcessor::reset(bool preserveColour) {
	std::lock_guard processorLock(processorMutex);
    livePresentation.reset(preserveColour);
    latestPresentation.reset();
    analysisFramePosition = 0;
    liveAnalysisRate = 0;
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
		auto processor = std::make_unique<FFTProcessor>(FFTProcessor::FrameDelivery::Direct);
		fftProcessors.push_back(std::move(processor));
	}
}

FFTProcessor* AudioProcessor::getProcessorForChannel(const size_t channel) {
	if (fftProcessors.empty()) {
		fftProcessors.push_back(std::make_unique<FFTProcessor>(FFTProcessor::FrameDelivery::Direct));
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
