#include "resyne/ui/toolbar/tool_state.h"

#include "ui/icons.h"

namespace ReSyne::UI::Utilities {

const char* toolLabel(ToolType tool) {
    switch (tool) {
        case ToolType::Cursor:
            return ICON_FA_ARROW_POINTER;
        case ToolType::Zoom:
            return ICON_FA_MAGNIFYING_GLASS_PLUS;
        case ToolType::Grab:
            return ICON_FA_HAND_POINTER;
        default:
            return "Tool";
    }
}

const char* toolTooltip(ToolType tool) {
    switch (tool) {
        case ToolType::Cursor:
            return "Default cursor";
        case ToolType::Zoom:
            return "Zoom in/out by dragging vertically";
        case ToolType::Grab:
            return "Drag timeline horizontally";
        default:
            return "";
    }
}

}
