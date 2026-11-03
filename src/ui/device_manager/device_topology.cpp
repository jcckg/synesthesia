#include "ui/device_manager/device_topology.h"
#include "ui/ui.h"
#include "resyne/recorder/recorder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

#include "audio/input/device_identity.h"
#include "audio/device_control.h"

namespace UI {
namespace {

template <typename Devices>
std::string identity(const Devices& devices, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= devices.size()) return {};
    const auto& selected = devices[static_cast<std::size_t>(index)];
    return selected.identifier.empty() ? selected.name : selected.identifier;
}

template <typename Devices>
int indexFor(const Devices& devices, const std::string& id) {
    for (std::size_t i = 0; i < devices.size(); ++i)
        if (identity(devices, static_cast<int>(i)) == id) return static_cast<int>(i);
    return -1;
}
}

struct DeviceTopology::Impl {
    struct Result {
        std::vector<AudioInput::DeviceInfo> inputs;
        std::vector<AudioOutput::DeviceInfo> outputs;
        int inputIndex = -1, outputIndex = -1;
        bool inputReady = false;
        std::string error;
    };
    AudioInput& input;
    UIState& state;
    std::vector<AudioInput::DeviceInfo>& inputs;
    std::vector<AudioOutput::DeviceInfo>& outputs;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Result> result;
    std::atomic<bool> changed{false}, complete{false};
    std::string preferredInput, preferredOutput;
    bool refreshing = false, waiting = false, resumeOutput = false, captureInput = false;
    int selectedChannel = 0;
    std::jthread refreshWorker;
    std::jthread monitor;
    Impl(AudioInput& source, UIState& ui, std::vector<AudioInput::DeviceInfo>& in, std::vector<AudioOutput::DeviceInfo>& out)
        : input(source), state(ui), inputs(in), outputs(out), monitor([this](std::stop_token stop) {
            auto previous = AudioDevices::topologyFingerprint();
            while (!stop.stop_requested()) {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
                lock.unlock();
                if (stop.stop_requested()) return;
                const auto current = AudioDevices::topologyFingerprint();
                if (!current.empty() && current != previous) { previous = current; changed.store(true); }
            }
        }) {}
    ~Impl() {
        monitor.request_stop();
        wake.notify_all();
        if (monitor.joinable()) monitor.join();
        if (refreshWorker.joinable()) refreshWorker.join();
    }
    void update() {
        auto& devices = state.deviceState;
        auto& recorder = state.resyneState.recorderState;
        if (!waiting && !refreshing) {
            if (devices.selectedDeviceIndex >= 0) preferredInput = identity(inputs, devices.selectedDeviceIndex);
            if (devices.selectedOutputDeviceIndex >= 0) preferredOutput = identity(outputs, devices.selectedOutputDeviceIndex);
            if (!changed.exchange(false)) return;
            waiting = true;
            resumeOutput = recorder.playbackStartPending || (recorder.audioOutput && recorder.audioOutput->isPlaying());
            recorder.playbackStartPending = resumeOutput;
            captureInput = (devices.selectedDeviceIndex < 0 && captureInput) || input.isStreamActive() || (devices.selectedDeviceIndex >= 0 &&
                static_cast<std::size_t>(devices.selectedDeviceIndex) < inputs.size() && !inputs[static_cast<std::size_t>(devices.selectedDeviceIndex)].passiveMonitoringProtected);
            selectedChannel = devices.selectedChannelIndex;
            if (recorder.isRecording) ReSyne::Recorder::stopRecording(recorder);
            devices.refreshing = true;
            recorder.audioDevicesRefreshing = true;
            state.inputLevelMonitor.setSuspended(true);
        }
        if (waiting) {
            ReSyne::Recorder::advancePlaybackPreparation(recorder);
            if (recorder.recordingFinishing) return;
            resumeOutput = recorder.playbackStartPending || (recorder.audioOutput && recorder.audioOutput->isPlaying());
            ReSyne::Recorder::cancelPlaybackPreparation(recorder);
            recorder.playbackStartPending = resumeOutput;
            if (!ReSyne::Recorder::playbackPreparationIdle(recorder)) return;
            if (recorder.audioOutput) {
                const auto total = recorder.audioOutput->getTotalFrames();
                if (total > 0) recorder.timeline.scrubberNormalisedPosition = static_cast<float>(recorder.audioOutput->getPresentationPosition() / static_cast<double>(total));
            }
            auto oldOutput = std::move(recorder.audioOutput);
            waiting = false;
            refreshing = true;
            complete = false;
            refreshWorker = std::jthread([this, oldOutput = std::move(oldOutput)]() mutable {
                Result fresh;
                try {
                    std::lock_guard control(AudioDevices::controlMutex());
                    state.inputLevelMonitor.closeAll();
                    input.closeStream();
                    oldOutput.reset();
                    Pa_Terminate();
                    if (Pa_Initialize() != paNoError) throw std::runtime_error("Unable to refresh audio devices");
                    fresh.inputs = AudioInput::getInputDevices();
                    fresh.outputs = AudioOutput::getOutputDevices();
                    fresh.inputIndex = indexFor(fresh.inputs, preferredInput);
                    fresh.outputIndex = indexFor(fresh.outputs, preferredOutput);
                    if (captureInput && fresh.inputIndex >= 0) {
                        const auto& device = fresh.inputs[static_cast<std::size_t>(fresh.inputIndex)];
                        fresh.inputReady = input.initStream(device.paIndex, std::min(16, device.maxChannels));
                        if (fresh.inputReady) input.setActiveChannel(selectedChannel - 1);
                    }
                } catch (const std::exception& error) { fresh.error = error.what(); }
                {
                    std::lock_guard lock(mutex);
                    result = std::move(fresh);
                }
                complete.store(true, std::memory_order_release);
            });
        }
        if (!refreshing || !complete.load(std::memory_order_acquire)) return;
        if (refreshWorker.joinable()) refreshWorker.join();
        Result fresh;
        { std::lock_guard lock(mutex); fresh = std::move(*result); result.reset(); }
        devices.deviceNames.clear();
        devices.outputDeviceNames.clear();
        inputs = std::move(fresh.inputs);
        outputs = std::move(fresh.outputs);
        devices.deviceNamesPopulated = false;
        devices.outputDeviceNamesPopulated = false;
        devices.selectedDeviceIndex = fresh.inputIndex;
        devices.selectedOutputDeviceIndex = fresh.outputIndex;
        devices.selectedChannelIndex = 0;
        devices.channelNames.clear();
        devices.channelNameStrings.clear();
        if (fresh.inputIndex >= 0) {
            devices.channelNameStrings.push_back("All channels");
            for (int ch = 0; ch < input.getChannelCount(); ++ch) devices.channelNameStrings.push_back("Channel " + std::to_string(ch + 1));
            for (const auto& name : devices.channelNameStrings) devices.channelNames.push_back(name.c_str());
            devices.selectedChannelIndex = std::clamp(selectedChannel, 0, static_cast<int>(devices.channelNames.size()) - 1);
        }
        devices.streamError = captureInput && !fresh.inputReady;
        devices.streamErrorMessage = devices.streamError ? "Input device disconnected or unavailable" : "";
        devices.refreshing = false;
        recorder.audioDevicesRefreshing = false;
        state.inputLevelMonitor.setSuspended(false);
        refreshing = false;
        resumeOutput = recorder.playbackStartPending;
        recorder.isPlaybackInitialised = false;
        if (fresh.outputIndex >= 0) {
            recorder.outputDeviceIndex = outputs[static_cast<std::size_t>(fresh.outputIndex)].paIndex;
            recorder.playbackStartPending = resumeOutput;
            ReSyne::Recorder::refreshPlaybackOutput(recorder);
        } else if (!preferredOutput.empty()) {
            recorder.statusMessage = "Output device disconnected";
            recorder.statusMessageTimer = 5;
        }
        if (!fresh.error.empty()) { recorder.statusMessage = fresh.error; recorder.statusMessageTimer = 5; }
    }
};

DeviceTopology::DeviceTopology(AudioInput& input, UIState& state, std::vector<AudioInput::DeviceInfo>& inputs,
    std::vector<AudioOutput::DeviceInfo>& outputs) : impl_(std::make_unique<Impl>(input, state, inputs, outputs)) {}
DeviceTopology::~DeviceTopology() = default;
void DeviceTopology::update() { impl_->update(); }
}
