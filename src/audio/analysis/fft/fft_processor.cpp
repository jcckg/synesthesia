#include "fft_processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>
#include <stdexcept>

FFTProcessor::FFTProcessor()
	: fft_in(FFT_SIZE),
	  fft_out(FFT_SIZE / 2 + 1),
	  hannWindow(FFT_SIZE),
	  windowBuffer(FFT_SIZE, 0.0f),
	  inputAccumulator(HOP_SIZE, 0.0f),
      weightedAccumulator(HOP_SIZE, 0.0f),
      weightedWindow(FFT_SIZE, 0.0f),
      weightedMagnitudes(FFT_SIZE / 2 + 1, 0.0f),
	  accumulatedSamples(0),
	  analysisHopSize(HOP_SIZE),
	  overlapSize(FFT_SIZE - HOP_SIZE),
	  rawMagnitudesBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  phaseBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  momentaryLoudnessLUFS(-200.0f),
	  totalEnergy(0.0f),
	  maxMagnitude(0.0f),
	  frameCounter(0) {
    magnitudeScratch.resize(FFT_SIZE / 2 + 1);

	frameRingBuffer.resize(FRAME_BUFFER_SIZE);
	for (auto& frame : frameRingBuffer) {
		frame.magnitudes.resize(FFT_SIZE / 2 + 1);
		frame.phases.resize(FFT_SIZE / 2 + 1);
        frame.kWeightedMagnitudes.resize(FFT_SIZE / 2 + 1);
	}

	// https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hann.html
	for (size_t i = 0; i < hannWindow.size(); ++i) {
		hannWindow[i] =
			0.5f * (1.0f - std::cos(
				2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
				static_cast<float>(FFT_SIZE - 1)));
	}
}

FFTProcessor::~FFTProcessor() = default;

uint64_t FFTProcessor::getFrameCounter() const {
	std::lock_guard lock(dataMutex);
	return frameCounter;
}

int FFTProcessor::fftSizeForSampleRate(const float sampleRate) {
    int size = FFT_SIZE;
    if (!std::isfinite(sampleRate)) return size;
    while (size < MAX_FFT_SIZE && static_cast<double>(sampleRate) > 48000.0 * size / FFT_SIZE) size *= 2;
    return size;
}

int FFTProcessor::hopSizeForSampleRate(const float sampleRate, const int requestedHopSize) {
    const int size = fftSizeForSampleRate(sampleRate);
    return requestedHopSize > 0 ? std::clamp(requestedHopSize, 1, size / 2) : size / 2;
}

void FFTProcessor::configureAnalysis(const float sampleRate) {
    const auto size = static_cast<std::size_t>(fftSizeForSampleRate(sampleRate));
    const auto hop = static_cast<std::size_t>(hopSizeForSampleRate(sampleRate, requestedHopSize));
    if (fft_in.size() != size) transform = std::make_unique<AudioAnalysis::RealFFT>(size);
    {
        std::lock_guard dataLock(dataMutex);
        std::lock_guard bufferLock(frameBufferMutex);
        fft_in.assign(size, 0);
        fft_out.assign(size / 2 + 1, {});
        hannWindow.resize(size);
        for (std::size_t i = 0; i < size; ++i)
            hannWindow[i] = .5f * (1 - std::cos(2 * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(size - 1)));
        windowBuffer.assign(size, 0);
        weightedWindow.assign(size, 0);
        magnitudeScratch.assign(size / 2 + 1, 0);
        rawMagnitudesBuffer.assign(size / 2 + 1, 0);
        phaseBuffer.assign(size / 2 + 1, 0);
        weightedMagnitudes.assign(size / 2 + 1, 0);
        inputAccumulator.assign(hop, 0);
        weightedAccumulator.assign(hop, 0);
        analysisHopSize = hop;
        overlapSize = size - hop;
        for (auto& frame : frameRingBuffer) {
            frame = {};
            frame.magnitudes.resize(size / 2 + 1);
            frame.phases.resize(size / 2 + 1);
            frame.kWeightedMagnitudes.resize(size / 2 + 1);
        }
    }
    resetState();
    analysisSampleRate = sampleRate;
}

