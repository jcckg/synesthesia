#include "audio_output.h"
#include "audio/analysis/meter_response.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

AudioOutput::AudioOutput()
	: stream_(nullptr),
	  audioBuffer_(std::make_unique<AudioStorage>()),
	  playbackPosition_(0),
	  totalSamples_(0),
	  isPlaying_(false),
	  loopEnabled_(true),
	  requestedSampleRate_(0.0f),
	  actualSampleRate_(0.0f),
	  playbackStep_(1.0f),
	  channelCount_(1),
	  playbackCursor_(0.0),

	  oldSeekCursor_(0.0),
	  leftLevel_(0.0f),
	  rightLevel_(0.0f) {
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

bool AudioOutput::initOutputStream(float sampleRate, int channelCount, int deviceIndex, int framesPerBuffer) {
	if (stream_) {
		if (Pa_IsStreamActive(stream_) == 1) {
			Pa_StopStream(stream_);
		}
		Pa_CloseStream(stream_);
		stream_ = nullptr;
	}
	isPlaying_.store(false);
	clockSession_.fetch_add(1);
    clockAvailable_.store(false);
	outputChannelCount_.store(0);
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

	const int requestedChannels = std::max(1, channelCount);
	// https://portaudio.com/docs/v19-doxydocs/structPaStreamParameters.html
	if (requestedChannels > deviceInfo->maxOutputChannels || !std::isfinite(sampleRate) || sampleRate <= 0.0f) {
		return false;
	}
	const int clampedChannels = requestedChannels;
	outputChannelCount_.store(static_cast<size_t>(clampedChannels));
	outputParameters.channelCount = clampedChannels;
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
		playbackEqualiser_.configure(actualRate, static_cast<size_t>(clampedChannels));
	}
	return true;
}

// https://eel.is/c++draft/atomics.order
void AudioOutput::setAudioData(const std::vector<float>& audioSamples, size_t channels) {
    if (audioSamples.empty()) { clearAudioData(); return; }
    auto storage = std::make_unique<AudioStorage>();
    storage->samples = audioSamples;
    for (auto& sample : storage->samples) if (!std::isfinite(sample)) sample = 0.0f;
    storage->channels = std::max<size_t>(1, channels);
    std::lock_guard lock(bufferMutex_);
    storage->generation = ++nextGeneration_;
    retiredBuffers_.push_back(std::move(audioBuffer_));
    audioBuffer_ = std::move(storage);
    channelCount_.store(audioBuffer_->channels);
    totalSamples_.store(audioBuffer_->samples.size());
    presentationGeneration_.store(audioBuffer_->generation);
    publishedBuffer_.store(audioBuffer_.get());
    if (callbackReaders_.load() == 0) retiredBuffers_.clear();
    publishSeek(0, false);
    clockSession_.fetch_add(1);
    playbackPosition_.store(0);
    playbackEqualiser_.requestReset();
}

void AudioOutput::publishSeek(size_t position, bool fade) {
    seekVersion_.fetch_add(1);
    pendingSeekPosition_.store(position);
    pendingSeekFade_.store(fade);
    seekGeneration_.store(audioBuffer_ ? audioBuffer_->generation : 0);
    seekVersion_.fetch_add(1);
}

void AudioOutput::setPlaybackEQEnabled(const bool enabled) {
	playbackEqualiser_.setEnabled(enabled);
}

void AudioOutput::setPlaybackEQGains(const float low, const float mid, const float high) {
	playbackEqualiser_.setGains(low, mid, high);
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
    clockSession_.fetch_add(1);
	resetStereoLevels();
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
        publishSeek(0, false);
        clockSession_.fetch_add(1);
	}
	playbackEqualiser_.requestReset();
	resetStereoLevels();
}

