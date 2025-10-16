#include "audio_output.h"
#include <algorithm>
#include <cmath>
#include <cstring>

AudioOutput::AudioOutput()
	: stream_(nullptr),
	  audioBuffer_(std::make_shared<std::vector<float>>()),
	  playbackPosition_(0),
	  totalSamples_(0),
	  isPlaying_(false),
	  loopEnabled_(true),
	  requestedSampleRate_(0.0f),
	  actualSampleRate_(0.0f),
	  playbackStep_(1.0f),
	  playbackCursor_(0.0),
	  seekCrossfadeActive_(false),
	  pendingSeekPosition_(0),
	  seekFadeRemaining_(0),
	  oldSeekCursor_(0.0) {
}

AudioOutput::~AudioOutput() {
	if (stream_) {
		if (Pa_IsStreamActive(stream_) == 1) {
			Pa_StopStream(stream_);
		}
		Pa_CloseStream(stream_);
	}
}

std::vector<AudioOutput::DeviceInfo> AudioOutput::getOutputDevices() {
	std::vector<DeviceInfo> devices;

	const int deviceCount = Pa_GetDeviceCount();
	if (deviceCount < 0) {
		return devices;
	}

	devices.reserve(static_cast<size_t>(deviceCount));
	for (int i = 0; i < deviceCount; ++i) {
		if (const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i)) {
			if (deviceInfo->maxOutputChannels > 0) {
				devices.emplace_back(DeviceInfo{deviceInfo->name, i, deviceInfo->maxOutputChannels});
			}
		}
	}
	return devices;
}

bool AudioOutput::initOutputStream(float sampleRate, int deviceIndex, int framesPerBuffer) {
	if (stream_) {
		if (Pa_IsStreamActive(stream_) == 1) {
			Pa_StopStream(stream_);
		}
		Pa_CloseStream(stream_);
		stream_ = nullptr;
	}
	requestedSampleRate_.store(sampleRate);
	actualSampleRate_.store(sampleRate);
	playbackStep_.store(1.0f);

	PaStreamParameters outputParameters{};
	if (deviceIndex >= 0) {
		outputParameters.device = deviceIndex;
	} else {
		outputParameters.device = Pa_GetDefaultOutputDevice();
	}
	if (outputParameters.device == paNoDevice) {
		return false;
	}

	const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(outputParameters.device);
	if (!deviceInfo) {
		return false;
	}

	outputParameters.channelCount = 1;
	outputParameters.sampleFormat = paFloat32;
	outputParameters.suggestedLatency = deviceInfo->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = nullptr;

	PaError err = Pa_OpenStream(
		&stream_,
		nullptr,
		&outputParameters,
		static_cast<double>(sampleRate),
		static_cast<unsigned long>(framesPerBuffer),
		paClipOff,
		&AudioOutput::audioCallback,
		this
	);

	if (err != paNoError) {
		stream_ = nullptr;
		return false;
	}

	if (const PaStreamInfo* info = Pa_GetStreamInfo(stream_)) {
		const float actualRate = static_cast<float>(info->sampleRate);
		actualSampleRate_.store(actualRate);
		if (actualRate > 0.0f && sampleRate > 0.0f) {
			playbackStep_.store(sampleRate / actualRate);
		} else {
			playbackStep_.store(1.0f);
		}
	}
	return true;
}

void AudioOutput::setAudioData(const std::vector<float>& audioSamples) {
	if (audioSamples.empty()) {
		clearAudioData();
		return;
	}

	auto newBuffer = std::make_shared<std::vector<float>>(audioSamples);
	{
		std::lock_guard<std::mutex> lock(bufferMutex_);
		audioBuffer_ = std::move(newBuffer);
		playbackCursor_ = 0.0;
	}
	totalSamples_.store(audioSamples.size());
	playbackPosition_.store(0);
}

void AudioOutput::play() {
	if (!stream_ || isPlaying_.load()) {
		return;
	}

	if (Pa_IsStreamActive(stream_) != 1) {
		PaError err = Pa_StartStream(stream_);
		if (err != paNoError) {
			return;
		}
	}

	isPlaying_.store(true);
}

void AudioOutput::pause() {
	if (!stream_ || !isPlaying_.load()) {
		return;
	}

	isPlaying_.store(false);
}

void AudioOutput::stop() {
	if (!stream_) {
		return;
	}

	isPlaying_.store(false);
	if (Pa_IsStreamActive(stream_) == 1) {
		Pa_StopStream(stream_);
	}
	playbackPosition_.store(0);
	{
		std::lock_guard<std::mutex> lock(bufferMutex_);
		playbackCursor_ = 0.0;
	}
}