void FFTProcessor::setHopSize(const int hopSize) {
    std::lock_guard processingLock(processingMutex);
    const int requested = std::max(0, hopSize);
    if (requested == requestedHopSize) return;
    requestedHopSize = requested;
    configureAnalysis(analysisSampleRate > 0 ? analysisSampleRate : 48000.0f);
}

void FFTProcessor::applyWindow(const std::span<const float> buffer) {
	const size_t copySize = std::min(buffer.size(), fft_in.size());
	std::fill(fft_in.begin()+static_cast<std::ptrdiff_t>(copySize),fft_in.end(),0.f);

#ifdef USE_NEON_OPTIMISATIONS
	if (FFTProcessorNEON::isNEONAvailable() && copySize >= 4) {
		FFTProcessorNEON::applyHannWindow(
			std::span<float>(fft_in.data(), copySize),
			std::span<const float>(buffer.data(), copySize),
			std::span<const float>(hannWindow.data(), copySize)
		);
	} else
#elif defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
	if (FFTProcessorSSE::isSSEAvailable() && copySize >= 4) {
		FFTProcessorSSE::applyHannWindow(
			std::span<float>(fft_in.data(), copySize),
			std::span<const float>(buffer.data(), copySize),
			std::span<const float>(hannWindow.data(), copySize)
		);
	} else
#endif
	{
		for (size_t i = 0; i < copySize; ++i) {
			fft_in[i] = buffer[i] * hannWindow[i];
		}
	}
}

void FFTProcessor::processBuffer(const std::span<const float> buffer, const float sampleRate, std::vector<FFTFrame>* completedFrames) {
	if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 768000.0f || buffer.empty())
		return;
	std::lock_guard processingLock(processingMutex);
    if (finished) return;
	if (analysisSampleRate != sampleRate) {
        configureAnalysis(sampleRate);
	}

	size_t bufferPos = 0;
	while (bufferPos < buffer.size()) {
		const size_t samplesNeeded = analysisHopSize - accumulatedSamples;
		const size_t samplesAvailable = buffer.size() - bufferPos;
		const size_t samplesToCopy = std::min(samplesNeeded, samplesAvailable);

		const std::span<float> incoming(inputAccumulator.data() + accumulatedSamples, samplesToCopy);
		for (size_t i = 0; i < samplesToCopy; ++i) {
			const float value = buffer[bufferPos + i];
			incoming[i] = std::isfinite(value) ? value : 0.0f;
		}
		loudnessMeter.processSamples(incoming, sampleRate,
            std::span(weightedAccumulator).subspan(accumulatedSamples, samplesToCopy));
        bassAnalyser.process(incoming, sampleRate);
		accumulatedSamples += samplesToCopy;
		bufferPos += samplesToCopy;

		if (accumulatedSamples == analysisHopSize) {
			processOverlappingWindow(sampleRate);
            if (completedFrames) {
                std::lock_guard lock(dataMutex);
                FFTFrame frame;
                frame.magnitudes = rawMagnitudesBuffer;
                frame.phases = phaseBuffer;
                frame.frameCounter = frameCounter;
                frame.sampleRate = sampleRate;
                frame.loudnessLUFS = momentaryLoudnessLUFS;
                frame.bass = bassSpectrum;
                frame.kWeightedMagnitudes = weightedMagnitudes;
                frame.hopSize = static_cast<int>(analysisHopSize);
                completedFrames->push_back(std::move(frame));
            }
			accumulatedSamples = 0;
		}
	}
}

