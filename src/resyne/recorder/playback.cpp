#include "resyne/recorder/recorder.h"
#include "resyne/recorder/embedded_source_utils.h"
#include "resyne/recorder/reconstruction_utils.h"
#include "imgui.h"

#include <algorithm>
#include <condition_variable>
#include <optional>
#include "audio/processing/audio_processor.h"
#include "utilities/cancellation.h"
#include <mutex>

namespace ReSyne {

void appendRecordingFrames(RecorderState& state, AudioProcessor::BufferedFrames frames);

class PlaybackPreparation {
public:
    struct Request {
        std::uint64_t serial = 0, generation = 0;
        AudioMetadata metadata;
        std::shared_ptr<const std::vector<AudioColourSample>> samples;
        std::shared_ptr<const std::vector<float>> audio;
        int device = -1;
        bool loop = true, rebuild = false;
        AudioProcessor* recording = nullptr;
        std::stop_token cancellation;
    };
    struct Result {
        Request request;
        std::unique_ptr<AudioOutput> output;
        std::vector<AudioColourSample> loaded;
        std::shared_ptr<const std::vector<float>> audio;
        AudioProcessor::BufferedFrames tail;
        std::uint64_t count = 0;
        float rate = 0;
        std::string error;
    };
    PlaybackPreparation() : worker_([this](std::stop_token stop) { run(stop); }) {}
    ~PlaybackPreparation() {
        cancel();
        worker_.request_stop();
        wake_.notify_all();
    }
    bool idle() { std::lock_guard lock(mutex_); return !busy_.load() && !pending_ && retired_.empty(); }
    void cancel() {
        std::lock_guard lock(mutex_);
        cancellation_.request_stop();
        pending_.reset();
        if (result_ && result_->output) retired_.push_back(std::move(result_->output));
        result_.reset();
        ++serial_;
        wake_.notify_all();
    }
    void request(Request request) {
        std::lock_guard lock(mutex_);
        cancellation_.request_stop();
        cancellation_ = {};
        request.cancellation = cancellation_.get_token();
        request.serial = ++serial_;
        pending_ = std::move(request);
        if (result_ && result_->output) retired_.push_back(std::move(result_->output));
        result_.reset();
        wake_.notify_one();
    }
    std::optional<Result> take() {
        std::lock_guard lock(mutex_);
        auto result = std::move(result_);
        result_.reset();
        return result;
    }
    void retire(std::unique_ptr<AudioOutput> output) {
        if (!output) return;
        std::lock_guard lock(mutex_);
        retired_.push_back(std::move(output));
        wake_.notify_one();
    }
private:
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    std::vector<std::unique_ptr<AudioOutput>> retired_;
    std::stop_source cancellation_;
    std::uint64_t serial_ = 0;
    std::atomic<bool> busy_{false};
    std::jthread worker_;
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            Request request;
            std::vector<std::unique_ptr<AudioOutput>> retired;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, stop, [&] { return pending_.has_value() || !retired_.empty(); });
                if (stop.stop_requested()) return;
                retired = std::move(retired_);
                retired_.clear();
                if (!pending_) { busy_.store(true); lock.unlock(); retired.clear(); busy_.store(false); continue; }
                busy_.store(true);
                request = std::move(*pending_);
                pending_.reset();
            }
            retired.clear();
            struct Scope {
                std::atomic<bool>& busy;
                ~Scope() { busy.store(false); }
            } scopeBusy{busy_};
            Result result;
            result.request = std::move(request);
            auto& job = result.request;
            Utilities::CancellationScope scope(job.cancellation);
            try {
                Utilities::checkCancellation();
                const auto progress = [](float) { Utilities::checkCancellation(); };
                if (job.recording) result.tail = job.recording->finishRecordingAnalysis(result.count, result.rate);
                else {
                    if ((!job.samples || job.samples->empty()) && job.metadata.lazyAsset) {
                        if (!SequenceExporter::hydrateRsynSamples(job.metadata, result.loaded, progress))
                            throw std::runtime_error("Archived spectra could not be loaded");
                        job.samples = std::make_shared<const std::vector<AudioColourSample>>(std::move(result.loaded));
                    }
                    result.audio = job.rebuild ? nullptr : job.audio;
                    if (!result.audio || result.audio->empty()) {
                        std::vector<float> audio;
                        std::string error;
                        if (!job.rebuild && SequenceExporter::hydrateRsynSource(job.metadata, progress))
                            EmbeddedSourceUtils::decodeEmbeddedSourceAudio(job.metadata, audio, error);
                        if (audio.empty() && job.samples && !job.samples->empty())
                            RecorderReconstruction::buildPlaybackAudio(*job.samples, job.metadata, audio, progress);
                        if (audio.empty()) throw std::runtime_error("Playback audio could not be prepared");
                        result.audio = std::make_shared<const std::vector<float>>(std::move(audio));
                    }
                    Utilities::checkCancellation();
                    result.output = std::make_unique<AudioOutput>();
                    if (!result.output->initOutputStream(job.metadata.sampleRate, static_cast<int>(job.metadata.channels), job.device))
                        throw std::runtime_error("Unable to open the selected output device");
                    result.output->setAudioData(result.audio, job.metadata.channels);
                    result.output->setLoopEnabled(job.loop);
                    Utilities::checkCancellation();
                }
            } catch (const Utilities::OperationCancelled&) { continue; }
            catch (const std::exception& error) { result.error = error.what(); }
            std::lock_guard lock(mutex_);
            if (!stop.stop_requested() && job.serial == serial_) result_ = std::move(result);
        }
    }
};

