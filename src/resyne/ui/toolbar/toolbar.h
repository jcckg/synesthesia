#pragma once

#include <imgui.h>

#include "resyne/ui/toolbar/tool_state.h"

namespace ReSyne::UI::Utilities {

struct ToolbarRenderContext {
    bool enabled = false;
    float buttonHeight = 26.0f;
    bool allowGrabTool = true;
};

struct ToolbarRenderResult {
    ImVec2 size = ImVec2(0.0f, 0.0f);
};

ToolbarRenderResult renderToolbar(ToolState& state, const ToolbarRenderContext& context);

}
