#pragma once

#include <portaudio.h>

#include <atomic>
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
	};

	AudioInput();
	~AudioInput();

	static std::vector<DeviceInfo> getInputDevices();
	bool initStream(int deviceIndex, int numChannels = 1);
	bool pauseStream();
	bool resumeStream();
	bool isStreamActive() const;
	AudioProcessor::SpectralData getSpectralData() const;
	FFTProcessor& getFFTProcessor(size_t channel = 0) { return processor.getFFTProcessor(channel); }
	const FFTProcessor& getFFTProcessor(size_t channel = 0) const { return processor.getFFTProcessor(channel); }
	AudioProcessor& getAudioProcessor() { return processor; }
	const AudioProcessor& getAudioProcessor() const { return processor; }

	void setEQGains(float low, float mid, float high) { processor.setEQGains(low, mid, high); }

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

	void stopStream();
	static int audioCallback(const void* input, void* output, unsigned long frameCount,
							 const PaStreamCallbackTimeInfo* timeInfo,
							 PaStreamCallbackFlags statusFlags, void* userData);
};