namespace {
bool refreshPlaybackOutputPreservingPosition(RecorderState& state) {
    return Recorder::refreshPlaybackOutput(state);
}
}

bool Recorder::playbackPreparationIdle(RecorderState& state) {
    return !state.playbackPreparation || state.playbackPreparation->idle();
}

void Recorder::retirePlaybackOutput(RecorderState& state) {
    if (!state.audioOutput) return;
    state.audioOutput->pause();
    if (!state.playbackPreparation) state.playbackPreparation = std::make_shared<PlaybackPreparation>();
    state.playbackPreparation->retire(std::move(state.audioOutput));
}

void Recorder::cancelPlaybackPreparation(RecorderState& state) {
    if (state.playbackPreparation) state.playbackPreparation->cancel();
    state.playbackPreparing = false;
    state.recordingFinishing = false;
    state.playbackStartPending = false;
}

bool Recorder::refreshPlaybackOutput(RecorderState& state) {
    if (state.audioDevicesRefreshing || state.recordingFinishing) return false;
    PlaybackPreparation::Request request;
    {
        std::lock_guard lock(state.samplesMutex);
        if (state.samples.empty() && !state.metadata.lazyAsset && state.playbackAudio.empty()) return false;
        request.metadata = state.metadata;
        if (request.metadata.sampleRate <= 0) request.metadata.sampleRate = state.fallbackSampleRate;
        request.samples = state.samples.snapshot();
        request.audio = state.playbackAudio.snapshot();
        request.generation = state.presentationSourceGeneration;
    }
    if (request.metadata.sampleRate <= 0) return false;
    if (state.audioOutput) {
        state.playbackStartPending = state.playbackStartPending || state.audioOutput->isPlaying();
        const auto total = state.audioOutput->getTotalFrames();
        if (total > 0) state.timeline.scrubberNormalisedPosition = static_cast<float>(state.audioOutput->getPresentationPosition() / static_cast<double>(total));
        state.audioOutput->pause();
    }
    request.device = state.outputDeviceIndex;
    request.loop = state.loopEnabled;
    if (!state.playbackPreparation) state.playbackPreparation = std::make_shared<PlaybackPreparation>();
    state.playbackPreparation->request(std::move(request));
    state.playbackPreparing = true;
    state.statusMessage = "Preparing playback";
    state.statusMessageTimer = 4;
    return true;
}

