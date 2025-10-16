#pragma once

#include <imgui.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/output/audio_output.h"
#include "colour/colour_mapper.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/ui/timeline/timeline.h"
#include "resyne/ui/toolbar/tool_state.h"

class FFTProcessor;

namespace ReSyne {

struct SampleColourEntry {
	ImVec4 rgb = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	float labL = 0.0f;
	float labA = 0.0f;
	float labB = 0.0f;
};

enum class RecorderExportFormat {
    WAV,
    RESYNE,
    TIFF,
    MP4
};

struct RecorderState {
    bool isRecording = false;
    bool windowOpen = false;
    std::vector<AudioColourSample> samples;
    uint64_t firstFrameCounter = 0;
    std::mutex samplesMutex;
    bool shouldOpenSaveDialog = false;
    bool shouldOpenLoadDialog = false;
    RecorderExportFormat exportFormat = RecorderExportFormat::WAV;
    AudioMetadata metadata;
    static constexpr size_t MAX_SAMPLES = 100000;
    float fallbackSampleRate = 0.0f;
    int fallbackFftSize = 0;
    int fallbackHopSize = 0;
    float importGamma = 0.8f;
    ColourMapper::ColourSpace importColourSpace = ColourMapper::ColourSpace::Rec2020;
    bool importGamutMapping = true;
    float importLowGain = 1.0f;
    float importMidGain = 1.0f;
    float importHighGain = 1.0f;
    Timeline::TimelineState timeline;
    UI::Utilities::ToolState toolState;
    Timeline::TrackpadGestureInput trackpadInput;
    float dropFlashAlpha = 0.0f;
    std::string statusMessage;
    float statusMessageTimer = 0.0f;

    bool showLoadingDialog = false;
    std::string loadingFilename;
    float loadingProgress = 0.0f;
    std::string pendingImportPath;
    int importPhase = 0;  // 0=none, 1=show dialog, 2=start thread, 3+=poll

    std::thread importThread;
    std::atomic<bool> importRunning{false};
    std::atomic<bool> importComplete{false};
    std::atomic<float> loadingProgressAtomic{0.0f};
    std::string importErrorMessage;  // Protected by samplesMutex
    std::vector<AudioColourSample> importedSamples;  // Protected by samplesMutex
    AudioMetadata importedMetadata;  // Protected by samplesMutex

    std::vector<AudioColourSample> previewSamples;  // Protected by samplesMutex
    std::atomic<bool> previewReady{false};
    std::string loadingOperationStatus;

    std::thread exportThread;
    std::atomic<bool> exportRunning{false};
    std::atomic<bool> exportComplete{false};
    std::atomic<float> exportProgressAtomic{0.0f};
    std::string exportErrorMessage;  // Protected by samplesMutex
    bool showExportingDialog = false;
    std::string exportingFilename;
    std::string exportOperationStatus;
    RecorderExportFormat pendingExportFormat = RecorderExportFormat::WAV;
    std::string pendingExportPath;

    std::unique_ptr<AudioOutput> audioOutput;
    std::vector<float> reconstructedAudio;
    bool isPlaybackInitialised = false;

    bool loopEnabled = true;
    bool showExportDialog = false;
    int outputDeviceIndex = -1;

    int videoWidth = 1920;
    int videoHeight = 1080;
    int videoFrameRate = 60;
    float videoGamma = 0.8f;
    ColourMapper::ColourSpace videoColourSpace = ColourMapper::ColourSpace::Rec2020;
    bool videoGamutMapping = true;
    float videoSmoothingAmount = 0.6f;
    bool exportGradient = false;

    std::vector<SampleColourEntry> sampleColourCache;
    bool colourCacheDirty = true;
    float colourCacheGamma = 0.8f;
    ColourMapper::ColourSpace colourCacheColourSpace = ColourMapper::ColourSpace::Rec2020;
    bool colourCacheGamutMapping = true;
};

class Recorder {
public:
    static void updateFromFFTProcessor(RecorderState& state,
                                       FFTProcessor& fftProcessor,
                                       float r,
                                       float g,
                                       float b);

    static void drawBottomPanel(RecorderState& state,
                                FFTProcessor& fftProcessor,
                                float panelX,
                                float panelY,
                                float panelWidth,
                                float panelHeight);

    static void drawFullWindow(RecorderState& state,
                               FFTProcessor& fftProcessor,
                               float windowX,
                               float windowY,
                               float windowWidth,
                               float windowHeight,
                               float currentR,
                               float currentG,
                               float currentB);

    static void handleFileDialog(RecorderState& state);
    static void handleLoadDialog(RecorderState& state);

    static bool importFromFile(RecorderState& state,
                               const std::string& filepath,
                               float gamma,
                               ColourMapper::ColourSpace colourSpace,
                               bool applyGamutMapping);

    static bool isSupportedImportFile(const std::string& filepath);

    static void startRecording(RecorderState& state,
                               FFTProcessor& fftProcessor,
                               int fftSize,
                               int hopSize);

    static void stopRecording(RecorderState& state);

    static bool exportRecording(RecorderState& state,
                                const std::string& filepath,
                                RecorderExportFormat format);

    static void exportRecordingThreaded(RecorderState& state,
                                        std::string filepath,
                                        RecorderExportFormat format);

    static void startPlayback(RecorderState& state);
    static void pausePlayback(RecorderState& state);
    static void stopPlayback(RecorderState& state);
    static void seekPlayback(RecorderState& state, float normalisedPosition);
    static void reconstructAudio(RecorderState& state);
    static void importFromFileThreaded(RecorderState& state,
                                       std::string filepath,
                                       float gamma,
                                       ColourMapper::ColourSpace colourSpace,
                                       bool applyGamutMapping);

private:
    static void drawExportDialog(RecorderState& state);
    static void drawLoadingDialog(RecorderState& state);
    static void drawExportingDialog(RecorderState& state);
    static void handleKeyboardShortcuts(RecorderState& state,
                                        bool windowFocused,
                                        bool hasPlaybackData);
};

}
