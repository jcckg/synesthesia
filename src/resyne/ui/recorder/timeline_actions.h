#pragma once

namespace ReSyne {
struct RecorderState;
}

namespace ReSyne::RecorderUI {

void requestTimelineClear(RecorderState& state);
void drawClearTimelineDialog(RecorderState& state);

void beginRecordingCountdown(RecorderState& state);
void cancelRecordingCountdown(RecorderState& state);
bool updateRecordingCountdown(RecorderState& state, float deltaSeconds);
const char* recordingStartButtonLabel(const RecorderState& state);

}
