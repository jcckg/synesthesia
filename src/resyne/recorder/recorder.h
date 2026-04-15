#pragma once

#include <imgui.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/output/audio_output.h"
#include "colour/colour_core.h"
#include "resyne/encoding/formats/exporter.h"
#include "resyne/ui/timeline/timeline.h"
#include "resyne/ui/toolbar/tool_state.h"

class FFTProcessor;
class AudioInput;
class AudioProcessor;
namespace Renderer {
class PresentationResources;
}

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

struct DetachedVisualisationState {
    bool available = false;
    bool isOpen = false;
    bool openRequested = false;
};

struct RecorderState {
    ~RecorderState();
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
    ColourCore::ColourSpace importColourSpace = ColourCore::ColourSpace::Rec2020;
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
    std::mutex operationStatusMutex;
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
    std::vector<float> playbackAudio;
    bool isPlaybackInitialised = false;

    bool loopEnabled = true;
    bool showExportDialog = false;
    bool focusRequested = false;
    int outputDeviceIndex = -1;
    DetachedVisualisationState detachedVisualisation;

    int videoWidth = 1920;
    int videoHeight = 1080;
    int videoFrameRate = 60;
    ColourCore::ColourSpace videoColourSpace = ColourCore::ColourSpace::Rec2020;
    bool videoGamutMapping = true;
    float videoSmoothingAmount = 0.6f;
    bool exportGradient = false;

    bool presentationSmoothingEnabled = true;
    bool presentationManualSmoothing = false;
    float presentationSmoothingAmount = 0.6f;
    Renderer::PresentationResources* presentationResources = nullptr;

    std::vector<Timeline::TimelineSample> timelinePreviewCache;
    bool timelinePreviewCacheDirty = true;
    size_t timelinePreviewCacheMaxSamples = 0;
    size_t timelinePreviewCacheSourceCount = 0;
    bool timelinePreviewCacheUsesPreviewSamples = false;
    ColourCore::ColourSpace timelinePreviewCacheColourSpace = ColourCore::ColourSpace::Rec2020;
    bool timelinePreviewCacheGamutMapping = true;
    float timelinePreviewCacheLowGain = 1.0f;
    float timelinePreviewCacheMidGain = 1.0f;
    float timelinePreviewCacheHighGain = 1.0f;
    bool timelinePreviewCacheSmoothingEnabled = true;
    bool timelinePreviewCacheManualSmoothing = false;
    float timelinePreviewCacheSmoothingAmount = 0.6f;
};

class Recorder {
public:
    static void updateFromFFTProcessor(RecorderState& state,
                                       AudioProcessor& audioProcessor,
                                       float r,
                                       float g,
                                       float b);

    static void drawBottomPanel(RecorderState& state,
                                AudioProcessor& audioProcessor,
                                float panelX,
                                float panelY,
                                float panelWidth,
                                float panelHeight);

    static void drawFullWindow(RecorderState& state,
                               AudioInput& audioInput,
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
                               ColourCore::ColourSpace colourSpace,
                               bool applyGamutMapping);

    static bool isSupportedImportFile(const std::string& filepath);
    static bool hasLoadedAudio(RecorderState& state);
    static void clearLoadedAudio(RecorderState& state);

    static void startRecording(RecorderState& state,
                               AudioProcessor& audioProcessor,
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
    static bool refreshPlaybackOutput(RecorderState& state);
    static void importFromFileThreaded(RecorderState& state,
                                       std::string filepath,
                                       ColourCore::ColourSpace colourSpace,
                                       bool applyGamutMapping);

private:
    static void drawExportDialog(RecorderState& state);
    static void drawLoadingDialog(RecorderState& state);
    static void drawExportingDialog(RecorderState& state);
    static void handleKeyboardShortcuts(RecorderState& state,
                                        bool windowFocused,
                                        bool hasPlaybackData);
};

void setLoadingOperationStatus(RecorderState& state, std::string status);
std::string getLoadingOperationStatus(RecorderState& state);
void setExportOperationStatus(RecorderState& state, std::string status);
std::string getExportOperationStatus(RecorderState& state);

}
