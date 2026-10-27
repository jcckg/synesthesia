#include "ui/menu/file_menu.h"

#include <array>

#include "imgui_internal.h"
#include "ui/ui.h"
#include "utilities/video/ffmpeg_locator.h"

namespace UI::Menu {

namespace {

constexpr std::array kExportItems{
    Item{"WAV…", Command::ExportWAV},
    Item{"RSYN…", Command::ExportRSYN},
    Item{"MP4…", Command::ExportMP4},
    Item{"MP4 + Gradient…", Command::ExportMP4WithGradient},
    Item{"TIFF…", Command::ExportTIFF}
};

constexpr std::array kFileItems{
    Item{"New", Command::NewTimeline, "n"},
    Item{"Load…", Command::Load, "o"},
    Item{},
    Item{.title = "Export…", .command = Command::Export, .shortcut = "e", .shiftShortcut = true},
    Item{"Export As", Command::None, "", kExportItems}
};

}

std::span<const Item> fileMenuItems() {
    return kFileItems;
}

bool canExecuteFileCommand(Command command, UIState& state) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetTopMostPopupModal() != nullptr) return false;

    auto& recorder = state.resyneState.recorderState;
    if (recorder.isRecording || recorder.recordingCountdownActive || recorder.importPhase != 0 ||
        recorder.importRunning.load(std::memory_order_acquire) ||
        recorder.exportRunning.load(std::memory_order_acquire) || recorder.showExportingDialog ||
        recorder.showExportDialog || recorder.showClearTimelineDialog ||
        recorder.shouldOpenLoadDialog || recorder.shouldOpenSaveDialog) {
        return false;
    }

    switch (command) {
        case Command::Load:
            return true;
        case Command::NewTimeline:
        case Command::Export:
        case Command::ExportWAV:
        case Command::ExportRSYN:
        case Command::ExportTIFF:
            return ReSyne::Recorder::hasLoadedAudio(recorder);
        case Command::ExportMP4:
        case Command::ExportMP4WithGradient:
            return Utilities::Video::FFmpegLocator::instance().isAvailable() &&
                   ReSyne::Recorder::hasLoadedAudio(recorder);
        default:
            return false;
    }
    return false;
}

bool executeFileCommand(Command command, UIState& state) {
    if (!canExecuteFileCommand(command, state)) return false;

    auto& recorder = state.resyneState.recorderState;
    switch (command) {
        case Command::NewTimeline:
            recorder.showClearTimelineDialog = true;
            break;
        case Command::Load:
            recorder.shouldOpenLoadDialog = true;
            break;
        case Command::Export:
            recorder.showExportDialog = true;
            break;
        case Command::ExportWAV:
            recorder.exportFormat = ReSyne::RecorderExportFormat::WAV;
            break;
        case Command::ExportRSYN:
            recorder.exportFormat = ReSyne::RecorderExportFormat::RSYN;
            break;
        case Command::ExportMP4:
        case Command::ExportMP4WithGradient:
            recorder.exportFormat = ReSyne::RecorderExportFormat::MP4;
            recorder.exportGradient = command == Command::ExportMP4WithGradient;
            break;
        case Command::ExportTIFF:
            recorder.exportFormat = ReSyne::RecorderExportFormat::TIFF;
            break;
        default:
            return false;
    }
    if (command != Command::NewTimeline && command != Command::Load && command != Command::Export) {
        recorder.shouldOpenSaveDialog = true;
    }

    state.visualSettings.activeView = UIState::View::ReSyne;
    state.visibility.showUI = true;
    recorder.focusRequested = true;
    return true;
}

}