// https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.istft.html
std::vector<FFTProcessor::FFTFrame> FFTProcessor::finish() {
    std::lock_guard lock(processingMutex);
    auto frames = getBufferedFrames();
    if (finished || analysisSampleRate <= 0.0f) return frames;
    finished = true;
    // Complete overlap-add frames without advancing loudness or bass beyond the real source samples.
    const size_t padding = overlapSize +
        (analysisHopSize - (accumulatedSamples + overlapSize) % analysisHopSize) % analysisHopSize;
    size_t remaining = padding;
    while (remaining > 0) {
        const size_t count = std::min(remaining, analysisHopSize - accumulatedSamples);
        std::fill_n(inputAccumulator.begin() + static_cast<std::ptrdiff_t>(accumulatedSamples), count, 0.0f);
        std::fill_n(weightedAccumulator.begin() + static_cast<std::ptrdiff_t>(accumulatedSamples), count, 0.0f);
        accumulatedSamples += count;
        remaining -= count;
        if (accumulatedSamples == analysisHopSize) {
            processOverlappingWindow(analysisSampleRate);
            accumulatedSamples = 0;
            auto produced = getBufferedFrames();
            frames.insert(frames.end(), std::make_move_iterator(produced.begin()), std::make_move_iterator(produced.end()));
        }
    }
    return frames;
}

void FFTProcessor::normaliseFFTOutput() {

	const float scaleFactor = 2.0f / static_cast<float>(fft_in.size());
	for (auto& i : fft_out) {
		i.r *= scaleFactor;
		i.i *= scaleFactor;
	}
	fft_out[0].r *= 0.5f;
	fft_out[0].i *= 0.5f;
	if (fft_out.size() > 1) {
		fft_out[fft_out.size() - 1].r *= 0.5f;
		fft_out[fft_out.size() - 1].i *= 0.5f;
	}
}

void FFTProcessor::updateSpectralData(const std::vector<float>& rawMagnitudes,
									  const float sampleRate, const float frameMaxMagnitude,
									  const float frameTotalEnergy) {
	std::lock_guard lock(dataMutex);
	momentaryLoudnessLUFS = loudnessMeter.getMomentaryLoudness();
    bassSpectrum = bassAnalyser.spectrum();

	rawMagnitudesBuffer = rawMagnitudes;
	this->totalEnergy = frameTotalEnergy;
	this->maxMagnitude = frameMaxMagnitude;
	++frameCounter;

	pushFrameToBuffer(rawMagnitudesBuffer, phaseBuffer, sampleRate);
}

void FFTProcessor::processOverlappingWindow(const float sampleRate) {
	if (overlapSize > 0) {
        std::memmove(windowBuffer.data(),windowBuffer.data()+analysisHopSize,overlapSize*sizeof(float));
	}
	std::copy(inputAccumulator.begin(),
			  inputAccumulator.end(),
			  windowBuffer.begin() + static_cast<std::ptrdiff_t>(overlapSize));

	applyWindow(windowBuffer);
	transform->forward(fft_in, fft_out);
	normaliseFFTOutput();

	auto& rawMagnitudes = magnitudeScratch;
	float frameMaxMagnitude = 0.0f;
	float frameTotalEnergy = 0.0f;

	calculateMagnitudes(rawMagnitudes, sampleRate, frameMaxMagnitude, frameTotalEnergy);
	calculatePhases();

    if (overlapSize > 0)
        std::memmove(weightedWindow.data(), weightedWindow.data() + analysisHopSize, overlapSize * sizeof(float));
    std::copy(weightedAccumulator.begin(), weightedAccumulator.end(), weightedWindow.begin() + static_cast<std::ptrdiff_t>(overlapSize));
    applyWindow(weightedWindow);
    transform->forward(fft_in, fft_out);
    normaliseFFTOutput();
    for (std::size_t i = 0; i < fft_out.size(); ++i)
        weightedMagnitudes[i] = std::hypot(fft_out[i].r, fft_out[i].i);

	updateSpectralData(rawMagnitudes, sampleRate, frameMaxMagnitude, frameTotalEnergy);

}