void AudioOutput::seek(size_t samplePosition) {
	const size_t total = totalSamples_.load();
	const size_t clamped = std::min(samplePosition, total);
	const size_t oldPos = playbackPosition_.load();

	constexpr size_t SEEK_THRESHOLD = 2205;
	constexpr size_t CROSSFADE_SAMPLES = 256;

	if (std::abs(static_cast<long>(clamped) - static_cast<long>(oldPos)) > static_cast<long>(SEEK_THRESHOLD)) {
		std::lock_guard<std::mutex> lock(bufferMutex_);
		oldSeekCursor_ = playbackCursor_;
		pendingSeekPosition_.store(clamped);
		seekFadeRemaining_.store(CROSSFADE_SAMPLES);
		seekCrossfadeActive_.store(true);
	} else {
		std::lock_guard<std::mutex> lock(bufferMutex_);
		playbackCursor_ = static_cast<double>(clamped);
	}
	playbackPosition_.store(clamped);
}

void AudioOutput::clearAudioData() {
	stop();
	std::lock_guard<std::mutex> lock(bufferMutex_);
	audioBuffer_ = std::make_shared<std::vector<float>>();
	totalSamples_.store(0);
	playbackPosition_.store(0);
	playbackCursor_ = 0.0;
}

float AudioOutput::getPlaybackRateRatio() const {
	const float actual = actualSampleRate_.load();
	const float requested = requestedSampleRate_.load();
	if (actual <= 0.0f || requested <= 0.0f) {
		return 1.0f;
	}
	return requested / actual;
}