void Recorder::reconstructAudio(RecorderState& state) {
    state.playbackAudio.clear();
    refreshPlaybackOutput(state);
}

void Recorder::finishRecordingLater(RecorderState& state, AudioProcessor& processor) {
    if (!state.playbackPreparation) state.playbackPreparation = std::make_shared<PlaybackPreparation>();
    PlaybackPreparation::Request request;
    request.generation = state.presentationSourceGeneration;
    request.recording = &processor;
    state.playbackPreparation->request(std::move(request));
    state.recordingFinishing = true;
    state.statusMessage = "Finalising recording";
    state.statusMessageTimer = 4;
}

void Recorder::advancePlaybackPreparation(RecorderState& state) {
    if (!state.playbackPreparation) return;
    auto ready = state.playbackPreparation->take();
    if (!ready || ready->request.generation != state.presentationSourceGeneration) return;
    if (ready->request.recording) {
        state.recordingFinishing = false;
        if (!ready->error.empty()) { state.statusMessage = ready->error; state.statusMessageTimer = 5; return; }
        appendRecordingFrames(state, std::move(ready->tail));
        {
            std::lock_guard lock(state.samplesMutex);
            auto count = ready->count;
            if (state.samples.size() >= RecorderState::MAX_RECORDING_SAMPLES)
                count = std::min(count, static_cast<std::uint64_t>(std::max(0.0, std::round(state.samples.back().timestamp * static_cast<double>(state.metadata.sampleRate) + static_cast<double>(state.metadata.fftSize) * .5))));
            state.metadata.originalSampleCount = count;
            state.metadata.durationSeconds = ready->rate > 0 ? static_cast<double>(count) / static_cast<double>(ready->rate) : 0;
            state.metadata.numFrames = state.samples.size();
        }
        reconstructAudio(state);
        return;
    }
    state.playbackPreparing = false;
    if (!ready->error.empty()) {
        state.statusMessage = ready->error;
        state.statusMessageTimer = 5;
        state.outputDeviceIndex = state.activeOutputDeviceIndex;
        state.playbackRouteFailed = true;
        if (state.playbackStartPending && state.audioOutput) state.audioOutput->play();
        state.playbackStartPending = false;
        return;
    }
    {
        std::lock_guard lock(state.samplesMutex);
        state.metadata = std::move(ready->request.metadata);
        if (state.samples.empty() && ready->request.samples) state.samples.share(ready->request.samples);
        state.playbackAudio.share(ready->audio);
    }
    auto previous = std::move(state.audioOutput);
    state.audioOutput = std::move(ready->output);
    state.playbackPreparation->retire(std::move(previous));
    state.activeOutputDeviceIndex = ready->request.device;
    state.isPlaybackInitialised = true;
    const auto total = state.audioOutput->getTotalFrames();
    if (total > 0) state.audioOutput->seek(std::min(total - 1, static_cast<std::size_t>(static_cast<double>(state.timeline.scrubberNormalisedPosition) * static_cast<double>(total))));
    if (state.playbackStartPending) state.audioOutput->play();
    state.playbackStartPending = false;
    state.statusMessage.clear();
}

bool Recorder::ensureRsynSamplesLoaded(RecorderState& state, const std::function<void(float)>& progress) {
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        if (!state.samples.empty()) {
            return true;
        }
        if (state.metadata.lazyAsset == nullptr) {
            return false;
        }
    }

    AudioMetadata metadata;
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        metadata = state.metadata;
    }

    std::vector<AudioColourSample> loadedSamples;
    if (!SequenceExporter::hydrateRsynSamples(metadata, loadedSamples, progress) || loadedSamples.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.metadata = std::move(metadata);
        state.samples = std::move(loadedSamples);
    }

    return true;
}

