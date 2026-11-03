#pragma once

#include <portaudio.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "audio_processor.h"
#include "fft_processor.h"

class AudioInput {
public:
	struct DeviceInfo {
		std::string name;
		int paIndex;
		int maxChannels;
		bool allowLevelMonitoring = true;
		bool passiveMonitoringProtected = false;
        std::string identifier;
	};

	AudioInput();
	~AudioInput();

	static std::vector<DeviceInfo> getInputDevices();
    static std::string deviceIdentifier(int deviceIndex);
	bool initStream(int deviceIndex, int numChannels = 1);
	bool pauseStream();
	bool resumeStream();
	void closeStream();
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

	void discardBufferedFrames() { processor.discardBufferedFrames(); }

	int getChannelCount() const { return channelCount; }
	void setActiveChannel(int channel) {
		const int selected = channel >= 0 && channel < channelCount ? channel : -1;
        activeChannel.store(selected);
        processor.setPresentationChannel(selected);
	}
	float getSampleRate() const { return sampleRate; }
    double getInputLatency() const { return inputLatency.load(); }

private:
	PaStream* stream;
	AudioProcessor processor;
	std::atomic<float> sampleRate;
    std::atomic<double> inputLatency{0};
	std::atomic<int> channelCount;
    std::atomic<bool> streamActive{false};
	std::atomic<int> activeChannel;
	std::atomic<float> leftLevel;
	std::atomic<float> rightLevel;

    static void streamFinished(void* context);
	void stopStream();
	void updateStereoLevels(float left, float right, double elapsed);
	void resetStereoLevels();
	static int audioCallback(const void* input, void* output, unsigned long frameCount,
							 const PaStreamCallbackTimeInfo* timeInfo,
							 PaStreamCallbackFlags statusFlags, void* userData);
};

class AudioInputLevelMonitor {
public:
	AudioInputLevelMonitor();
	~AudioInputLevelMonitor();

    void setSuspended(bool value) { suspended_.store(value); }
    void closeAll() { stopAll(); }
	void syncDevices(const std::vector<AudioInput::DeviceInfo>& devices, int selectedPaIndex = -1);
	std::array<float, 2> getStereoLevels(size_t deviceListIndex) const;

private:
	struct MonitoredDevice;
    std::atomic<bool> suspended_{false};

	std::vector<std::unique_ptr<MonitoredDevice>> monitoredDevices_;

	void stopAll();
	static void stopDevice(MonitoredDevice& device);
	static bool startDevice(MonitoredDevice& device);
	static int monitorCallback(const void* input, void* output, unsigned long frameCount,
							   const PaStreamCallbackTimeInfo* timeInfo,
							   PaStreamCallbackFlags statusFlags, void* userData);
};
