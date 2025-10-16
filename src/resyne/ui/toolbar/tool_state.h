#pragma once

#include <imgui.h>

namespace ReSyne::UI::Utilities {

enum class ToolType {
    Cursor,
    Zoom,
    Grab
};

struct ToolState {
    ToolType activeTool = ToolType::Cursor;
};

const char* toolLabel(ToolType tool);
const char* toolTooltip(ToolType tool);

}