void AudioOutput::seek(size_t framePosition) {
    std::lock_guard lock(bufferMutex_);
    const auto count = audioBuffer_ && audioBuffer_->channels > 0 ?
        audioBuffer_->samples.size() / audioBuffer_->channels : 0;
    const auto position = std::min(framePosition, count);
    const auto previous = playbackPosition_.load();
    const bool fade = position > previous ? position - previous > 2205 : previous - position > 2205;
    publishSeek(position, fade);
    playbackEqualiser_.requestReset();
    playbackPosition_.store(position);
}

void AudioOutput::clearAudioData() {
    stop();
    std::lock_guard lock(bufferMutex_);
    publishedBuffer_.store(nullptr);
    if (callbackReaders_.load() == 0) {
        retiredBuffers_.clear();
        audioBuffer_.reset();
    } else retiredBuffers_.push_back(std::move(audioBuffer_));
    totalSamples_.store(0);
    playbackPosition_.store(0);
    clockSession_.fetch_add(1);
    playbackEqualiser_.requestReset();
    resetStereoLevels();
}

// https://portaudio.com/docs/v19-doxydocs/structPaStreamCallbackTimeInfo.html
// https://eel.is/c++draft/atomics.order
double AudioOutput::getPresentationPosition() const {
    const double fallback = static_cast<double>(playbackPosition_.load());
    if (!stream_ || !clockAvailable_.load()) return fallback;
    const double time = Pa_GetStreamTime(stream_);
    if (!std::isfinite(time)) return fallback;
    const auto session = clockSession_.load();
    const auto generation = presentationGeneration_.load();
    const auto written = writtenIntervals_.load();
    const auto count = std::min<std::uint64_t>(written, outputIntervals_.size());
    bool found = false;
    double selectedTime = 0, position = fallback;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto& interval = outputIntervals_[(written - count + i) % outputIntervals_.size()];
        const auto version = interval.version.load();
        if (version == 0 || (version & 1u)) continue;
        const double dacTime = interval.dacTime.load();
        const double start = interval.position.load();
        const double duration = interval.duration.load();
        const double speed = interval.framesPerSecond.load();
        const double total = interval.totalFrames.load();
        const bool loop = interval.loop.load();
        const auto intervalSession = interval.session.load();
        const auto intervalGeneration = interval.generation.load();
        if (interval.version.load() != version || intervalSession != session ||
            intervalGeneration != generation ||
            (found && dacTime > time)) continue;
        double candidate = start + std::clamp(time - dacTime, 0.0, duration) * speed;
        if (loop && total > 0.0) candidate = std::fmod(candidate, total);
        if (!found || dacTime >= selectedTime) {
            found = true;
            selectedTime = dacTime;
            position = std::clamp(candidate, 0.0, total);
        }
    }
    return position;
}

float AudioOutput::getPlaybackRateRatio() const {
	const float actual = actualSampleRate_.load();
	const float requested = requestedSampleRate_.load();
	if (actual <= 0.0f || requested <= 0.0f) {
		return 1.0f;
	}
	return requested / actual;
}

void AudioOutput::updateStereoLevels(const float left, const float right, const double elapsed) {
	leftLevel_.store(AudioMeter::peakResponse(leftLevel_.load(), std::clamp(left, 0.0f, 1.0f), elapsed));
	rightLevel_.store(AudioMeter::peakResponse(rightLevel_.load(), std::clamp(right, 0.0f, 1.0f), elapsed));
}

void AudioOutput::resetStereoLevels() {
	leftLevel_.store(0.0f);
	rightLevel_.store(0.0f);
}

