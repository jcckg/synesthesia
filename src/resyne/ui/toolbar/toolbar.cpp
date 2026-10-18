#include "resyne/ui/toolbar/toolbar.h"

#include "imgui.h"
#include <cstdio>

namespace ReSyne::UI::Utilities {

bool drawToolButton(const char* label,
                    const char* tooltip,
                    bool active,
                    bool enabled,
                    float height,
                    float opticalOffsetX,
                    bool destructive) {
    const ImVec4 baseColour = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 hoveredColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 activeColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ImVec4 textColour = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImVec4 buttonColour = active ? activeColour : baseColour;
    ImVec4 buttonHovered = active ? activeColour : hoveredColour;
    if (destructive) buttonHovered = ImVec4(1.0f, 56.0f / 255.0f, 60.0f / 255.0f, 1.0f);

    if (!enabled) {
        buttonColour.w *= 0.45f;
        buttonHovered.w *= 0.45f;
        textColour.w *= 0.45f;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, buttonColour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColour);
    ImGui::PushStyleColor(ImGuiCol_Text, textColour);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    ImVec2 buttonSize(height, height);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    char btnId[64];
    snprintf(btnId, sizeof(btnId), "##ToolBtn_%s", label);
    
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::Button(btnId, buttonSize);
    ImGui::EndDisabled();

    constexpr float iconScale = 0.8f;
    const float fontSize = ImGui::GetFontSize() * iconScale;
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    const ImVec2 textPos(
        screenPos.x + (buttonSize.x - textSize.x) * 0.5f + opticalOffsetX,
        screenPos.y + (buttonSize.y - textSize.y) * 0.5f
    );

    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::ColorConvertFloat4ToU32(textColour), label);

    if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);

    return clicked && enabled;
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
        const float offset = (tool == ToolType::Cursor) ? 1.0f : 0.0f;
        if (drawToolButton(label, tooltip, active, toolEnabled, buttonHeight, offset)) {
            state.activeTool = tool;
        }
    }

    ImGui::EndGroup();

    result.size = ImGui::GetItemRectSize();
    return result;
}

}