void Recorder::startPlayback(RecorderState& state) {
    if (state.audioDevicesRefreshing) { state.playbackStartPending = true; return; }
    if (state.playbackOutputRefreshPending) {
        const double now = ImGui::GetTime();
        if (now < state.playbackRouteReadyTime) {
            state.playbackStartPending = true;
            state.statusMessage = "Preparing Bluetooth playback";
            state.statusMessageTimer = static_cast<float>(state.playbackRouteReadyTime - now);
            return;
        }

        state.playbackOutputRefreshPending = false;
        refreshPlaybackOutputPreservingPosition(state);
    }

    if (state.playbackPreparing || state.recordingFinishing) { state.playbackStartPending = true; return; }

    const bool initialisePlayback = !state.isPlaybackInitialised || state.playbackAudio.empty();
    if (initialisePlayback) {
        state.playbackStartPending = true;
        refreshPlaybackOutput(state);
        return;
    }

    if (!state.audioOutput) {
        return;
    }

    if (!state.playbackAudio.empty()) {
        size_t totalFrames = state.audioOutput->getTotalFrames();
        size_t currentPosition = state.audioOutput->getPlaybackPosition();
        if (totalFrames > 0 && currentPosition >= totalFrames - 1) {
            state.audioOutput->seek(0);
            state.timeline.scrubberNormalisedPosition = 0.0f;
        } else if (initialisePlayback) {
            if (totalFrames == 0) {
                const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
                totalFrames = numChannels > 0 ? state.playbackAudio.size() / numChannels : state.playbackAudio.size();
            }
            if (totalFrames > 0) {
                size_t startFrame = static_cast<size_t>(
                    std::clamp(state.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                    static_cast<float>(totalFrames));
                startFrame = std::min(startFrame, totalFrames - 1);
                size_t currentPos = state.audioOutput->getPlaybackPosition();
                if (currentPos != startFrame) {
                    state.audioOutput->seek(startFrame);
                }
            }
        }
    }

    state.audioOutput->setLoopEnabled(state.loopEnabled);
    state.audioOutput->play();
}

bool Recorder::completePendingPlaybackRouteRefresh(RecorderState& state) {
    if (!state.playbackOutputRefreshPending || ImGui::GetTime() < state.playbackRouteReadyTime) {
        return false;
    }

    const bool startPending = state.playbackStartPending;
    state.playbackOutputRefreshPending = false;
    state.playbackStartPending = false;

    const bool refreshed = refreshPlaybackOutputPreservingPosition(state);
    if (refreshed && startPending) {
        if (state.statusMessage == "Preparing Bluetooth playback") {
            state.statusMessage.clear();
            state.statusMessageTimer = 0.0f;
        }
        startPlayback(state);
    }
    return refreshed;
}

void Recorder::pausePlayback(RecorderState& state) {
    state.playbackStartPending = false;
    if (state.audioOutput) {
        state.audioOutput->pause();
    }
}

void Recorder::stopPlayback(RecorderState& state) {
    state.playbackStartPending = false;
    if (state.audioOutput) {
        state.audioOutput->stop();
    }
}

void Recorder::togglePlayback(RecorderState& state) {
    if (state.importPhase != 0 || state.isRecording || state.recordingCountdownActive || state.recordingFinishing) return;
    if (state.playbackStartPending || (state.audioOutput && state.audioOutput->isPlaying())) {
        pausePlayback(state);
    } else {
        startPlayback(state);
    }
}

void Recorder::seekPlayback(RecorderState& state, float normalisedPosition) {
    float clamped = std::clamp(normalisedPosition, 0.0f, 1.0f);
    state.timeline.scrubberNormalisedPosition = clamped;

    if (state.audioOutput && !state.playbackAudio.empty()) {
        size_t totalFrames = state.audioOutput->getTotalFrames();
        if (totalFrames == 0) {
            const uint32_t numChannels = !state.samples.empty() ? state.samples.front().channels : 1;
            totalFrames = numChannels > 0 ? state.playbackAudio.size() / numChannels : state.playbackAudio.size();
        }
        if (totalFrames > 0) {
            size_t framePosition = static_cast<size_t>(clamped * static_cast<float>(totalFrames));
            framePosition = std::min(framePosition, totalFrames - 1);
            state.audioOutput->seek(framePosition);
        }
    }
}

}