std::vector<float> FFTProcessor::getRawMagnitudesBuffer() const {
	std::lock_guard lock(dataMutex);
	return rawMagnitudesBuffer;
}

std::vector<float> FFTProcessor::getPhaseBuffer() const {
	std::lock_guard lock(dataMutex);
	return phaseBuffer;
}

void FFTProcessor::copyRawFrame(std::vector<float>& rawMagnitudes,
								std::vector<float>& phases,
								AnalysisState& state) const {
	std::lock_guard processingLock(processingMutex);
	std::lock_guard lock(dataMutex);
	rawMagnitudes = rawMagnitudesBuffer;
	phases = phaseBuffer;
	state.frameCounter = frameCounter;
	state.momentaryLoudnessLUFS = momentaryLoudnessLUFS;
	state.totalEnergy = totalEnergy;
	state.maxMagnitude = maxMagnitude;
	state.hopSize = static_cast<int>(analysisHopSize);
    state.fftSize = static_cast<int>(fft_in.size());
}

void FFTProcessor::calculateMagnitudes(std::vector<float>& rawMagnitudes, const float sampleRate,
									   float& outMaxMagnitude, float& outTotalEnergy) const {
	outMaxMagnitude = 0.0f;
	outTotalEnergy = 0.0f;

#ifdef USE_NEON_OPTIMISATIONS
	if (FFTProcessorNEON::isNEONAvailable() && fft_out.size() >= 4) {
		FFTProcessorNEON::calculateMagnitudesFromComplex(
			std::span<float>(rawMagnitudes.data(), rawMagnitudes.size()),
			fft_out.data(), fft_out.size());

		for (size_t i = 1; i < fft_out.size() - 1; ++i) {
			const float freq = static_cast<float>(i) * sampleRate / static_cast<float>(fft_in.size());
			if (freq < MIN_FREQ || freq > MAX_FREQ) {
				continue;
			}

			outTotalEnergy += rawMagnitudes[i] * rawMagnitudes[i];
			outMaxMagnitude = std::max(outMaxMagnitude, rawMagnitudes[i]);
		}
	} else
#elif defined(__SSE__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
	if (FFTProcessorSSE::isSSEAvailable() && fft_out.size() >= 4) {
		FFTProcessorSSE::calculateMagnitudesFromComplex(
			std::span<float>(rawMagnitudes.data(), rawMagnitudes.size()),
			fft_out.data(), fft_out.size());

		for (size_t i = 1; i < fft_out.size() - 1; ++i) {
			const float freq = static_cast<float>(i) * sampleRate / static_cast<float>(fft_in.size());
			if (freq < MIN_FREQ || freq > MAX_FREQ) {
				continue;
			}

			outTotalEnergy += rawMagnitudes[i] * rawMagnitudes[i];
			outMaxMagnitude = std::max(outMaxMagnitude, rawMagnitudes[i]);
		}
	} else
#endif
	{
		for (size_t i = 0; i < fft_out.size(); ++i) {
			const bool endpoint = i == 0 || i + 1 == fft_out.size();
			const float freq = static_cast<float>(i) * sampleRate / static_cast<float>(fft_in.size());
			const float magnitudeSquared = fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i;
			const float magnitude = std::sqrt(magnitudeSquared);
			rawMagnitudes[i] = magnitude;
			if (!endpoint && freq >= MIN_FREQ && freq <= MAX_FREQ) {
				outTotalEnergy += magnitudeSquared;
				outMaxMagnitude = std::max(outMaxMagnitude, magnitude);
			}
		}
	}
}

void FFTProcessor::calculatePhases() {
	std::lock_guard lock(dataMutex);
	for (size_t i = 0; i < fft_out.size(); ++i) {
		phaseBuffer[i] = std::atan2(fft_out[i].i, fft_out[i].r);
	}
}

void FFTProcessor::reset() {
	std::lock_guard processingLock(processingMutex);
	resetState();
}