int AudioOutput::audioCallback(const void* input, void* output,
								unsigned long frameCount,
								const PaStreamCallbackTimeInfo* timeInfo,
								PaStreamCallbackFlags statusFlags,
								void* userData) {
	(void)input;
	(void)statusFlags;

	auto* audioOutput = static_cast<AudioOutput*>(userData);
	auto* out = static_cast<float*>(output);
	const size_t outputChannels = audioOutput->outputChannelCount_.load();

	if (!audioOutput->isPlaying_.load()) {
		std::memset(out, 0, frameCount * outputChannels * sizeof(float));
		audioOutput->resetStereoLevels();
		return paContinue;
	}

    // https://portaudio.com/docs/v19-doxydocs/writing_a_callback.html
    // https://eel.is/c++draft/atomics.order
    struct ReadGuard {
        std::atomic<std::uint32_t>& readers;
        explicit ReadGuard(std::atomic<std::uint32_t>& value) : readers(value) { readers.fetch_add(1); }
        ~ReadGuard() { readers.fetch_sub(1); }
    } guard(audioOutput->callbackReaders_);
    const auto* snapshot = audioOutput->publishedBuffer_.load();
    if (!snapshot || snapshot->channels != outputChannels || snapshot->samples.empty()) {
        std::fill_n(out, frameCount * outputChannels, 0.0f);
        audioOutput->resetStereoLevels();
        return paContinue;
    }
    if (snapshot->generation != audioOutput->activeGeneration_) {
        audioOutput->activeGeneration_ = snapshot->generation;
        audioOutput->playbackCursor_ = 0.0;
        audioOutput->seekFadeRemaining_ = 0;
    }
    const auto version = audioOutput->seekVersion_.load();
    if ((version & 1u) == 0 && version != audioOutput->appliedSeekVersion_) {
        const auto position = audioOutput->pendingSeekPosition_.load();
        const auto fade = audioOutput->pendingSeekFade_.load();
        const auto generation = audioOutput->seekGeneration_.load();
        if (audioOutput->seekVersion_.load() == version && generation == snapshot->generation) {
            audioOutput->oldSeekCursor_ = audioOutput->playbackCursor_;
            audioOutput->playbackCursor_ = static_cast<double>(position);
            audioOutput->seekFadeRemaining_ = fade ? 256 : 0;
            audioOutput->appliedSeekVersion_ = version;
        }
    }
    const double cursorSnapshot = audioOutput->playbackCursor_;
    const double oldSeekCursorSnapshot = audioOutput->oldSeekCursor_;
    const auto session = audioOutput->clockSession_.load();
    const size_t totalSamples = snapshot->samples.size();
    const bool loopEnabled = audioOutput->loopEnabled_.load();
    const size_t totalFrames = totalSamples / outputChannels;
    const double totalFramesDouble = static_cast<double>(totalFrames);
    if (totalFrames == 0) { std::fill_n(out, frameCount * outputChannels, 0.0f); return paContinue; }

	double cursor = cursorSnapshot;
	double step = static_cast<double>(audioOutput->playbackStep_.load());
	if (!std::isfinite(step) || step <= 0.0) {
		step = 1.0;
	}

	bool stopPlayback = false;
	const auto& buffer = snapshot->samples;
	double oldSeekCursor = oldSeekCursorSnapshot;
	size_t seekFadeRemaining = audioOutput->seekFadeRemaining_;

	for (unsigned long i = 0; i < frameCount; ++i) {
		if (!loopEnabled && cursor >= totalFramesDouble) {
			const size_t remaining = frameCount - i;
			if (remaining > 0) {
				std::memset(&out[i * outputChannels], 0, remaining * outputChannels * sizeof(float));
			}
			stopPlayback = true;
			cursor = totalFramesDouble;
			break;
		}

		double workingCursor = cursor;
		if (loopEnabled && totalFramesDouble > 0.0) {
			workingCursor = std::fmod(workingCursor, totalFramesDouble);
			if (!std::isfinite(workingCursor)) {
				workingCursor = 0.0;
			} else if (workingCursor < 0.0) {
				workingCursor += totalFramesDouble;
			}
		} else {
			workingCursor = std::clamp(workingCursor, 0.0, totalFramesDouble);
		}

		const size_t frameIndex = static_cast<size_t>(workingCursor);
		size_t clampedFrameIndex = std::min(frameIndex, totalFrames > 0 ? totalFrames - 1 : 0);
		size_t nextFrameIndex = clampedFrameIndex + 1;
		if (nextFrameIndex >= totalFrames) {
			nextFrameIndex = loopEnabled ? 0 : (totalFrames > 0 ? totalFrames - 1 : 0);
		}

		const double frac = std::clamp(workingCursor - static_cast<double>(clampedFrameIndex), 0.0, 1.0);

		const size_t loopFadeFrames = 128;
		const bool nearLoopBoundary = loopEnabled && totalFrames >= loopFadeFrames * 2;

		for (size_t ch = 0; ch < outputChannels; ++ch) {
			const size_t currentSampleIndex = clampedFrameIndex * outputChannels + ch;
			const size_t nextSampleIndex = nextFrameIndex * outputChannels + ch;

			const float current = buffer[std::min(currentSampleIndex, totalSamples - 1)];
			const float next = buffer[std::min(nextSampleIndex, totalSamples - 1)];

            float sample = current + static_cast<float>(frac) * (next - current);
            // https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hann.html
            if (nearLoopBoundary) {
                const double distance = std::min(workingCursor, totalFramesDouble - 1.0 - workingCursor);
                const double fraction = std::clamp(distance / static_cast<double>(loopFadeFrames - 1), 0.0, 1.0);
                sample *= static_cast<float>(.5 - .5 * std::cos(std::numbers::pi * fraction));
            }

			out[i * outputChannels + ch] = sample;
		}

		if (seekFadeRemaining > 0) {
			double oldWorkingCursor = oldSeekCursor;
			if (loopEnabled && totalFramesDouble > 0.0) {
				oldWorkingCursor = std::fmod(oldWorkingCursor, totalFramesDouble);
				if (!std::isfinite(oldWorkingCursor)) {
					oldWorkingCursor = 0.0;
				} else if (oldWorkingCursor < 0.0) {
					oldWorkingCursor += totalFramesDouble;
				}
			} else {
				oldWorkingCursor = std::clamp(oldWorkingCursor, 0.0, totalFramesDouble);
			}

			const size_t oldFrameIndex = static_cast<size_t>(oldWorkingCursor);
			const size_t oldClampedFrameIndex = std::min(oldFrameIndex, totalFrames > 0 ? totalFrames - 1 : 0);
			size_t oldNextFrameIndex = oldClampedFrameIndex + 1;
			if (oldNextFrameIndex >= totalFrames) {
				oldNextFrameIndex = loopEnabled ? 0 : (totalFrames > 0 ? totalFrames - 1 : 0);
			}

			const double oldFrac = std::clamp(oldWorkingCursor - static_cast<double>(oldClampedFrameIndex), 0.0, 1.0);

			const size_t totalCrossfadeSamples = 256;
			const float fadeIn = static_cast<float>(totalCrossfadeSamples - seekFadeRemaining) / static_cast<float>(totalCrossfadeSamples);
			const float fadeOut = 1.0f - fadeIn;

			for (size_t ch = 0; ch < outputChannels; ++ch) {
				const size_t oldCurrentSampleIndex = oldClampedFrameIndex * outputChannels + ch;
				const size_t oldNextSampleIndex = oldNextFrameIndex * outputChannels + ch;
				const float oldSample = buffer[std::min(oldCurrentSampleIndex, totalSamples - 1)] +
					static_cast<float>(oldFrac) * (buffer[std::min(oldNextSampleIndex, totalSamples - 1)] - buffer[std::min(oldCurrentSampleIndex, totalSamples - 1)]);

				out[i * outputChannels + ch] = out[i * outputChannels + ch] * fadeIn + oldSample * fadeOut;
			}

			seekFadeRemaining--;
			oldSeekCursor += step;
		}

		cursor += step;
	}

	double finalCursor = cursor;
	if (loopEnabled && totalFramesDouble > 0.0) {
		finalCursor = std::fmod(finalCursor, totalFramesDouble);
		if (!std::isfinite(finalCursor)) {
			finalCursor = 0.0;
		} else if (finalCursor < 0.0) {
			finalCursor += totalFramesDouble;
		}
	} else {
		finalCursor = std::clamp(finalCursor, 0.0, totalFramesDouble);
	}

    audioOutput->playbackCursor_ = finalCursor;
    audioOutput->seekFadeRemaining_ = seekFadeRemaining;
    audioOutput->oldSeekCursor_ = oldSeekCursor;
    if (timeInfo && std::isfinite(timeInfo->outputBufferDacTime) && std::isfinite(timeInfo->currentTime)) {
        const auto serial = audioOutput->writtenIntervals_.load(std::memory_order_relaxed);
        auto& interval = audioOutput->outputIntervals_[serial % audioOutput->outputIntervals_.size()];
        interval.version.store(serial * 2 + 1);
        const double rate = std::max(1.0f, audioOutput->actualSampleRate_.load());
        interval.dacTime.store(timeInfo->outputBufferDacTime);
        interval.position.store(cursorSnapshot);
        interval.duration.store(static_cast<double>(frameCount) / rate);
        interval.framesPerSecond.store(rate * step);
        interval.totalFrames.store(totalFramesDouble);
        interval.loop.store(loopEnabled);
        interval.session.store(session);
        interval.generation.store(snapshot->generation);
        interval.version.store(serial * 2 + 2);
        audioOutput->writtenIntervals_.store(serial + 1);
        audioOutput->clockAvailable_.store(true);
    } else audioOutput->clockAvailable_.store(false);

	if (loopEnabled) {
		size_t stored = static_cast<size_t>(std::floor(finalCursor));
		if (stored >= totalFrames) {
			stored = totalFrames > 0 ? totalFrames - 1 : 0;
		}
		audioOutput->playbackPosition_.store(stored);
	} else {
		if (stopPlayback || finalCursor >= totalFramesDouble) {
			audioOutput->playbackPosition_.store(totalFrames);
			audioOutput->isPlaying_.store(false);
		} else {
			size_t stored = static_cast<size_t>(std::floor(finalCursor));
			if (stored >= totalFrames) {
				stored = totalFrames - 1;
			}
			audioOutput->playbackPosition_.store(stored);
		}
	}

	audioOutput->playbackEqualiser_.processInterleaved(out, frameCount, outputChannels);

	constexpr float THRESHOLD = 0.85f;
	constexpr float KNEE = 0.1f;
	constexpr float CEILING = 0.95f;

	for (size_t i = 0; i < frameCount * outputChannels; ++i) {
		float sample = out[i];
		float absSample = std::abs(sample);

		if (absSample > THRESHOLD) {
			float excess = absSample - THRESHOLD;
			float reduction;

			if (excess < KNEE) {
				reduction = excess * excess / (2.0f * KNEE);
			} else {
				reduction = excess - KNEE / 2.0f;
			}

			float limited = THRESHOLD + reduction * 0.3f;
			limited = std::min(limited, CEILING);
			out[i] = (sample >= 0.0f) ? limited : -limited;
		}
	}

	float leftPeak = 0.0f;
	float rightPeak = 0.0f;
	for (unsigned long frame = 0; frame < frameCount; ++frame) {
		const size_t offset = static_cast<size_t>(frame) * outputChannels;
		const float left = std::abs(out[offset]);
		const float right = outputChannels > 1 ? std::abs(out[offset + 1]) : left;
		leftPeak = std::max(leftPeak, left);
		rightPeak = std::max(rightPeak, right);
	}
	audioOutput->updateStereoLevels(leftPeak, rightPeak,
        static_cast<double>(frameCount) / static_cast<double>(std::max(1.0f, audioOutput->actualSampleRate_.load())));

	return paContinue;
}
