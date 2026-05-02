#pragma once

#include <portaudio.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "audio_processor.h"
#include "dc_filter.h"
#include "noise_gate.h"
#include "fft_processor.h"

class AudioInput {
public:
	struct DeviceInfo {
		std::string name;
		int paIndex;
		int maxChannels;
		bool allowLevelMonitoring = true;
	};

	AudioInput();
	~AudioInput();

	static std::vector<DeviceInfo> getInputDevices();
	bool initStream(int deviceIndex, int numChannels = 1);
	bool pauseStream();
	bool resumeStream();
	bool isStreamActive() const;
	std::array<float, 2> getStereoLevels() const {
		return {leftLevel.load(), rightLevel.load()};
	}
	AudioProcessor::SpectralData getSpectralData() const;
	void copySpectralData(AudioProcessor::SpectralData& out) const { processor.copySpectralData(out); }
	FFTProcessor& getFFTProcessor(size_t channel = 0) { return processor.getFFTProcessor(channel); }
	const FFTProcessor& getFFTProcessor(size_t channel = 0) const { return processor.getFFTProcessor(channel); }
	AudioProcessor& getAudioProcessor() { return processor; }
	const AudioProcessor& getAudioProcessor() const { return processor; }

	void setEQGains(float low, float mid, float high) { processor.setEQGains(low, mid, high); }
	void discardBufferedFrames() { processor.discardBufferedFrames(); }

	int getChannelCount() const { return channelCount; }
	void setActiveChannel(int channel) {
		activeChannel.store(channel >= 0 && channel < channelCount ? channel : 0);
	}
	float getSampleRate() const { return sampleRate; }

private:
	PaStream* stream;
	AudioProcessor processor;
	DCFilter dcFilter;
	NoiseGate noiseGate;
	float sampleRate;
	int channelCount;
	std::atomic<int> activeChannel;
	std::atomic<float> leftLevel;
	std::atomic<float> rightLevel;

	void stopStream();
	void updateStereoLevels(float left, float right);
	void resetStereoLevels();
	static int audioCallback(const void* input, void* output, unsigned long frameCount,
							 const PaStreamCallbackTimeInfo* timeInfo,
							 PaStreamCallbackFlags statusFlags, void* userData);
};

class AudioInputLevelMonitor {
public:
	AudioInputLevelMonitor();
	~AudioInputLevelMonitor();

	void syncDevices(const std::vector<AudioInput::DeviceInfo>& devices, int selectedPaIndex = -1);
	std::array<float, 2> getStereoLevels(size_t deviceListIndex) const;

private:
	struct MonitoredDevice;

	std::vector<std::unique_ptr<MonitoredDevice>> monitoredDevices_;

	void stopAll();
	static void stopDevice(MonitoredDevice& device);
	static bool startDevice(MonitoredDevice& device);
	static int monitorCallback(const void* input, void* output, unsigned long frameCount,
							   const PaStreamCallbackTimeInfo* timeInfo,
							   PaStreamCallbackFlags statusFlags, void* userData);
};
