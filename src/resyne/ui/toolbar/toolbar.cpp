#include "resyne/ui/toolbar/toolbar.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>

namespace ReSyne::UI::Utilities {

bool drawToolButton(const char* label,
                    const char* tooltip,
                    bool active,
                    bool enabled,
                    float height,
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

    ImVec2 buttonSize(height, height);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    char btnId[64];
    snprintf(btnId, sizeof(btnId), "##ToolBtn_%s", label);
    
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::Button(btnId, buttonSize);
    ImGui::EndDisabled();

    constexpr float iconScale = 0.8f;
    const float fontSize = ImGui::GetFontSize() * iconScale;
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, label, nullptr);
    ImFontBaked* font = ImGui::GetFont()->GetFontBaked(fontSize);
    const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));
    const float scale = fontSize / font->Size;
    const ImVec2 textPos(
        screenPos.x + (buttonSize.x - (glyph->X0 + glyph->X1) * scale) * 0.5f,
        screenPos.y + (buttonSize.y - (glyph->Y0 + glyph->Y1) * scale) * 0.5f
    );

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushDrawFlag(ImDrawFlags_TextNoPixelSnap, true);
    drawList->AddText(ImGui::GetFont(), fontSize, textPos, ImGui::ColorConvertFloat4ToU32(textColour), label);
    drawList->PopDrawFlag();

    if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }

    ImGui::PopStyleColor(4);

    return clicked && enabled;
}

void drawToolbarSeparator(float height) {
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float inset = height * 0.2f;
    ImGui::Dummy(ImVec2(kToolbarSeparatorWidth, height));
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(position.x, position.y + inset),
        ImVec2(position.x + kToolbarSeparatorWidth, position.y + height - inset),
        ImGui::GetColorU32(ImGuiCol_Separator, 0.65f));
    ImGui::SameLine(0.0f, kToolbarButtonSpacing);
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
            ImGui::SameLine(0.0f, kToolbarButtonSpacing);
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
