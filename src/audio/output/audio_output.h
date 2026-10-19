#pragma once

#include <portaudio.h>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <memory>

#include "playback_equaliser.h"
#include "output_timeline.h"

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
	bool initOutputStream(float sampleRate, int channelCount = 1, int deviceIndex = -1, int framesPerBuffer = 512);
	void setAudioData(const std::vector<float>& audioSamples, size_t channelCount = 1);

	void play();
	void pause();
	void stop();
	bool isPlaying() const { return isPlaying_.load(); }
	std::array<float, 2> getStereoLevels() const {
		return {leftLevel_.load(), rightLevel_.load()};
	}

	void seek(size_t framePosition);
	size_t getPlaybackPosition() const { return playbackPosition_.load(); }
    double getPresentationPosition() const;
	size_t getTotalSamples() const { return totalSamples_.load(); }
	size_t getTotalFrames() const {
		const size_t channelCount = channelCount_.load();
		const size_t totalSamples = totalSamples_.load();
		return channelCount > 0 ? totalSamples / channelCount : totalSamples;
	}

	void setLoopEnabled(bool enabled) { loopEnabled_.store(enabled); }
	bool isLoopEnabled() const { return loopEnabled_.load(); }
	void setPlaybackEQEnabled(bool enabled);
	void setPlaybackEQGains(float low, float mid, float high);

	void clearAudioData();

private:
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<void*>::is_always_lock_free);
	PaStream* stream_;
    struct AudioStorage {
        std::vector<float> samples;
        std::size_t channels = 1;
        std::uint64_t generation = 0;
    };
    std::unique_ptr<AudioStorage> audioBuffer_;
    std::vector<std::unique_ptr<AudioStorage>> retiredBuffers_;
    std::atomic<const AudioStorage*> publishedBuffer_{nullptr};
    std::atomic<std::uint32_t> callbackReaders_{0};
    std::atomic<std::uint64_t> presentationGeneration_{0};
    std::uint64_t nextGeneration_ = 0;
    std::uint64_t activeGeneration_ = 0;
	mutable std::mutex bufferMutex_;
    struct OutputInterval {
        std::atomic<std::uint64_t> version{0};
        std::atomic<double> dacTime{0.0};
        std::atomic<double> position{0.0};
        std::atomic<double> duration{0.0};
        std::atomic<double> framesPerSecond{0.0};
        std::atomic<double> totalFrames{0.0};
        std::atomic<bool> loop{false};
        std::atomic<std::uint64_t> session{0};
        std::atomic<std::uint64_t> generation{0};
    };
    std::array<OutputInterval, 256> outputIntervals_{};
    std::atomic<std::uint64_t> writtenIntervals_{0};
    std::atomic<std::uint64_t> clockSession_{1};
    std::atomic<bool> clockAvailable_{false};
    AudioOutputTimeline outputTimeline_;
    std::uint64_t outputTimelineSession_ = 0;

	std::atomic<size_t> playbackPosition_;
	std::atomic<size_t> totalSamples_;
	std::atomic<bool> isPlaying_;
	std::atomic<bool> loopEnabled_;
	std::atomic<float> actualSampleRate_;
	std::atomic<float> playbackStep_;
	std::atomic<size_t> channelCount_;
	std::atomic<size_t> outputChannelCount_{0};
	double playbackCursor_;

    std::atomic<std::uint64_t> seekVersion_{0};
    std::atomic<std::uint64_t> seekGeneration_{0};
    std::atomic<size_t> pendingSeekPosition_{0};
    std::atomic<bool> pendingSeekFade_{false};
    std::uint64_t appliedSeekVersion_ = 0;
    size_t seekFadeRemaining_ = 0;
    void publishSeek(size_t position, bool fade);
	double oldSeekCursor_;
	PlaybackEqualiser playbackEqualiser_;
	std::atomic<float> leftLevel_;
	std::atomic<float> rightLevel_;

	void updateStereoLevels(float left, float right, double elapsed);
	void resetStereoLevels();
	static int audioCallback(const void* input, void* output,
						 unsigned long frameCount,
						 const PaStreamCallbackTimeInfo* timeInfo,
						 PaStreamCallbackFlags statusFlags,
						 void* userData);
};
