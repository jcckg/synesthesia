#include "audio_input.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <cstdio>

static void suppress_alsa_errors(const char* file, int line, const char* function, int err, const char* fmt, ...) {
    (void)file; (void)line; (void)function; (void)err; (void)fmt;
}

class ALSAErrorSuppressor {
public:
    ALSAErrorSuppressor() {
        snd_lib_error_set_handler(suppress_alsa_errors);
    }

    ~ALSAErrorSuppressor() {
        snd_lib_error_set_handler(nullptr);
    }
};
#endif

AudioInput::AudioInput()
	: stream(nullptr),
	  dcFilter(0.995f),
	  noiseGate(0.0001f),
	  sampleRate(44100.0f),
	  channelCount(1),
	  activeChannel(0) {
#ifdef __linux__
	ALSAErrorSuppressor suppressor;
#endif
	if (const PaError err = Pa_Initialize(); err != paNoError) {
		throw std::runtime_error("PortAudio initialisation failed: " +
								 std::string(Pa_GetErrorText(err)));
	}

	processor.start();
}

AudioInput::~AudioInput() {
	stopStream();
	Pa_Terminate();

	processor.stop();
}

std::vector<AudioInput::DeviceInfo> AudioInput::getInputDevices() {
	std::vector<DeviceInfo> devices;

#ifdef __linux__
	ALSAErrorSuppressor suppressor;
#endif

	const int deviceCount = Pa_GetDeviceCount();

	if (deviceCount < 0) {
		std::cerr << "AudioInput: Failed to get device count: " << Pa_GetErrorText(deviceCount) << std::endl;
		return devices;
	}

	devices.reserve(static_cast<size_t>(deviceCount));
	for (int i = 0; i < deviceCount; ++i) {
		if (const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i)) {
			if (deviceInfo->maxInputChannels > 0) {
				devices.emplace_back(DeviceInfo{deviceInfo->name, i, deviceInfo->maxInputChannels});
			}
		}
	}
	return devices;
}

bool AudioInput::initStream(const int deviceIndex, const int numChannels) {
	stopStream();

	const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
	if (!deviceInfo) {
		std::cerr << "AudioInput: Failed to get device info for index " << deviceIndex << std::endl;
		return false;
	}
	if (deviceInfo->maxInputChannels < 1) {
		std::cerr << "AudioInput: Device has no input channels" << std::endl;
		return false;
	}

	channelCount = std::min(numChannels, deviceInfo->maxInputChannels);
	if (channelCount < 1)
		channelCount = 1;

	activeChannel = 0;
	dcFilter.setChannelCount(static_cast<size_t>(channelCount));

	PaStreamParameters inputParameters{};
	inputParameters.device = deviceIndex;
	inputParameters.channelCount = channelCount;
	inputParameters.sampleFormat = paFloat32;
	inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;

	const PaError err =
		Pa_OpenStream(&stream, &inputParameters, nullptr, deviceInfo->defaultSampleRate,
					  FFTProcessor::FFT_SIZE, paClipOff, audioCallback, this);

	if (err != paNoError) {
		std::cerr << "AudioInput: Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
		stream = nullptr;
		return false;
	}

	const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream);
	if (!streamInfo) {
		std::cerr << "AudioInput: Failed to get stream info" << std::endl;
		stopStream();
		return false;
	}
	sampleRate = static_cast<float>(streamInfo->sampleRate);

	if (const PaError startErr = Pa_StartStream(stream); startErr != paNoError) {
		std::cerr << "AudioInput: Failed to start stream: " << Pa_GetErrorText(startErr) << std::endl;
		stopStream();
		return false;
	}

	return true;
}

AudioProcessor::SpectralData AudioInput::getSpectralData() const {
	return processor.getSpectralData();
}

bool AudioInput::pauseStream() {
	if (stream && Pa_IsStreamActive(stream) == 1) {
		if (Pa_StopStream(stream) == paNoError) {
			return true;
		}
	}
	return false;
}

bool AudioInput::resumeStream() {
	if (stream && Pa_IsStreamStopped(stream) == 1) {
		if (Pa_StartStream(stream) == paNoError) {
			return true;
		}
	}
	return false;
}

bool AudioInput::isStreamActive() const {
	return stream && Pa_IsStreamActive(stream) == 1;
}

void AudioInput::stopStream() {
	if (stream) {
		Pa_StopStream(stream);
		Pa_CloseStream(stream);
		stream = nullptr;
	}
}

int AudioInput::audioCallback(const void* input, [[maybe_unused]] void* output,
							  const unsigned long frameCount,
							  [[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
							  [[maybe_unused]] PaStreamCallbackFlags statusFlags,
							  void* userData) {
	auto* audio = static_cast<AudioInput*>(userData);

	if (!input) {
		return paContinue;
	}

	try {
		const auto* inBuffer = static_cast<const float*>(input);
		constexpr size_t MAX_BUFFER_SIZE = 4096;
		thread_local std::vector<float> processedBuffer(MAX_BUFFER_SIZE);
		if (frameCount > MAX_BUFFER_SIZE) {
			processedBuffer.resize(frameCount);
		}

		audio->processor.queueAudioData(inBuffer, frameCount * static_cast<size_t>(audio->channelCount), audio->sampleRate, static_cast<size_t>(audio->channelCount));
	}
	catch (const std::exception& e) {
		std::cerr << "[Audio] Exception in audio callback: " << e.what() << std::endl;
		return paContinue;
	}
	catch (...) {
		std::cerr << "[Audio] Unknown exception in audio callback" << std::endl;
		return paContinue;
	}

	return paContinue;
}