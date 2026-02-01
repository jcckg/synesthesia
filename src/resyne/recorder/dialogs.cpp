#include "resyne/recorder/recorder.h"

#include <utility>
#include <chrono>
#include <algorithm>

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
    std::lock_guard<std::mutex> lock(state.samplesMutex);

    switch (format) {
        case RecorderExportFormat::WAV:
            return SequenceExporter::exportToWAV(filepath, state.samples, state.metadata);
        case RecorderExportFormat::RESYNE:
            return SequenceExporter::exportToResyne(filepath, state.samples, state.metadata);
        case RecorderExportFormat::TIFF:
            return SequenceExporter::exportToTIFF(filepath, state.samples, state.metadata);
        case RecorderExportFormat::MP4: {
            auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
            if (!ffmpegLocator.isAvailable()) {
                return false;
            }
            ReSyne::Encoding::Video::ExportOptions options;
            options.ffmpegExecutable = ffmpegLocator.executablePath();
            options.gamma = state.videoGamma;
            options.colourSpace = state.videoColourSpace;
            options.applyGamutMapping = state.videoGamutMapping;
            options.smoothingAmount = state.videoSmoothingAmount;
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
    if (state.exportRunning.load(std::memory_order_acquire)) {
        return;
    }

    if (state.exportThread.joinable()) {
        state.exportThread.join();
    }

    state.exportRunning.store(true, std::memory_order_release);
    state.exportComplete.store(false, std::memory_order_release);
    state.exportProgressAtomic.store(0.0f, std::memory_order_release);
    state.exportThread = std::thread([&state, filepath = std::move(filepath), format]() {
        bool success = false;
        std::string errorMessage;

        auto updateProgress = [&](float progress) {
            state.exportProgressAtomic.store(progress, std::memory_order_release);
        };

        auto updateStatus = [&](const std::string& status) {
            state.exportOperationStatus = status;
        };

        std::vector<AudioColourSample> samplesCopy;
        AudioMetadata metadataCopy;
        {
            std::lock_guard<std::mutex> lock(state.samplesMutex);
            samplesCopy = state.samples;
            metadataCopy = state.metadata;
        }

        updateProgress(0.05f);

		try {
			switch (format) {
				case RecorderExportFormat::WAV:
					updateStatus("Reconstructing audio from spectral data...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToWAV(
						filepath,
						samplesCopy,
						metadataCopy,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
				case RecorderExportFormat::RESYNE:
					updateStatus("Encoding spectral data to ReSyne format...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToResyne(
						filepath,
						samplesCopy,
						metadataCopy,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
				case RecorderExportFormat::TIFF:
					updateStatus("Encoding spectral data to TIFF format...");
					updateProgress(0.1f);
					success = SequenceExporter::exportToTIFF(
						filepath,
						samplesCopy,
						metadataCopy,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(0.1f + clamped * 0.8f);
						});
					break;
				case RecorderExportFormat::MP4: {
					auto& ffmpegLocator = Utilities::Video::FFmpegLocator::instance();
					if (!ffmpegLocator.isAvailable()) {
						errorMessage = "FFmpeg not found";
						success = false;
						break;
					}
					updateStatus("Preparing video export...");
					updateProgress(0.02f);
					ReSyne::Encoding::Video::ExportOptions options;
					options.ffmpegExecutable = ffmpegLocator.executablePath();
					options.gamma = state.videoGamma;
					options.colourSpace = state.videoColourSpace;
					options.applyGamutMapping = state.videoGamutMapping;
					options.smoothingAmount = state.videoSmoothingAmount;
					options.width = state.videoWidth;
					options.height = state.videoHeight;
					options.frameRate = state.videoFrameRate;
					options.exportGradient = state.exportGradient;
					success = ReSyne::Encoding::Video::exportToMP4(
						filepath,
						samplesCopy,
						metadataCopy,
						options,
						[&](float fraction) {
							const float clamped = std::clamp(fraction, 0.0f, 1.0f);
							updateProgress(clamped);
							if (clamped < 0.15f) {
								updateStatus("Reconstructing audio...");
							} else if (clamped < 0.5f) {
								updateStatus("Rendering video frames...");
							} else if (clamped < 0.9f && options.exportGradient) {
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

            if (success) {
                updateProgress(0.95f);
                updateStatus("Finalising export...");
            } else if (errorMessage.empty()) {
                errorMessage = "Export failed";
            }
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
                state.exportErrorMessage = errorMessage.empty() ? "Export failed" : errorMessage;
            }
        }

        updateProgress(1.0f);
        state.exportComplete.store(true, std::memory_order_release);
        state.exportRunning.store(false, std::memory_order_release);
    });
}

void Recorder::handleLoadDialog(RecorderState& state) {
    if (!state.shouldOpenLoadDialog) {
        return;
    }

    state.shouldOpenLoadDialog = false;

    auto result = pfd::open_file(
        "Load Audio/Colour File",
        "",
        {"ReSyne Files", "*.resyne *.synesthesia *.tiff *.tif",
         "TIFF Files", "*.tiff *.tif",
         "ReSyne Files", "*.resyne *.synesthesia",
         "All Files", "*"}
    ).result();

    if (!result.empty() && !result[0].empty()) {
        state.pendingImportPath = result[0];
        state.importPhase = 1;
    } else {
        state.focusRequested = true;
    }
}

void Recorder::handleFileDialog(RecorderState& state) {
    if (!state.shouldOpenSaveDialog) {
        return;
    }

    state.shouldOpenSaveDialog = false;

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
        case RecorderExportFormat::RESYNE:
            extension = ".resyne";
            dialogTitle = "Save as ReSyne File";
            fileFilter = "*.resyne";
            fileTypeName = "ReSyne Files";
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
        
        size_t lastSlash = filepath.find_last_of("/\\");
        state.exportingFilename = (lastSlash != std::string::npos)
            ? filepath.substr(lastSlash + 1)
            : filepath;

        exportRecordingThreaded(state, filepath, state.exportFormat);
    }
}

}
