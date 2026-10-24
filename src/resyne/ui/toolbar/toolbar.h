#pragma once

#include <imgui.h>

#include "resyne/ui/toolbar/tool_state.h"

namespace ReSyne::UI::Utilities {

inline constexpr float kToolbarButtonSpacing = 6.0f;
inline constexpr float kToolbarSeparatorWidth = 1.0f;

struct ToolbarRenderContext {
    bool enabled = false;
    float buttonHeight = 26.0f;
    bool allowGrabTool = true;
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

void drawToolbarSeparator(float height);

ToolbarRenderResult renderToolbar(ToolState& state, const ToolbarRenderContext& context);

}
