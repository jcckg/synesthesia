#pragma once

#include <portaudio.h>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <memory>

class AudioOutput {
public:
	struct DeviceInfo {
		std::string name;
		int paIndex;
		int maxChannels;
	};

	AudioOutput();
	~AudioOutput();

	static std::vector<DeviceInfo> getOutputDevices();
	bool initOutputStream(float sampleRate, int deviceIndex = -1, int framesPerBuffer = 512);
	void setAudioData(const std::vector<float>& audioSamples);

	void play();
	void pause();
	void stop();
	bool isPlaying() const { return isPlaying_.load(); }

	void seek(size_t samplePosition);
	size_t getPlaybackPosition() const { return playbackPosition_.load(); }
	size_t getTotalSamples() const { return totalSamples_.load(); }

	void setLoopEnabled(bool enabled) { loopEnabled_.store(enabled); }
	bool isLoopEnabled() const { return loopEnabled_.load(); }
	float getRequestedSampleRate() const { return requestedSampleRate_.load(); }
	float getActualSampleRate() const { return actualSampleRate_.load(); }
	float getPlaybackRateRatio() const;
	float getPlaybackStep() const { return playbackStep_.load(); }

	void clearAudioData();

private:
	PaStream* stream_;
	std::shared_ptr<std::vector<float>> audioBuffer_;
	std::mutex bufferMutex_;

	std::atomic<size_t> playbackPosition_;
	std::atomic<size_t> totalSamples_;
	std::atomic<bool> isPlaying_;
	std::atomic<bool> loopEnabled_;
	std::atomic<float> requestedSampleRate_;
	std::atomic<float> actualSampleRate_;
	std::atomic<float> playbackStep_;
	double playbackCursor_;

	std::atomic<bool> seekCrossfadeActive_;
	std::atomic<size_t> pendingSeekPosition_;
	std::atomic<size_t> seekFadeRemaining_;
	double oldSeekCursor_;

	static int audioCallback(const void* input, void* output,
						 unsigned long frameCount,
						 const PaStreamCallbackTimeInfo* timeInfo,
						 PaStreamCallbackFlags statusFlags,
						 void* userData);
};
