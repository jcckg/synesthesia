#pragma once

#include <array>
#include <condition_variable>
#include "utilities/timing_statistics.h"
#include <atomic>

#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "fft_processor.h"
#include "audio/analysis/presentation/live_presentation.h"

struct AudioCaptureTiming {
    double adcTime = std::numeric_limits<double>::quiet_NaN();
    double callbackTime = std::numeric_limits<double>::quiet_NaN();
    bool overflow = false;
    bool underflow = false;
};

class AudioProcessor {
public:
    struct TimingStatistics {
        std::uint64_t callbacks = 0, overflows = 0, underflows = 0, droppedFrames = 0;
        std::size_t callbackFrames = 0;
        double callbackInterval = 0, captureDeliveryAge = 0, queueAge = 0, processingTime = 0;
        double displayedFrameAge = 0, analysisToDisplayAge = 0;
        bool captureTimingAvailable = false;
        Utilities::TimingStatistics::Snapshot callbackDistribution, queueDistribution, processingDistribution, displayDistribution;
    };

	struct SpectralData {
		std::vector<std::vector<float>> magnitudes;
		std::vector<std::vector<float>> phases;
		float dominantFrequency = 0.0f;
		float sampleRate = 0.0f;
		float momentaryLoudnessLUFS = -200.0f;
		float spectralFlux = 0.0f;
		uint64_t frameCounter = 0;
		int hopSize = FFTProcessor::HOP_SIZE;
		bool onsetDetected = false;
        std::shared_ptr<const SpectralPresentation::LiveResult> presentation;
	};

	using BufferedFrames = std::vector<std::vector<FFTProcessor::FFTFrame>>;

    static int liveHopSize(float sampleRate);
    void setPresentationChannel(int channel) { requestedChannel.store(channel, std::memory_order_relaxed); }

	AudioProcessor();
	~AudioProcessor();

	void queueAudioData(const float* buffer, size_t numSamples, float sampleRate, size_t numChannels, const AudioCaptureTiming& timing = {});

    [[nodiscard]] TimingStatistics getTimingStatistics() const;
    void markPresentationDisplayed(const SpectralPresentation::LiveResult& frame);
	SpectralData getSpectralData() const;
	void copySpectralData(SpectralData& out) const;
	BufferedFrames consumeBufferedFrames();
	void discardBufferedFrames();
    void beginRecordingAnalysis(int hopSize);
    BufferedFrames consumeRecordingFrames();
    BufferedFrames finishRecordingAnalysis(std::uint64_t& sampleCount, float& sampleRate);
    void cancelRecordingAnalysis();

    void setPresentationSettings(const RSYNPresentationSettings& settings);
    [[nodiscard]] std::shared_ptr<const SpectralPresentation::LiveResult> getPresentation() const;
	void reset(bool preserveColour = false);
	void start();
    void setCaptureActive(bool active);
    void markCaptureStopped() { captureActive.store(false, std::memory_order_release); }
	void stop();
	uint64_t getDroppedBufferCount() const { return droppedBufferCount.load(std::memory_order_relaxed); }
    uint64_t getDroppedFrameCount() const { return droppedFrameCount.load(std::memory_order_relaxed); }

	FFTProcessor& getFFTProcessor(size_t channel = 0);
	const FFTProcessor& getFFTProcessor(size_t channel = 0) const;
	size_t getChannelCount() const;

private:
	static constexpr size_t QUEUE_SIZE = 64;
	static constexpr size_t MAX_SAMPLES = 4096;

	struct AudioBuffer {
		std::vector<float> data = std::vector<float>(MAX_SAMPLES);
		size_t sampleCount = 0;
		float sampleRate = 44100.0f;
		size_t numChannels = 1;
        std::uint64_t firstFrame = 0;
        double queuedTime = 0, captureEndTime = 0;
	};

	std::array<AudioBuffer, QUEUE_SIZE> audioQueue;
	std::atomic<size_t> writeIndex;
	std::atomic<size_t> readIndex;
	std::thread workerThread;
	std::atomic<bool> running;
    std::atomic<bool> workerActive{false};
    std::atomic<bool> captureActive{true};
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::uint64_t sourceFramePosition = 0;
    std::atomic<std::uint64_t> capturedFrameCount{0};
    std::atomic<std::uint64_t> processedFrameCount{0};
    std::atomic<std::uint64_t> droppedFrameCount{0};
	std::atomic<uint64_t> droppedBufferCount{0};

    double previousCallbackTime = 0;
    Utilities::TimingStatistics callbackDistribution, queueDistribution, processingDistribution, displayDistribution;
    std::atomic<std::uint64_t> callbackCount{0}, overflowCount{0}, underflowCount{0};
    std::atomic<std::size_t> callbackFrames{0};
    std::atomic<double> callbackInterval{0}, captureDeliveryAge{0}, queueAge{0}, processingTime{0};
    std::atomic<double> displayedFrameAge{0}, analysisToDisplayAge{0};
    std::atomic<bool> captureTimingAvailable{false};
    std::uint64_t analysisFramePosition = 0;
    float liveAnalysisRate = 0;
    std::atomic<int> requestedChannel{-1};
    int presentationChannel = -1;

	mutable std::mutex processorMutex;
	std::vector<std::unique_ptr<FFTProcessor>> fftProcessors;
	size_t activeChannelCount = 0;


	std::vector<float> channelBufferScratch;
    std::vector<float> channelLoudnessScratch;
    BufferedFrames presentationFrames;
	SpectralData stagingSpectralData;
    SpectralPresentation::LivePresentation livePresentation;
    std::shared_ptr<const SpectralPresentation::LiveResult> latestPresentation;
    std::mutex presentationSettingsMutex;
    RSYNPresentationSettings pendingPresentationSettings;
    bool recordingAnalysis = false;
    std::uint64_t recordingGeneration = 0;
    int recordingHop = 0;
    std::vector<std::unique_ptr<FFTProcessor>> recordingProcessors;
    BufferedFrames recordingPendingFrames;
    std::uint64_t recordingSampleCount = 0;
    float recordingRate = 0.0f;
    BufferedFrames drainRecordingFramesLocked(bool finish);

	mutable std::mutex resultsMutex;
	SpectralData currentSpectralData;

	void processingThreadFunc();
	void processBuffer(const AudioBuffer& buffer);
	void ensureProcessorCountLocked(size_t numChannels);
	FFTProcessor* getProcessorForChannel(size_t channel);
	const FFTProcessor* getProcessorForChannel(size_t channel) const;
};
