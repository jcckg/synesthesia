#include "fft_processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

FFTProcessor::FFTProcessor()
	: fft_in(FFT_SIZE),
	  fft_out(FFT_SIZE / 2 + 1),
	  hannWindow(FFT_SIZE),
	  overlapBuffer(FFT_SIZE, 0.0f),
	  windowBuffer(FFT_SIZE, 0.0f),
	  inputAccumulator(HOP_SIZE, 0.0f),
	  accumulatedSamples(0),
	  magnitudesBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  rawMagnitudesBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  processedMagnitudesBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  spectralEnvelope(FFT_SIZE / 2 + 1, 0.0f),
	  phaseBuffer(FFT_SIZE / 2 + 1, 0.0f),
	  currentLoudness(0.0f),
	  momentaryLoudnessLUFS(-200.0f),
	  totalEnergy(0.0f),
	  maxMagnitude(0.0f),
	  spectralFlux(0.0f),
	  onsetDetected(false),
	  previousMagnitudes(FFT_SIZE / 2 + 1, 0.0f),
	  fluxHistory(FLUX_HISTORY_SIZE, 0.0f),
	  fluxHistoryIndex(0),
	  frameCounter(0),
	  criticalBandSmoothingEnabled(true) {
	fft_cfg = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
	if (!fft_cfg) {
		throw std::runtime_error("Error allocating FFTR configuration.");
	}

	frameRingBuffer.resize(FRAME_BUFFER_SIZE);
	for (auto& frame : frameRingBuffer) {
		frame.magnitudes.resize(FFT_SIZE / 2 + 1);
		frame.phases.resize(FFT_SIZE / 2 + 1);
	}

	// Hann window (also called Hanning window) - symmetric variant
	// Formula: w[n] = 0.5 * (1 - cos(2π * n / (N-1)))
	// Reduces spectral leakage in FFT analysis by smoothly tapering signal to zero at edges
	// Named after Austrian meteorologist Julius von Hann
	// https://en.wikipedia.org/wiki/Hann_function
	for (size_t i = 0; i < hannWindow.size(); ++i) {
		hannWindow[i] =
			0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (FFT_SIZE - 1)));
	}
}

FFTProcessor::~FFTProcessor() {
	if (fft_cfg) {
		kiss_fftr_free(fft_cfg);
		fft_cfg = nullptr;
	}
}

void FFTProcessor::setEQGains(const float low, const float mid, const float high) {
	equaliser.setGains(low, mid, high);
}

float FFTProcessor::calculateMelWeight(const float frequency) {
	if (frequency < MEL_SCALE_TRANSITION_FREQ) {
		return MEL_LINEAR_WEIGHT;
	}
	constexpr float ln_6_4 = 1.856298f; // ln(6.4) with full precision
	const float derivative = MEL_LOG_NUMERATOR / (frequency * ln_6_4);
	constexpr float derivative_at_1000 = MEL_LOG_NUMERATOR / (MEL_SCALE_TRANSITION_FREQ * ln_6_4);
	const float melWeight = MEL_LINEAR_WEIGHT * (derivative / derivative_at_1000);
	return std::max(MEL_MIN_WEIGHT, melWeight);
}

