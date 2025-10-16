#include "resyne/ui/toolbar/toolbar.h"

#include "imgui.h"

namespace ReSyne::UI::Utilities {

namespace {

bool drawToolButton(const char* label,
                    const char* tooltip,
                    bool active,
                    bool enabled,
                    float height) {
    const ImVec4 baseColour = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 hoveredColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 activeColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

    ImVec4 buttonColour = active ? activeColour : baseColour;
    ImVec4 buttonHovered = active ? activeColour : hoveredColour;

    if (!enabled) {
        buttonColour.w *= 0.45f;
        buttonHovered.w *= 0.45f;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, buttonColour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColour);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float verticalPadding = (height - textSize.y) * 0.5f;
    const float horizontalPadding = (height - textSize.x) * 0.5f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(horizontalPadding, verticalPadding));

    ImVec2 buttonSize(height, height);
    bool clicked = ImGui::Button(label, buttonSize);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    return clicked && enabled;
}

}

ToolbarRenderResult renderToolbar(ToolState& state, const ToolbarRenderContext& context) {
    ToolbarRenderResult result;

    ImGui::BeginGroup();

    const bool enabled = context.enabled;
    const float buttonHeight = context.buttonHeight;

    if (!context.allowGrabTool && state.activeTool == ToolType::Grab) {
        state.activeTool = ToolType::Cursor;
    }

    struct ToolEntry {
        ToolType type;
        bool allowed;
    };

    const ToolEntry tools[] = {
        {ToolType::Cursor, true},
        {ToolType::Zoom, true},
        {ToolType::Grab, context.allowGrabTool}
    };
    bool first = true;
    for (const ToolEntry& toolEntry : tools) {
        const ToolType tool = toolEntry.type;
        if (!first) {
            ImGui::SameLine(0.0f, 4.0f);
        }
        first = false;

        const char* label = toolLabel(tool);
        const char* tooltip = toolTooltip(tool);

        const bool active = state.activeTool == tool;
        const bool toolEnabled = enabled && toolEntry.allowed;
        if (drawToolButton(label, tooltip, active, toolEnabled, buttonHeight)) {
            state.activeTool = tool;
        }
    }

    ImGui::EndGroup();

    result.size = ImGui::GetItemRectSize();
    return result;
}

}