int AudioOutput::audioCallback(const void* input, void* output,
								unsigned long frameCount,
								const PaStreamCallbackTimeInfo* timeInfo,
								PaStreamCallbackFlags statusFlags,
								void* userData) {
	(void)input;
	(void)timeInfo;
	(void)statusFlags;

	auto* audioOutput = static_cast<AudioOutput*>(userData);
	auto* out = static_cast<float*>(output);

	if (!audioOutput->isPlaying_.load()) {
		std::memset(out, 0, frameCount * sizeof(float));
		return paContinue;
	}

	std::shared_ptr<std::vector<float>> bufferSnapshot;
	double cursorSnapshot = 0.0;
	double oldSeekCursorSnapshot = 0.0;
	bool seekFading = false;
	{
		std::lock_guard<std::mutex> lock(audioOutput->bufferMutex_);
		bufferSnapshot = audioOutput->audioBuffer_;
		cursorSnapshot = audioOutput->playbackCursor_;

		if (audioOutput->seekCrossfadeActive_.load()) {
			oldSeekCursorSnapshot = audioOutput->oldSeekCursor_;
			audioOutput->playbackCursor_ = static_cast<double>(audioOutput->pendingSeekPosition_.load());
			cursorSnapshot = audioOutput->playbackCursor_;
			seekFading = true;
			audioOutput->seekCrossfadeActive_.store(false);
		}
	}

	const size_t total = audioOutput->totalSamples_.load();
	const bool loopEnabled = audioOutput->loopEnabled_.load();
	const double totalDouble = static_cast<double>(total);

	if (!bufferSnapshot || bufferSnapshot->empty() || total == 0) {
		std::memset(out, 0, frameCount * sizeof(float));
		return paContinue;
	}

	double cursor = cursorSnapshot;
	double step = static_cast<double>(audioOutput->playbackStep_.load());
	if (!std::isfinite(step) || step <= 0.0) {
		step = 1.0;
	}

	bool stopPlayback = false;
	const auto& buffer = *bufferSnapshot;
	double oldSeekCursor = oldSeekCursorSnapshot;
	size_t seekFadeRemaining = seekFading ? audioOutput->seekFadeRemaining_.load() : 0;

	for (unsigned long i = 0; i < frameCount; ++i) {
		if (!loopEnabled && cursor >= totalDouble) {
			const size_t remaining = frameCount - i;
			if (remaining > 0) {
				std::memset(&out[i], 0, remaining * sizeof(float));
			}
			stopPlayback = true;
			cursor = totalDouble;
			break;
		}

		double workingCursor = cursor;
		if (loopEnabled && totalDouble > 0.0) {
			workingCursor = std::fmod(workingCursor, totalDouble);
			if (!std::isfinite(workingCursor)) {
				workingCursor = 0.0;
			} else if (workingCursor < 0.0) {
				workingCursor += totalDouble;
			}
		} else {
			workingCursor = std::clamp(workingCursor, 0.0, totalDouble);
		}

		size_t index = static_cast<size_t>(workingCursor);
		if (index >= total) {
			index = total - 1;
		}
		size_t nextIndex = index + 1;
		if (nextIndex >= total) {
			nextIndex = loopEnabled ? 0 : total - 1;
		}

		const double frac = std::clamp(workingCursor - static_cast<double>(index), 0.0, 1.0);
		const float current = buffer[index];
		const float next = buffer[nextIndex];

		constexpr size_t LOOP_CROSSFADE_SAMPLES = 128;
		const bool nearLoopBoundary = loopEnabled && total >= LOOP_CROSSFADE_SAMPLES * 2;

		if (nearLoopBoundary && index >= total - LOOP_CROSSFADE_SAMPLES && nextIndex < LOOP_CROSSFADE_SAMPLES) {
			const size_t distanceFromEnd = total - 1 - index;
			const float fadeOut = static_cast<float>(distanceFromEnd + 1) / static_cast<float>(LOOP_CROSSFADE_SAMPLES);
			const float fadeIn = 1.0f - fadeOut;
			out[i] = (current + static_cast<float>(frac) * (next - current)) * fadeOut +
			         buffer[nextIndex] * fadeIn;
		} else {
			out[i] = current + static_cast<float>(frac) * (next - current);
		}

		if (seekFadeRemaining > 0) {
			double oldWorkingCursor = oldSeekCursor;
			if (loopEnabled && totalDouble > 0.0) {
				oldWorkingCursor = std::fmod(oldWorkingCursor, totalDouble);
				if (!std::isfinite(oldWorkingCursor)) {
					oldWorkingCursor = 0.0;
				} else if (oldWorkingCursor < 0.0) {
					oldWorkingCursor += totalDouble;
				}
			} else {
				oldWorkingCursor = std::clamp(oldWorkingCursor, 0.0, totalDouble);
			}

			size_t oldIndex = static_cast<size_t>(oldWorkingCursor);
			if (oldIndex >= total) {
				oldIndex = total - 1;
			}
			size_t oldNextIndex = oldIndex + 1;
			if (oldNextIndex >= total) {
				oldNextIndex = loopEnabled ? 0 : total - 1;
			}

			const double oldFrac = std::clamp(oldWorkingCursor - static_cast<double>(oldIndex), 0.0, 1.0);
			const float oldSample = buffer[oldIndex] + static_cast<float>(oldFrac) * (buffer[oldNextIndex] - buffer[oldIndex]);

			constexpr size_t TOTAL_CROSSFADE_SAMPLES = 256;
			const float fadeIn = static_cast<float>(TOTAL_CROSSFADE_SAMPLES - seekFadeRemaining) / static_cast<float>(TOTAL_CROSSFADE_SAMPLES);
			const float fadeOut = 1.0f - fadeIn;

			out[i] = out[i] * fadeIn + oldSample * fadeOut;

			seekFadeRemaining--;
			oldSeekCursor += step;
		}

		cursor += step;
	}

	double finalCursor = cursor;
	if (loopEnabled && totalDouble > 0.0) {
		finalCursor = std::fmod(finalCursor, totalDouble);
		if (!std::isfinite(finalCursor)) {
			finalCursor = 0.0;
		} else if (finalCursor < 0.0) {
			finalCursor += totalDouble;
		}
	} else {
		finalCursor = std::clamp(finalCursor, 0.0, totalDouble);
	}

	{
		std::lock_guard<std::mutex> lock(audioOutput->bufferMutex_);
		audioOutput->playbackCursor_ = finalCursor;
	}

	if (seekFading) {
		audioOutput->seekFadeRemaining_.store(seekFadeRemaining);
	}

	if (loopEnabled) {
		size_t stored = static_cast<size_t>(std::floor(finalCursor));
		if (stored >= total) {
			stored = total > 0 ? total - 1 : 0;
		}
		audioOutput->playbackPosition_.store(stored);
	} else {
		if (stopPlayback || finalCursor >= totalDouble) {
			audioOutput->playbackPosition_.store(total);
			audioOutput->isPlaying_.store(false);
		} else {
			size_t stored = static_cast<size_t>(std::floor(finalCursor));
			if (stored >= total) {
				stored = total - 1;
			}
			audioOutput->playbackPosition_.store(stored);
		}
	}

	// Apply soft-knee limiter to prevent clipping and protect hearing
	constexpr float THRESHOLD = 0.85f;  // Start limiting at -1.4 dBFS
	constexpr float KNEE = 0.1f;        // Soft knee width
	constexpr float CEILING = 0.95f;    // Hard ceiling at -0.44 dBFS

	for (size_t i = 0; i < frameCount; ++i) {
		float sample = out[i];
		float absSample = std::abs(sample);

		if (absSample > THRESHOLD) {
			float excess = absSample - THRESHOLD;
			float reduction;

			if (excess < KNEE) {
				// Soft knee: gradual compression
				reduction = excess * excess / (2.0f * KNEE);
			} else {
				// Above knee: stronger limiting
				reduction = excess - KNEE / 2.0f;
			}

			float limited = THRESHOLD + reduction * 0.3f;  // 3:1 ratio above threshold
			limited = std::min(limited, CEILING);
			out[i] = (sample >= 0.0f) ? limited : -limited;
		}
	}

	return paContinue;
}