void FFTProcessor::applyWindow(const std::span<const float> buffer) {
	const size_t copySize = std::min(buffer.size(), static_cast<size_t>(FFT_SIZE));
	std::ranges::fill(fft_in, 0.0f);

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

void FFTProcessor::processBuffer(const std::span<const float> buffer, const float sampleRate) {
	if (sampleRate <= 0.0f || buffer.empty())
		return;
	std::lock_guard processingLock(processingMutex);

	if (criticalBands.empty()) {
		initialiseCriticalBands(sampleRate);
	}

	size_t bufferPos = 0;
	while (bufferPos < buffer.size()) {
		const size_t samplesNeeded = HOP_SIZE - accumulatedSamples;
		const size_t samplesAvailable = buffer.size() - bufferPos;
		const size_t samplesToCopy = std::min(samplesNeeded, samplesAvailable);

		const std::span<const float> incoming(buffer.data() + bufferPos, samplesToCopy);
		loudnessMeter.processSamples(incoming, sampleRate);

		std::copy_n(buffer.begin() + static_cast<std::ptrdiff_t>(bufferPos), samplesToCopy,
					inputAccumulator.begin() + static_cast<std::ptrdiff_t>(accumulatedSamples));
		accumulatedSamples += samplesToCopy;
		bufferPos += samplesToCopy;

		if (accumulatedSamples == HOP_SIZE) {
			processOverlappingWindow(sampleRate);
			accumulatedSamples = 0;
		}
	}
}

// Applies energy-preserving FFT normalisation
// DC and Nyquist bins get 1/N scaling, positive frequency bins get 2/N
void FFTProcessor::normalizeFFTOutput() {
	// FFT normalisation convention: Scale by 2/N for positive frequencies
	// DC (bin 0) and Nyquist (bin N/2) get additional 0.5x since they lack complex conjugates
	// This gives energy-preserving normalisation: Parseval's theorem holds for magnitude²
	// Reference: KissFFT uses unnormalized FFT, so we apply 1/N scaling here
	// Positive frequency bins: 2/N (to account for negative frequencies folded in real FFT)
	// DC and Nyquist bins: 1/N (no negative frequency counterpart)
	constexpr float scaleFactor = 2.0f / FFT_SIZE;
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

float FFTProcessor::updateLoudnessMetrics() {
	const float lufs = loudnessMeter.getMomentaryLoudness();
	momentaryLoudnessLUFS = lufs;
	return std::clamp((lufs + LUFS_NORMALISATION_OFFSET) / LUFS_NORMALISATION_OFFSET, 0.0f, 1.0f);
}

void FFTProcessor::updateSpectralData(const std::vector<float>& rawMagnitudes,
									  const float sampleRate, const float frameMaxMagnitude,
									  const float frameTotalEnergy, const float normalisedLoudness) {
	std::lock_guard lock(dataMutex);

	rawMagnitudesBuffer = rawMagnitudes;
	magnitudesBuffer = processedMagnitudesBuffer;
	currentLoudness = currentLoudness * (1.0f - LOUDNESS_SMOOTHING) + normalisedLoudness * LOUDNESS_SMOOTHING;
	this->totalEnergy = frameTotalEnergy;
	this->maxMagnitude = frameMaxMagnitude;
	++frameCounter;

	pushFrameToBuffer(rawMagnitudesBuffer, phaseBuffer, sampleRate);
}

void FFTProcessor::processOverlappingWindow(const float sampleRate) {
	std::copy(overlapBuffer.begin(), overlapBuffer.begin() + HOP_SIZE, windowBuffer.begin());
	std::copy(inputAccumulator.begin(), inputAccumulator.begin() + HOP_SIZE,
			  windowBuffer.begin() + HOP_SIZE);
	std::copy(windowBuffer.begin() + HOP_SIZE, windowBuffer.end(), overlapBuffer.begin());

	applyWindow(windowBuffer);
	kiss_fftr(fft_cfg, fft_in.data(), fft_out.data());
	normalizeFFTOutput();

	const size_t binCount = fft_out.size();
	std::vector rawMagnitudes(binCount, 0.0f);
	float frameMaxMagnitude = 0.0f;
	float frameTotalEnergy = 0.0f;

	calculateMagnitudes(rawMagnitudes, sampleRate, frameMaxMagnitude, frameTotalEnergy);
	calculatePhases();

	const float normalisedLoudness = updateLoudnessMetrics();

	if (processedMagnitudesBuffer.size() != binCount) {
		processedMagnitudesBuffer.resize(binCount, 0.0f);
	}
	std::ranges::fill(processedMagnitudesBuffer, 0.0f);
	processMagnitudes(processedMagnitudesBuffer, sampleRate, frameMaxMagnitude);

	calculateSpectralFluxAndOnset(processedMagnitudesBuffer);

	updateSpectralData(rawMagnitudes, sampleRate, frameMaxMagnitude, frameTotalEnergy, normalisedLoudness);
}

std::vector<float> FFTProcessor::getSpectralEnvelope() const {
	std::lock_guard lock(dataMutex);
	return spectralEnvelope;
}

std::vector<float> FFTProcessor::getMagnitudesBuffer() const {
	std::lock_guard lock(dataMutex);
	return magnitudesBuffer;
}

std::vector<float> FFTProcessor::getPhaseBuffer() const {
	std::lock_guard lock(dataMutex);
	return phaseBuffer;
}

void FFTProcessor::processMagnitudes(std::vector<float>& magnitudes, const float sampleRate,
									 const float referenceMaxMagnitude) {
	const float normalisationFactor =
		referenceMaxMagnitude > MAGNITUDE_EPSILON ? 1.0f / referenceMaxMagnitude : 1.0f;

	std::ranges::fill(spectralEnvelope, 0.0f);

	const size_t minBinIndex = std::max(static_cast<size_t>(1), static_cast<size_t>(MIN_FREQ * FFT_SIZE / sampleRate));
	const size_t maxBinIndex =
		std::min(static_cast<size_t>(MAX_FREQ * FFT_SIZE / sampleRate) + 1, fft_out.size() - 1);
	float envelopeEnergy = 0.0f;
	for (size_t i = minBinIndex; i <= maxBinIndex; ++i) {
		const float energy = fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i;
		spectralEnvelope[i] = energy;
		envelopeEnergy += energy;
	}

	if (envelopeEnergy > MAGNITUDE_EPSILON) {
		for (size_t i = minBinIndex; i <= maxBinIndex; ++i) {
			spectralEnvelope[i] /= envelopeEnergy;
		}
	}

	for (size_t i = minBinIndex; i <= maxBinIndex; ++i) {
		magnitudes[i] =
			std::sqrt(fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i) * normalisationFactor;
	}

	// Apply mel-scale perceptual weighting to magnitudes.
	// Mel-scale reflects human pitch perception: linear below 1 kHz, logarithmic above.
	// The derivative dm/df gives perceptual resolution, emphasising frequencies where
	// humans have better pitch discrimination (lower frequencies).
	//
	// Slaney mel-scale formula (Auditory Toolbox, 1998):
	//   m(f) = 3f/200              for f < 1000 Hz (linear region)
	//   m(f) = 15 + 27×log₆.₄(f/1000)  for f ≥ 1000 Hz (logarithmic region)
	//
	// Reference: Malcolm Slaney, "Auditory Toolbox: A MATLAB Toolbox for Auditory
	// Modeling Work", Technical Report #1998-010, Interval Research Corporation, 1998.
	// https://engineering.purdue.edu/~malcolm/apple/tr45/AuditoryToolboxTechReport.pdf
	for (size_t i = minBinIndex; i <= maxBinIndex; ++i) {
		const float freq = static_cast<float>(i) * sampleRate / FFT_SIZE;
		const float melWeight = calculateMelWeight(freq);
		magnitudes[i] *= melWeight;
	}

	equaliser.applyEQ(magnitudes, sampleRate, FFT_SIZE);

	applyCriticalBandSmoothing(magnitudes);
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
			const float freq = static_cast<float>(i) * sampleRate / FFT_SIZE;
			if (freq < MIN_FREQ || freq > MAX_FREQ) {
				rawMagnitudes[i] = 0.0f;
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
			const float freq = static_cast<float>(i) * sampleRate / FFT_SIZE;
			if (freq < MIN_FREQ || freq > MAX_FREQ) {
				rawMagnitudes[i] = 0.0f;
				continue;
			}

			outTotalEnergy += rawMagnitudes[i] * rawMagnitudes[i];
			outMaxMagnitude = std::max(outMaxMagnitude, rawMagnitudes[i]);
		}
	} else
#endif
	{
		for (size_t i = 1; i < fft_out.size() - 1; ++i) {
			if (const float freq = static_cast<float>(i) * sampleRate / FFT_SIZE;
				freq < MIN_FREQ || freq > MAX_FREQ)
				continue;

			const float magnitudeSquared = fft_out[i].r * fft_out[i].r + fft_out[i].i * fft_out[i].i;
			const float magnitude = std::sqrt(magnitudeSquared);
			rawMagnitudes[i] = magnitude;
			outTotalEnergy += magnitudeSquared;
			outMaxMagnitude = std::max(outMaxMagnitude, magnitude);
		}
	}
}

void FFTProcessor::calculatePhases() {
	for (size_t i = 0; i < fft_out.size(); ++i) {
		phaseBuffer[i] = std::atan2(fft_out[i].i, fft_out[i].r);
	}
}

void FFTProcessor::reset() {
	std::lock_guard processingLock(processingMutex);
	std::lock_guard dataLock(dataMutex);
	std::lock_guard bufferLock(frameBufferMutex);

	std::ranges::fill(magnitudesBuffer, 0.0f);
	std::ranges::fill(rawMagnitudesBuffer, 0.0f);
	std::ranges::fill(spectralEnvelope, 0.0f);
	std::ranges::fill(overlapBuffer, 0.0f);
	std::ranges::fill(inputAccumulator, 0.0f);
	accumulatedSamples = 0;
	frameCounter = 0;
	loudnessMeter.reset();
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

// SuperFlux: Böck & Widmer (2013) - maximum filter for onset detection
void FFTProcessor::calculateSpectralFluxAndOnset(const std::vector<float>& currentMagnitudes) {
	if (currentMagnitudes.size() != previousMagnitudes.size()) {
		previousMagnitudes = currentMagnitudes;
		spectralFlux = 0.0f;
		onsetDetected = false;
		return;
	}

	float flux = 0.0f;
	for (size_t i = 0; i < currentMagnitudes.size(); ++i) {
		const float diff = currentMagnitudes[i] - previousMagnitudes[i];
		flux += std::max(diff, 0.0f);
	}

	flux /= static_cast<float>(currentMagnitudes.size());

	fluxHistory[fluxHistoryIndex] = flux;
	fluxHistoryIndex = (fluxHistoryIndex + 1) % FLUX_HISTORY_SIZE;

	float maxFlux = 0.0f;
	for (const float histFlux : fluxHistory) {
		maxFlux = std::max(maxFlux, histFlux);
	}

	const float threshold = maxFlux * ONSET_THRESHOLD_MULTIPLIER;
	onsetDetected = flux > threshold && flux > 0.01f;

	spectralFlux = flux;
	previousMagnitudes = currentMagnitudes;
}

// Glasberg & Moore (1990) - ERB: Equivalent Rectangular Bandwidth
// ERB(f) = 24.7 * (4.37 * f/1000 + 1)
float FFTProcessor::calculateERB(const float frequency) {
	return 24.7f * (4.37f * frequency / 1000.0f + 1.0f);
}

// Convert frequency to ERB-number scale (perceptual frequency scale)
float FFTProcessor::frequencyToERBScale(const float frequency) {
	return 21.4f * std::log10(4.37f * frequency / 1000.0f + 1.0f);
}

// Convert ERB-number back to frequency
float FFTProcessor::erbScaleToFrequency(const float erbScale) {
	return (std::pow(10.0f, erbScale / 21.4f) - 1.0f) * 1000.0f / 4.37f;
}

void FFTProcessor::setCriticalBandSmoothingEnabled(const bool enabled) {
	std::lock_guard<std::mutex> lock(processingMutex);
	criticalBandSmoothingEnabled = enabled;
}

void FFTProcessor::initialiseCriticalBands(const float sampleRate) {
	criticalBands.clear();

	const float nyquist = sampleRate / 2.0f;
	const size_t numBins = FFT_SIZE / 2 + 1;
	const float binSize = sampleRate / static_cast<float>(FFT_SIZE);

	const float minERB = frequencyToERBScale(MIN_FREQ);
	const float maxERB = frequencyToERBScale(std::min(MAX_FREQ, nyquist));

	constexpr size_t TARGET_NUM_BANDS = 32;
	const float erbStep = (maxERB - minERB) / static_cast<float>(TARGET_NUM_BANDS);

	for (size_t i = 0; i < TARGET_NUM_BANDS; ++i) {
		const float centerERB = minERB + (static_cast<float>(i) + 0.5f) * erbStep;
		const float centerFreq = erbScaleToFrequency(centerERB);
		const float erbWidth = calculateERB(centerFreq);

		const float lowerFreq = std::max(MIN_FREQ, centerFreq - erbWidth / 2.0f);
		const float upperFreq = std::min(nyquist, centerFreq + erbWidth / 2.0f);

		const auto startBin = static_cast<size_t>(lowerFreq / binSize);
		const auto endBin = static_cast<size_t>(upperFreq / binSize);

		if (startBin < numBins && endBin < numBins && startBin < endBin) {
			CriticalBand band;
			band.centerFrequency = centerFreq;
			band.lowerFreq = lowerFreq;
			band.upperFreq = upperFreq;
			band.startBin = startBin;
			band.endBin = endBin;
			band.smoothingFactor = calculatePsychoacousticSmoothingFactor(centerFreq);
			band.smoothedMagnitude = 0.0f;
			band.rawMagnitude = 0.0f;

			criticalBands.push_back(band);
		}
	}
}

// Fletcher (1940), Moore (2012) - frequency-dependent temporal integration
// Frequency-dependent smoothing for psychoacoustic critical band averaging
// Smoothing factors empirically tuned based on:
// - Fletcher (1940): Auditory masking patterns and critical bands
// - Moore (2012): An Introduction to the Psychology of Hearing
// - Perceptual experiments: heavier smoothing at low frequencies matches auditory frequency discrimination
//
// Rationale:
// - Low frequencies (<100Hz): 3x smoothing - poor frequency resolution, wide ERB bands
// - 100-500Hz: 3x → 1x transition - improving frequency discrimination
// - 500-2000Hz: 1x (base) - optimal frequency discrimination range
// - >2000Hz: 1x → 0.25x - excellent discrimination, wider ERB bands but sharper perceptual resolution
float FFTProcessor::calculatePsychoacousticSmoothingFactor(const float frequency) const {
	constexpr float BASE_SMOOTHING = 0.2f;

	if (frequency < 100.0f) {
		return BASE_SMOOTHING * 3.0f;
	}
	if (frequency < 500.0f) {
		const float t = (frequency - 100.0f) / 400.0f;
		return BASE_SMOOTHING * (3.0f - 2.0f * t);
	}
	if (frequency < 2000.0f) {
		return BASE_SMOOTHING;
	}

	const float t = std::min((frequency - 2000.0f) / 8000.0f, 1.0f);
	return BASE_SMOOTHING * (1.0f - 0.75f * t);
}

void FFTProcessor::applyCriticalBandSmoothing(std::vector<float>& magnitudes) {
	if (!criticalBandSmoothingEnabled || criticalBands.empty()) {
		return;
	}

	for (auto& band : criticalBands) {
		float bandEnergy = 0.0f;
		size_t count = 0;

		for (size_t bin = band.startBin; bin <= band.endBin && bin < magnitudes.size(); ++bin) {
			const float mag = magnitudes[bin];
			bandEnergy += mag * mag * mag;
			++count;
		}

		if (count > 0) {
			band.rawMagnitude = std::cbrt(bandEnergy / static_cast<float>(count));
		}

		const float alpha = 1.0f - band.smoothingFactor;
		band.smoothedMagnitude = alpha * band.rawMagnitude + band.smoothingFactor * band.smoothedMagnitude;

		for (size_t bin = band.startBin; bin <= band.endBin && bin < magnitudes.size(); ++bin) {
			const float originalMag = magnitudes[bin];
			if (band.rawMagnitude > 1e-6f) {
				const float scale = band.smoothedMagnitude / band.rawMagnitude;
				magnitudes[bin] = originalMag * scale;
			} else {
				magnitudes[bin] = band.smoothedMagnitude;
			}
		}
	}
}
