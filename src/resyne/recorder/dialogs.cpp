#include "resyne/recorder/recorder.h"
#include "resyne/recorder/export_destination.h"
#include "utilities/cancellation.h"

#include <utility>
#include <chrono>
#include <algorithm>
#include <filesystem>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
#include <portable-file-dialogs.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "resyne/encoding/formats/format_mp4.h"
#include "utilities/video/ffmpeg_locator.h"

namespace ReSyne {

bool Recorder::exportRecording(RecorderState& state,
                               const std::string& filepath,
                               RecorderExportFormat format) {
    ensureRsynSamplesLoaded(state);

    std::lock_guard<std::mutex> lock(state.samplesMutex);

    switch (format) {
        case RecorderExportFormat::WAV:
            return SequenceExporter::exportToWAV(filepath, state.samples, state.metadata);
        case RecorderExportFormat::RSYN: {
            RSYNExportOptions options{};
            options.presentationSettings.colourSpace = state.importColourSpace;
            options.presentationSettings.applyGamutMapping = state.importGamutMapping;
            options.presentationSettings.lowGain = state.importLowGain;
            options.presentationSettings.midGain = state.importMidGain;
            options.presentationSettings.highGain = state.importHighGain;
            options.presentationSettings.smoothingEnabled = state.presentationSmoothingEnabled;
            options.presentationSettings.manualSmoothing = state.presentationManualSmoothing;
            options.presentationSettings.smoothingAmount = state.presentationSmoothingAmount;
            return SequenceExporter::exportToRsyn(filepath, state.samples, state.metadata, options);
        }
        case RecorderExportFormat::TIFF:
            return SequenceExporter::exportToTIFF(filepath, state.samples, state.metadata);
        case RecorderExportFormat::MP4: {
            auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
            if (!ffmpegLocator.isAvailable()) {
                return false;
            }
            ReSyne::Encoding::Video::ExportOptions options;
            options.ffmpegExecutable = ffmpegLocator.executablePath();
            options.colourSpace = state.videoColourSpace;
            options.applyGamutMapping = state.videoGamutMapping;
            options.smoothingAmount = state.presentationSmoothingAmount;
            options.smoothingEnabled = state.presentationSmoothingEnabled;
            options.manualSmoothing = state.presentationManualSmoothing;
            options.lowGain = state.importLowGain;
            options.midGain = state.importMidGain;
            options.highGain = state.importHighGain;
            options.width = state.videoWidth;
            options.height = state.videoHeight;
            options.frameRate = state.videoFrameRate;
            options.exportGradient = state.exportGradient;
            std::string errorMsg;
            return ReSyne::Encoding::Video::exportToMP4(
                filepath, state.samples, state.metadata, options,
                nullptr, errorMsg);
        }
        default:
            return false;
    }
}

void Recorder::exportRecordingThreaded(RecorderState& state,
                                       std::string filepath,
                                       RecorderExportFormat format) {
    if (state.exportRunning.load(std::memory_order_acquire) || state.isRecording ||
        state.recordingCountdownActive || state.importPhase != 0) {
        return;
    }

    if (state.exportThread.joinable()) {
        state.exportThread.join();
    }

    RSYNExportOptions rsynOptions{};
    rsynOptions.presentationSettings.colourSpace = state.importColourSpace;
    rsynOptions.presentationSettings.applyGamutMapping = state.importGamutMapping;
    rsynOptions.presentationSettings.lowGain = state.importLowGain;
    rsynOptions.presentationSettings.midGain = state.importMidGain;
    rsynOptions.presentationSettings.highGain = state.importHighGain;
    rsynOptions.presentationSettings.smoothingEnabled = state.presentationSmoothingEnabled;
    rsynOptions.presentationSettings.manualSmoothing = state.presentationManualSmoothing;
    rsynOptions.presentationSettings.smoothingAmount = state.presentationSmoothingAmount;

    ReSyne::Encoding::Video::ExportOptions videoOptions;
    if (format == RecorderExportFormat::MP4) {
        auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
        if (ffmpegLocator.isAvailable()) videoOptions.ffmpegExecutable = ffmpegLocator.executablePath();
    }
    videoOptions.colourSpace = state.videoColourSpace;
    videoOptions.applyGamutMapping = state.videoGamutMapping;
    videoOptions.smoothingAmount = state.presentationSmoothingAmount;
    videoOptions.smoothingEnabled = state.presentationSmoothingEnabled;
    videoOptions.manualSmoothing = state.presentationManualSmoothing;
    videoOptions.lowGain = state.importLowGain;
    videoOptions.midGain = state.importMidGain;
    videoOptions.highGain = state.importHighGain;
    videoOptions.width = state.videoWidth;
    videoOptions.height = state.videoHeight;
    videoOptions.frameRate = state.videoFrameRate;
    videoOptions.exportGradient = state.exportGradient;

    state.exportCancellation = std::stop_source{};
    state.exportRunning.store(true, std::memory_order_release);
    state.exportComplete.store(false, std::memory_order_release);
    state.exportProgressAtomic.store(0.0f, std::memory_order_release);
    state.exportThread = std::thread([&state, filepath = std::move(filepath), format,
        rsynOptions = std::move(rsynOptions), videoOptions = std::move(videoOptions)]() {
        const auto cancellation = state.exportCancellation.get_token();
        bool success = false;
        bool cancelled = false;
        std::string errorMessage;

        auto updateProgress = [&](float progress) {
            Utilities::checkCancellation(cancellation);
            const float current = state.exportProgressAtomic.load(std::memory_order_relaxed);
            state.exportProgressAtomic.store(std::max(current, std::clamp(progress, 0.0f, 1.0f)), std::memory_order_release);
        };

        auto updateStatus = [&](const std::string& status) {
            Utilities::checkCancellation(cancellation);
            setExportOperationStatus(state, status);
        };

		try {
            std::vector<AudioColourSample> samplesCopy;
            AudioMetadata metadataCopy;
            Utilities::checkCancellation(cancellation);
            ExportDestination destination(filepath, format == RecorderExportFormat::MP4 && videoOptions.exportGradient);
            ensureRsynSamplesLoaded(state, [&](float fraction) { updateProgress(0.05f * fraction); });
            {
                std::lock_guard<std::mutex> lock(state.samplesMutex);
                samplesCopy = state.samples;
                metadataCopy = state.metadata;
            }
            updateProgress(0.05f);
			switch (format) {
				case RecorderExportFormat::WAV:
					updateStatus("Reconstructing audio from spectral data...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToWAV(
						destination.path(),
						samplesCopy,
						metadataCopy,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
				case RecorderExportFormat::RSYN: {
					updateStatus("Encoding spectral data to RSYN format...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToRsyn(
						destination.path(),
						samplesCopy,
						metadataCopy,
                        rsynOptions,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
                }
				case RecorderExportFormat::TIFF:
					updateStatus("Encoding spectral data to TIFF format...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToTIFF(
						destination.path(),
						samplesCopy,
						metadataCopy,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
				case RecorderExportFormat::MP4: {
					if (videoOptions.ffmpegExecutable.empty()) {
						errorMessage = "FFmpeg not found";
						success = false;
						break;
					}
					updateStatus("Preparing video export...");
					updateProgress(0.02f);
					success = ReSyne::Encoding::Video::exportToMP4(
						destination.path(),
						samplesCopy,
						metadataCopy,
						videoOptions,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(clamped);
							if (clamped < 0.15f) {
								updateStatus("Reconstructing audio...");
							} else if (clamped < 0.5f) {
								updateStatus("Rendering video frames...");
							} else if (clamped < 0.9f && videoOptions.exportGradient) {
								updateStatus("Rendering gradient video...");
							} else {
								updateStatus("Finalising video...");
							}
						},
						errorMessage);
					break;
				}
                default:
                    errorMessage = "Unknown export format";
                    success = false;
                    break;
            }

            Utilities::checkCancellation(cancellation);
            if (success) {
                updateProgress(0.95f);
                updateStatus("Finalising export...");
                destination.commit();
            } else if (errorMessage.empty()) {
                errorMessage = "Export failed";
            }
        } catch (const Utilities::OperationCancelled&) {
            success = false;
            cancelled = true;
            errorMessage.clear();
        } catch (const std::exception& e) {
            success = false;
            errorMessage = std::string("Export error: ") + e.what();
        } catch (...) {
            success = false;
            errorMessage = "Unknown export error";
            std::cerr << "[ReSyne] Non-standard exception caught during export" << std::endl;
        }

        {
            std::lock_guard<std::mutex> lock(state.samplesMutex);
            if (success) {
                state.exportErrorMessage.clear();
            } else {
                state.exportErrorMessage = cancelled ? "" : (errorMessage.empty() ? "Export failed" : errorMessage);
            }
        }

        state.exportProgressAtomic.store(1.0f, std::memory_order_release);
        state.exportComplete.store(true, std::memory_order_release);
        state.exportRunning.store(false, std::memory_order_release);
    });
}

void Recorder::cancelExport(RecorderState& state) {
    if (state.exportRunning.load(std::memory_order_acquire)) state.exportCancellation.request_stop();
}

void Recorder::handleLoadDialog(RecorderState& state) {
    const auto loadSelection = [&](bool directoryOnly) {
        const auto path = ::UI::FileBrowser::chooseLoadPath(state.fileBrowser.directory,
            true, directoryOnly);
        if (path.empty()) {
            state.focusRequested = true;
            return;
        }
        std::error_code error;
        if (std::filesystem::is_directory(std::filesystem::path(std::u8string(path.begin(), path.end())), error)) {
            ::UI::FileBrowser::selectDirectory(state.fileBrowser, path);
        } else {
            requestImport(state, path);
        }
    };

#ifdef __APPLE__
    if (!state.shouldOpenLoadDialog) return;
    state.shouldOpenLoadDialog = false;
    if (state.exportRunning.load(std::memory_order_acquire)) return;
    loadSelection(false);
#else
    if (state.shouldOpenLoadDialog) {
        state.shouldOpenLoadDialog = false;
        if (!state.exportRunning.load(std::memory_order_acquire)) ImGui::OpenPopup("Load File or Directory");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Load File or Directory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Load a file, or browse a directory in the file sidebar.");
        bool chooseFile = ImGui::Button("File...", ImVec2(140.0f, 0.0f));
        ImGui::SameLine();
        bool chooseDirectory = ImGui::Button("Directory...", ImVec2(140.0f, 0.0f));
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            state.focusRequested = true;
        }
        if (chooseFile || chooseDirectory) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (chooseFile || chooseDirectory) loadSelection(chooseDirectory);
    }
#endif
}

void Recorder::handleFileDialog(RecorderState& state) {
    if (!state.shouldOpenSaveDialog) {
        return;
    }

    state.shouldOpenSaveDialog = false;
    if (state.exportRunning.load(std::memory_order_acquire) || state.isRecording ||
        state.recordingCountdownActive || state.importPhase != 0) return;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);

    std::string extension;
    std::string dialogTitle;
    std::string fileFilter;
    std::string fileTypeName;

    switch (state.exportFormat) {
        case RecorderExportFormat::WAV:
            extension = ".wav";
            dialogTitle = "Save Audio as WAV";
            fileFilter = "*.wav";
            fileTypeName = "WAV Files";
            break;
        case RecorderExportFormat::RSYN:
            extension = ".rsyn";
            dialogTitle = "Save as RSYN File";
            fileFilter = "*.rsyn";
            fileTypeName = "RSYN Files";
            break;
        case RecorderExportFormat::TIFF:
            extension = ".tiff";
            dialogTitle = "Save as TIFF (RGB Float32 Lossless)";
            fileFilter = "*.tiff";
            fileTypeName = "TIFF Files";
            break;
        case RecorderExportFormat::MP4:
            extension = ".mp4";
            dialogTitle = "Save Video as MP4";
            fileFilter = "*.mp4";
            fileTypeName = "MP4 Video Files";
            break;
        default:
            extension = ".wav";
            dialogTitle = "Save Recording";
            fileFilter = "*.*";
            fileTypeName = "All Files";
            break;
    }

    std::string defaultFilename = "recording_" + std::to_string(timestamp) + extension;

    auto result = pfd::save_file(
        dialogTitle,
        defaultFilename,
        {fileTypeName, fileFilter, "All Files", "*"}
    ).result();

    if (!result.empty()) {
        std::string filepath = result;
        if (filepath.size() < extension.size() ||
            filepath.compare(filepath.size() - extension.size(), extension.size(), extension) != 0) {
            filepath += extension;
        }

        state.pendingExportPath = filepath;
        state.pendingExportFormat = state.exportFormat;
        state.showExportingDialog = true;
        setExportOperationStatus(state, {});
        
        size_t lastSlash = filepath.find_last_of("/\\");
        state.exportingFilename = (lastSlash != std::string::npos)
            ? filepath.substr(lastSlash + 1)
            : filepath;

        exportRecordingThreaded(state, filepath, state.exportFormat);
    }
}

}
