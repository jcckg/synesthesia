#pragma once

#include <imgui.h>

#include "resyne/ui/toolbar/tool_state.h"
#include "resyne/ui/recorder/recorder_constants.h"

namespace ReSyne {
struct RecorderState;
}

namespace ReSyne::UI::Utilities {

inline constexpr float kToolbarButtonSpacing = 2.0f;
inline constexpr float kToolbarSeparatorWidth = 1.0f;
inline constexpr float kToolbarGroupSpacing = 8.0f;
inline constexpr float kToolbarIconGroupSpacing = 4.0f;

struct ToolbarRenderContext {
    bool enabled = false;
    float buttonHeight = 0.0f;
    bool allowGrabTool = true;
};

struct RecorderToolbarContext {
    bool enabled = false;
    bool canStartRecording = false;
    bool hasData = false;
    bool timelineHasBorder = false;
    float width = 0.0f;
    bool outerLeftCorner = false;
    bool outerRightCorner = false;
};

struct ToolbarRenderResult {
    ImVec2 size = ImVec2(0.0f, 0.0f);
};

bool drawToolButton(const char* label,
                    const char* tooltip,
                    bool active,
                    bool enabled,
                    float height,
                    bool destructive = false);

void drawToolbarSeparator(float height, float spacing = kToolbarGroupSpacing);

ToolbarRenderResult renderToolbar(ToolState& state, const ToolbarRenderContext& context);
float recorderToolbarHeight(const RecorderState& state, float width);
void renderRecorderToolbar(RecorderState& state, const RecorderToolbarContext& context);

}
