#include "import_handler.h"
#include "imgui_internal.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace UIHandlers {

void ImportHandler::processFileImport(ReSyne::RecorderState& recorderState) {
	if (recorderState.importPhase == 1 && !recorderState.pendingImportPath.empty()) {
		std::filesystem::path fsPath(recorderState.pendingImportPath);
		std::string filename = fsPath.filename().string();
		if (filename.empty()) {
			filename = recorderState.pendingImportPath;
		}

		recorderState.showLoadingDialog = true;
		recorderState.loadingFilename = filename;
		recorderState.loadingProgress = 0.0f;
		ReSyne::setLoadingOperationStatus(recorderState, {});
		recorderState.importPhase = 2;
	} else if (recorderState.importPhase == 2 && !recorderState.pendingImportPath.empty()) {
		const std::string pathToImport = recorderState.pendingImportPath;

		if (recorderState.importThread.joinable()) {
			recorderState.importThread.join();
		}

		recorderState.importThread = std::thread(
			&ReSyne::Recorder::importFromFileThreaded,
			std::ref(recorderState),
			pathToImport,
			recorderState.importColourSpace,
			recorderState.importGamutMapping);

		recorderState.importPhase = 3;
	} else if (recorderState.importPhase == 3) {
		recorderState.loadingProgress = recorderState.loadingProgressAtomic.load(std::memory_order_acquire);

		if (recorderState.importComplete.load(std::memory_order_acquire)) {
			if (recorderState.importThread.joinable()) {
				recorderState.importThread.join();
			}

			bool success = false;
			bool hasReconstructedAudio = false;
			std::string errorMessage;
			std::string filename;

			{
				std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
				success = !recorderState.importedSamples.empty();
				errorMessage = recorderState.importErrorMessage;

				if (success) {
					recorderState.samples = std::move(recorderState.importedSamples);
					recorderState.metadata = std::move(recorderState.importedMetadata);
					recorderState.isRecording = false;
					recorderState.isPlaybackInitialised = false;
					recorderState.sampleColourCache.clear();
					recorderState.colourCacheDirty = true;
					recorderState.timelinePreviewCache.clear();
					recorderState.timelinePreviewCacheDirty = true;

					hasReconstructedAudio = !recorderState.playbackAudio.empty();

					if (recorderState.audioOutput) {
						recorderState.audioOutput->stop();
						recorderState.audioOutput->clearAudioData();
					}

					recorderState.fallbackSampleRate = recorderState.metadata.sampleRate;
					recorderState.fallbackFftSize = recorderState.metadata.fftSize;
					recorderState.fallbackHopSize = recorderState.metadata.hopSize;
					recorderState.dropFlashAlpha = 1.0f;
					recorderState.timeline.scrubberNormalisedPosition = 0.0f;
					recorderState.timeline.isScrubberDragging = false;
					recorderState.timeline.hoverOverlayAlpha = 0.0f;
					recorderState.timeline.gradientRegionValid = false;
					recorderState.timeline.zoomFactor = 1.0f;
					recorderState.timeline.viewCentreNormalised = 0.5f;
					recorderState.timeline.grabStartViewCentre = 0.5f;
					recorderState.timeline.trackScrubber = false;
					recorderState.timeline.isZoomGestureActive = false;
					recorderState.timeline.isGrabGestureActive = false;

					std::filesystem::path fsPath(recorderState.pendingImportPath);
					filename = fsPath.filename().string();
					if (filename.empty()) {
						filename = recorderState.pendingImportPath;
					}

					recorderState.previewSamples.clear();
					recorderState.previewReady.store(false, std::memory_order_release);
					ReSyne::setLoadingOperationStatus(recorderState, {});
				}
			}

			if (success) {
				if (hasReconstructedAudio) {
					ReSyne::Recorder::refreshPlaybackOutput(recorderState);
				}

				recorderState.statusMessage.clear();
				recorderState.statusMessageTimer = 0.0f;
			} else {
				recorderState.statusMessage = "Failed to load file";
				if (!errorMessage.empty()) {
					recorderState.statusMessage += " (" + errorMessage + ")";
				}
				recorderState.statusMessageTimer = 4.0f;
			}

			recorderState.showLoadingDialog = false;
			ReSyne::setLoadingOperationStatus(recorderState, {});
			recorderState.pendingImportPath.clear();
			recorderState.importPhase = 0;
			ImGui::ClearActiveID();
			recorderState.focusRequested = true;
		}
	}
}

}