void FFTProcessor::resetState() {
	std::lock_guard dataLock(dataMutex);
	std::lock_guard bufferLock(frameBufferMutex);

	std::ranges::fill(rawMagnitudesBuffer, 0.0f);
	std::ranges::fill(phaseBuffer, 0.0f);
	totalEnergy = 0.0f;
	maxMagnitude = 0.0f;
	analysisSampleRate = 0.0f;
    finished = false;
	std::ranges::fill(windowBuffer, 0.0f);
	std::ranges::fill(inputAccumulator, 0.0f);
	accumulatedSamples = 0;
	frameCounter = 0;
	loudnessMeter.reset();
    bassAnalyser.reset();
    bassSpectrum.reset();
    std::ranges::fill(weightedAccumulator, 0.0f);
    std::ranges::fill(weightedWindow, 0.0f);
    std::ranges::fill(weightedMagnitudes, 0.0f);
	momentaryLoudnessLUFS = -200.0f;

	frameBufferHead = 0;
	frameBufferTail = 0;
}

void FFTProcessor::pushFrameToBuffer(const std::vector<float>& mags, const std::vector<float>& phases, const float sampleRate) {
	std::lock_guard lock(frameBufferMutex);

	const size_t head = frameBufferHead;
	const size_t tail = frameBufferTail;
	const size_t nextHead = (head + 1) % FRAME_BUFFER_SIZE;

	if (nextHead == tail) {
		frameBufferTail = (tail + 1) % FRAME_BUFFER_SIZE;
		droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
	}

	FFTFrame& frame = frameRingBuffer[head];

	const size_t magsToCopy = std::min(mags.size(), frame.magnitudes.size());
	const size_t phasesToCopy = std::min(phases.size(), frame.phases.size());

	std::copy_n(mags.begin(), magsToCopy, frame.magnitudes.begin());
	std::copy_n(phases.begin(), phasesToCopy, frame.phases.begin());
	frame.frameCounter = frameCounter;
	frame.sampleRate = sampleRate;
	frame.loudnessLUFS = momentaryLoudnessLUFS;
    frame.bass = bassSpectrum;
    frame.kWeightedMagnitudes = weightedMagnitudes;
    frame.hopSize = static_cast<int>(analysisHopSize);

	frameBufferHead = nextHead;
}

std::vector<FFTProcessor::FFTFrame> FFTProcessor::getBufferedFrames() {
	std::lock_guard lock(frameBufferMutex);

	const size_t head = frameBufferHead;
	const size_t tail = frameBufferTail;

	std::vector<FFTFrame> frames;

	if (head == tail) {
		return frames;
	}

	const size_t count = (head >= tail) ? (head - tail) : (FRAME_BUFFER_SIZE - tail + head);
	frames.reserve(count);

	size_t current = tail;
	while (current != head) {
		frames.push_back(frameRingBuffer[current]);
		current = (current + 1) % FRAME_BUFFER_SIZE;
	}

	frameBufferTail = head;

	return frames;
}

int FFTProcessor::getHopSize() const {
	std::lock_guard lock(processingMutex);
	return static_cast<int>(analysisHopSize);
}

int FFTProcessor::getFftSize() const {
    std::lock_guard lock(processingMutex);
    return static_cast<int>(fft_in.size());
}

void FFTProcessor::discardBufferedFrames() {
    std::lock_guard lock(frameBufferMutex);
    frameBufferTail = frameBufferHead;
}

float FFTProcessor::getMomentaryLoudnessLUFS() const {
	std::lock_guard lock(dataMutex);
	return momentaryLoudnessLUFS;
}

float FFTProcessor::getTotalEnergy() const {
	std::lock_guard lock(dataMutex);
	return totalEnergy;
}

float FFTProcessor::getMaxMagnitude() const {
	std::lock_guard lock(dataMutex);
	return maxMagnitude;
}
