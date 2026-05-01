#include "audio_input.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
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

namespace {

float smoothedLevel(const float current, const float target) {
	return std::max(target, current * 0.84f);
}

}

AudioInput::AudioInput()
	: stream(nullptr),
	  dcFilter(0.995f),
	  noiseGate(0.0001f),
	  sampleRate(44100.0f),
	  channelCount(1),
	  activeChannel(0),
	  leftLevel(0.0f),
	  rightLevel(0.0f) {
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
	resetStereoLevels();
}

void AudioInput::updateStereoLevels(const float left, const float right) {
	leftLevel.store(smoothedLevel(leftLevel.load(), std::clamp(left, 0.0f, 1.0f)));
	rightLevel.store(smoothedLevel(rightLevel.load(), std::clamp(right, 0.0f, 1.0f)));
}

void AudioInput::resetStereoLevels() {
	leftLevel.store(0.0f);
	rightLevel.store(0.0f);
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

		float leftPeak = 0.0f;
		float rightPeak = 0.0f;
		const size_t channels = static_cast<size_t>(std::max(audio->channelCount, 1));
		for (unsigned long frame = 0; frame < frameCount; ++frame) {
			const size_t offset = static_cast<size_t>(frame) * channels;
			const float left = std::abs(inBuffer[offset]);
			const float right = channels > 1 ? std::abs(inBuffer[offset + 1]) : left;
			leftPeak = std::max(leftPeak, left);
			rightPeak = std::max(rightPeak, right);
		}
		audio->updateStereoLevels(leftPeak, rightPeak);
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

struct AudioInputLevelMonitor::MonitoredDevice {
	int paIndex = paNoDevice;
	int channelCount = 0;
	PaStream* stream = nullptr;
	bool startAttempted = false;
	std::atomic<float> leftLevel{0.0f};
	std::atomic<float> rightLevel{0.0f};
};

AudioInputLevelMonitor::AudioInputLevelMonitor() = default;

AudioInputLevelMonitor::~AudioInputLevelMonitor() {
	stopAll();
}

void AudioInputLevelMonitor::syncDevices(const std::vector<AudioInput::DeviceInfo>& devices,
										 const int selectedPaIndex) {
	bool topologyChanged = monitoredDevices_.size() != devices.size();
	if (!topologyChanged) {
		for (size_t i = 0; i < devices.size(); ++i) {
			if (monitoredDevices_[i]->paIndex != devices[i].paIndex) {
				topologyChanged = true;
				break;
			}
		}
	}

	if (topologyChanged) {
		stopAll();
		monitoredDevices_.reserve(devices.size());
		for (const auto& device : devices) {
			auto monitoredDevice = std::make_unique<MonitoredDevice>();
			monitoredDevice->paIndex = device.paIndex;
			monitoredDevices_.push_back(std::move(monitoredDevice));
		}
	}

	for (const auto& device : monitoredDevices_) {
		if (!device) {
			continue;
		}

		if (device->paIndex == selectedPaIndex) {
			stopDevice(*device);
			device->startAttempted = false;
			continue;
		}

		if (!device->stream && !device->startAttempted) {
			startDevice(*device);
		}
	}
}

std::array<float, 2> AudioInputLevelMonitor::getStereoLevels(const size_t deviceListIndex) const {
	if (deviceListIndex >= monitoredDevices_.size() || !monitoredDevices_[deviceListIndex]) {
		return {0.0f, 0.0f};
	}

	const MonitoredDevice& device = *monitoredDevices_[deviceListIndex];
	return {device.leftLevel.load(), device.rightLevel.load()};
}

void AudioInputLevelMonitor::stopAll() {
	for (const auto& device : monitoredDevices_) {
		if (device) {
			stopDevice(*device);
		}
	}
	monitoredDevices_.clear();
}

void AudioInputLevelMonitor::stopDevice(MonitoredDevice& device) {
	if (device.stream) {
		Pa_StopStream(device.stream);
		Pa_CloseStream(device.stream);
		device.stream = nullptr;
	}
	device.channelCount = 0;
	device.leftLevel.store(0.0f);
	device.rightLevel.store(0.0f);
}

bool AudioInputLevelMonitor::startDevice(MonitoredDevice& device) {
	device.startAttempted = true;

	const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(device.paIndex);
	if (!deviceInfo || deviceInfo->maxInputChannels < 1) {
		return false;
	}

	device.channelCount = std::min(2, deviceInfo->maxInputChannels);

	PaStreamParameters inputParameters{};
	inputParameters.device = device.paIndex;
	inputParameters.channelCount = device.channelCount;
	inputParameters.sampleFormat = paFloat32;
	inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;

	PaStream* stream = nullptr;
	const PaError openErr = Pa_OpenStream(
		&stream,
		&inputParameters,
		nullptr,
		deviceInfo->defaultSampleRate,
		256,
		paClipOff,
		monitorCallback,
		&device);

	if (openErr != paNoError) {
		device.channelCount = 0;
		return false;
	}

	const PaError startErr = Pa_StartStream(stream);
	if (startErr != paNoError) {
		Pa_CloseStream(stream);
		device.channelCount = 0;
		return false;
	}

	device.stream = stream;
	return true;
}

int AudioInputLevelMonitor::monitorCallback(const void* input, [[maybe_unused]] void* output,
											const unsigned long frameCount,
											[[maybe_unused]] const PaStreamCallbackTimeInfo* timeInfo,
											[[maybe_unused]] PaStreamCallbackFlags statusFlags,
											void* userData) {
	auto* device = static_cast<MonitoredDevice*>(userData);
	if (!device || !input || device->channelCount < 1) {
		return paContinue;
	}

	const auto* inBuffer = static_cast<const float*>(input);
	float leftPeak = 0.0f;
	float rightPeak = 0.0f;
	const size_t channels = static_cast<size_t>(device->channelCount);
	for (unsigned long frame = 0; frame < frameCount; ++frame) {
		const size_t offset = static_cast<size_t>(frame) * channels;
		const float left = std::abs(inBuffer[offset]);
		const float right = channels > 1 ? std::abs(inBuffer[offset + 1]) : left;
		leftPeak = std::max(leftPeak, left);
		rightPeak = std::max(rightPeak, right);
	}

	device->leftLevel.store(smoothedLevel(device->leftLevel.load(), std::clamp(leftPeak, 0.0f, 1.0f)));
	device->rightLevel.store(smoothedLevel(device->rightLevel.load(), std::clamp(rightPeak, 0.0f, 1.0f)));
	return paContinue;
}
