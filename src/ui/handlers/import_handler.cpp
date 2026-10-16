#include "import_handler.h"
#include "ui.h"
#include "imgui_internal.h"
#include "resyne/recorder/presentation_worker.h"

#include <filesystem>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace UIHandlers {

void ImportHandler::processFileImport(UIState& state) {
    auto& recorder = state.resyneState.recorderState;
    if (recorder.importPhase == 1 && !recorder.pendingImportPath.empty()) {
        const std::filesystem::path path(recorder.pendingImportPath);
        recorder.loadingFilename = path.filename().string();
        if (recorder.loadingFilename.empty()) recorder.loadingFilename = recorder.pendingImportPath;
        recorder.showLoadingDialog = true;
        recorder.loadingProgress = 0.0f;
        recorder.recordingCountdownActive = false;
        ReSyne::Recorder::pausePlayback(recorder);
        ReSyne::setLoadingOperationStatus(recorder, "Decoding audio...");
        recorder.importPhase = 2;
    } else if (recorder.importPhase == 2 && !recorder.pendingImportPath.empty()) {
        if (recorder.importThread.joinable()) recorder.importThread.join();
        {
            std::lock_guard lock(recorder.samplesMutex);
            ++recorder.presentationPreviewGeneration;
            recorder.presentationPreviewRevision = 0;
            recorder.previewSamples.clear();
            recorder.previewMetadata = {};
            recorder.importedSamples.clear();
            recorder.importedMetadata = {};
            recorder.importedPlaybackAudio.clear();
            recorder.importErrorMessage.clear();
            recorder.previewReady.store(false, std::memory_order_release);
        }
        recorder.importComplete.store(false, std::memory_order_release);
        recorder.importRunning.store(true, std::memory_order_release);
        recorder.loadingProgressAtomic.store(0.0f, std::memory_order_release);
        recorder.timeline.scrubberNormalisedPosition = 0.0f;
        recorder.timeline.isScrubberDragging = false;
        recorder.timeline.zoomFactor = 1.0f;
        recorder.timeline.viewCentreNormalised = 0.5f;
        recorder.timeline.trackScrubber = false;
        recorder.timeline.isZoomGestureActive = false;
        recorder.timeline.isGrabGestureActive = false;
        recorder.importThread = std::thread([&recorder, path = recorder.pendingImportPath,
            colourSpace = recorder.importColourSpace, gamutMapping = recorder.importGamutMapping] {
            try {
                ReSyne::Recorder::importFromFileThreaded(recorder, path, colourSpace, gamutMapping);
            } catch (const std::exception& error) {
                {
                    std::lock_guard lock(recorder.samplesMutex);
                    recorder.importedSamples.clear();
                    recorder.importedMetadata = {};
                    recorder.importedPlaybackAudio.clear();
                    recorder.importErrorMessage = error.what();
                }
                recorder.importComplete.store(true, std::memory_order_release);
                recorder.importRunning.store(false, std::memory_order_release);
            }
        });
        recorder.importPhase = 3;
    } else if (recorder.importPhase == 3) {
        recorder.loadingProgress = recorder.loadingProgressAtomic.load(std::memory_order_acquire);
        if (!recorder.importComplete.load(std::memory_order_acquire)) {
            ReSyne::recorderPresentation(recorder);
            return;
        }
        const auto presentation = ReSyne::recorderPresentation(recorder);

        bool success = false;
        std::size_t frameCount = 0;
        std::string errorMessage;
        {
            std::lock_guard lock(recorder.samplesMutex);
            success = !recorder.importedSamples.empty() || recorder.importedMetadata.presentationData != nullptr;
            frameCount = recorder.importedSamples.size();
            if (frameCount == 0 && recorder.importedMetadata.presentationData)
                frameCount = recorder.importedMetadata.presentationData->frames.size();
            errorMessage = recorder.importErrorMessage;
        }
        if (success && (!presentation || presentation->sourceRevision != recorder.presentationPreviewRevision ||
            presentation->processedFrames < frameCount) &&
            (!presentation || presentation->message.empty())) {
            const float fraction = presentation && frameCount > 0
                ? static_cast<float>(presentation->processedFrames) / static_cast<float>(frameCount) : 0.0f;
            recorder.loadingProgress = 0.95f + fraction * 0.04f;
            ReSyne::setLoadingOperationStatus(recorder, "Preparing colours...");
            return;
        }
        if (recorder.importThread.joinable()) recorder.importThread.join();

        if (success) {
            if (recorder.audioOutput) {
                recorder.audioOutput->stop();
                recorder.audioOutput->clearAudioData();
            }
            {
                std::lock_guard lock(recorder.samplesMutex);
                ++recorder.presentationSourceGeneration;
                recorder.presentationSourceRevision = recorder.presentationPreviewRevision;
                if (recorder.presentationWorker)
                    recorder.presentationWorker->retainImportPreview(recorder.presentationPreviewGeneration,
                        recorder.presentationSourceGeneration);
                recorder.samples = std::move(recorder.importedSamples);
                recorder.metadata = std::move(recorder.importedMetadata);
                recorder.playbackAudio = std::move(recorder.importedPlaybackAudio);
                recorder.previewSamples.clear();
                recorder.previewMetadata = {};
                recorder.previewReady.store(false, std::memory_order_release);
            }
            recorder.isRecording = false;
            recorder.isPlaybackInitialised = false;
            recorder.playbackOutputRefreshPending = false;
            recorder.playbackStartPending = false;
            recorder.playbackRouteReadyTime = 0.0;
            recorder.fallbackSampleRate = recorder.metadata.sampleRate;
            recorder.fallbackFftSize = recorder.metadata.fftSize;
            recorder.fallbackHopSize = recorder.metadata.hopSize;
            recorder.dropFlashAlpha = 0.0f;
            if (recorder.metadata.presentationData) {
                const auto& stored = recorder.metadata.presentationData->settings;
                recorder.importColourSpace = stored.colourSpace;
                recorder.importGamutMapping = stored.applyGamutMapping;
                recorder.presentationSmoothingEnabled = stored.smoothingEnabled;
                recorder.presentationManualSmoothing = stored.manualSmoothing;
                recorder.presentationSmoothingAmount = stored.smoothingAmount;
                state.visualSettings.colourSpace = stored.colourSpace;
                state.visualSettings.gamutMappingEnabled = stored.applyGamutMapping;
                state.visualSettings.smoothingEnabled = stored.smoothingEnabled;
                state.visualSettings.manualSmoothing = stored.manualSmoothing;
                state.visualSettings.colourSmoothingSpeed = stored.smoothingAmount;
            }
            ReSyne::setLoadingOperationStatus(recorder, "Preparing playback...");
            const bool playbackReady = ReSyne::Recorder::refreshPlaybackOutput(recorder);
            recorder.statusMessage = playbackReady ? "" : "Track loaded; select an available output device to play";
            if (presentation && !presentation->message.empty()) recorder.statusMessage = presentation->message;
            recorder.statusMessageTimer = recorder.statusMessage.empty() ? 0.0f : 4.0f;
            recorder.loadingProgress = 1.0f;
        } else {
            {
                std::lock_guard lock(recorder.samplesMutex);
                ++recorder.presentationPreviewGeneration;
                recorder.previewSamples.clear();
                recorder.previewMetadata = {};
                recorder.previewReady.store(false, std::memory_order_release);
            }
            recorder.statusMessage = "Failed to load file";
            if (!errorMessage.empty()) recorder.statusMessage += " (" + errorMessage + ")";
            recorder.statusMessageTimer = 4.0f;
        }
        recorder.showLoadingDialog = false;
        ReSyne::setLoadingOperationStatus(recorder, {});
        recorder.pendingImportPath.clear();
        recorder.importPhase = 0;
        ImGui::ClearActiveID();
        recorder.focusRequested = true;
    }
}

}
