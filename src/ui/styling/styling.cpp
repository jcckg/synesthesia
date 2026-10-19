#include "styling.h"

namespace {

ImVec4 subduedColour(ImVec4 colour) {
    colour.x *= 0.75f;
    colour.y *= 0.75f;
    colour.z *= 0.75f;
    return colour;
}

void pushDropdownColours() {
    const bool dark = ImGui::GetStyleColorVec4(ImGuiCol_Text).x > 0.5f;
    const struct {
        ImGuiCol role;
        ImGuiCol darkSource;
    } roles[] = {
        {ImGuiCol_FrameBg, ImGuiCol_FrameBg},
        {ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgHovered},
        {ImGuiCol_Button, ImGuiCol_Header},
        {ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered}
    };
    for (const auto& entry : roles) {
        ImVec4 colour = ImGui::GetStyleColorVec4(dark ? entry.darkSource : entry.role);
        if (dark) {
            colour = subduedColour(colour);
        }
        ImGui::PushStyleColor(entry.role, colour);
    }
}

}

bool UIStyler::beginCombo(const char* label, const char* preview, ImGuiComboFlags flags) {
    pushDropdownColours();
    const bool open = ImGui::BeginCombo(label, preview, flags);
    ImGui::PopStyleColor(4);
    return open;
}

bool UIStyler::combo(const char* label, int* selected, const char* const items[], int count, int popupHeight) {
    pushDropdownColours();
    const bool changed = ImGui::Combo(label, selected, items, count, popupHeight);
    ImGui::PopStyleColor(4);
    return changed;
}

bool UIStyler::combo(const char* label, int* selected, const char* items, int popupHeight) {
    pushDropdownColours();
    const bool changed = ImGui::Combo(label, selected, items, popupHeight);
    ImGui::PopStyleColor(4);
    return changed;
}

void UIStyler::applyCustomStyle(ImGuiStyle& style, StyleState& state) {
    applyCustomStyleWithTheme(style, state, state.currentTheme);
}

void UIStyler::applyCustomStyleWithTheme(ImGuiStyle& style, StyleState& state, UITheme theme) {
    if (!state.styleApplied) {
        state.originalStyle = style;
        state.styleApplied = true;
    }

    state.currentTheme = theme;
    setDimensions(style);
    setColours(style, theme);
}

void UIStyler::restoreOriginalStyle(ImGuiStyle& style, StyleState& state) {
    if (state.styleApplied) {
        style = state.originalStyle;
        state.styleApplied = false;
    }
}

void UIStyler::setDimensions(ImGuiStyle& style) {
    style.WindowRounding = 0.0f;
    style.FrameRounding = 1.0f;
    style.ScrollbarRounding = 5.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabRounding = 1.0f;
    style.TabRounding = 1.0f;
    style.ChildRounding = 1.0f;
    style.PopupRounding = 1.0f;
    style.Alpha = 1.0f;
    style.ItemSpacing = ImVec2(10, 12);
    style.FramePadding = ImVec2(8, 6);
    style.WindowPadding = ImVec2(12, 12);
}

ImVec4 UIStyler::windowBackgroundColour(UITheme theme) {
    return theme == UITheme::Dark ? ImVec4(0.017f, 0.017f, 0.017f, 1.0f) :
        ImVec4(0.969f, 0.969f, 0.969f, 1.0f);
}

void UIStyler::setColours(ImGuiStyle& style, UITheme theme) {
    ImVec4* colours = style.Colors;
    const bool dark = theme == UITheme::Dark;
    const auto set = [&](ImGuiCol role, const ImVec4& darkColour, const ImVec4& lightColour) {
        colours[role] = dark ? darkColour : lightColour;
    };

    colours[ImGuiCol_WindowBg] = windowBackgroundColour(theme);
    set(ImGuiCol_Border, {0.260f, 0.260f, 0.260f, 0.60f}, {0.747f, 0.747f, 0.747f, 0.70f});
    colours[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    set(ImGuiCol_FrameBg, {0.145f, 0.145f, 0.145f, 1.00f}, {1.000f, 1.000f, 1.000f, 1.00f});
    set(ImGuiCol_FrameBgHovered, {0.185f, 0.185f, 0.185f, 1.00f}, {0.975f, 0.975f, 0.975f, 1.00f});
    set(ImGuiCol_FrameBgActive, {0.225f, 0.225f, 0.225f, 1.00f}, {0.939f, 0.939f, 0.939f, 1.00f});

    set(ImGuiCol_TitleBg, {0.025f, 0.025f, 0.025f, 1.00f}, {0.949f, 0.949f, 0.949f, 1.00f});
    set(ImGuiCol_TitleBgActive, {0.064f, 0.064f, 0.064f, 1.00f}, {0.918f, 0.918f, 0.918f, 1.00f});
    colours[ImGuiCol_TitleBgCollapsed] = colours[ImGuiCol_TitleBg];

    set(ImGuiCol_SliderGrab, {0.467f, 0.467f, 0.467f, 1.00f}, {0.545f, 0.545f, 0.545f, 1.00f});
    set(ImGuiCol_SliderGrabActive, {0.645f, 0.645f, 0.645f, 1.00f}, {0.404f, 0.404f, 0.404f, 1.00f});

    set(ImGuiCol_Button, subduedColour(colours[ImGuiCol_FrameBg]), {0.914f, 0.914f, 0.914f, 1.00f});
    set(ImGuiCol_ButtonHovered, subduedColour(colours[ImGuiCol_FrameBgHovered]), {0.859f, 0.859f, 0.859f, 1.00f});
    set(ImGuiCol_ButtonActive, subduedColour(colours[ImGuiCol_FrameBgActive]), {0.787f, 0.787f, 0.787f, 1.00f});
    colours[ImGuiCol_CheckboxSelectedBg] = colours[ImGuiCol_ButtonActive];

    set(ImGuiCol_Header, {0.090f, 0.090f, 0.090f, 1.00f}, {0.899f, 0.899f, 0.899f, 1.00f});
    set(ImGuiCol_HeaderHovered, {0.130f, 0.130f, 0.130f, 1.00f}, {0.859f, 0.859f, 0.859f, 1.00f});
    set(ImGuiCol_HeaderActive, {0.170f, 0.170f, 0.170f, 1.00f}, {0.833f, 0.833f, 0.833f, 1.00f});

    colours[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    set(ImGuiCol_ScrollbarGrab, {0.219f, 0.219f, 0.219f, 0.75f}, {0.706f, 0.706f, 0.706f, 0.75f});
    set(ImGuiCol_ScrollbarGrabHovered, {0.307f, 0.307f, 0.307f, 0.85f}, {0.595f, 0.595f, 0.595f, 0.85f});
    set(ImGuiCol_ScrollbarGrabActive, {0.417f, 0.417f, 0.417f, 1.00f}, {0.484f, 0.484f, 0.484f, 1.00f});

    set(ImGuiCol_CheckMark, {0.869f, 0.869f, 0.869f, 1.00f}, {0.245f, 0.245f, 0.245f, 1.00f});
    set(ImGuiCol_Text, {0.920f, 0.920f, 0.920f, 1.00f}, {0.139f, 0.139f, 0.139f, 1.00f});
    colours[ImGuiCol_InputTextCursor] = colours[ImGuiCol_Text];
    set(ImGuiCol_TextDisabled, {0.619f, 0.619f, 0.619f, 1.00f}, {0.387f, 0.387f, 0.387f, 1.00f});
    set(ImGuiCol_PopupBg, {0.080f, 0.080f, 0.080f, 1.00f}, {0.994f, 0.994f, 0.994f, 1.00f});
    set(ImGuiCol_ModalWindowDimBg, {0.000f, 0.000f, 0.000f, 0.40f}, {0.000f, 0.000f, 0.000f, 0.18f});
    set(ImGuiCol_TextSelectedBg, {0.350f, 0.350f, 0.350f, 0.60f}, {0.484f, 0.484f, 0.484f, 0.25f});
    set(ImGuiCol_NavCursor, {0.606f, 0.606f, 0.606f, 1.00f}, {0.343f, 0.343f, 0.343f, 1.00f});
    set(ImGuiCol_Separator, {0.180f, 0.180f, 0.180f, 1.00f}, {0.78f, 0.78f, 0.78f, 1.0f});
    colours[ImGuiCol_SeparatorHovered] = colours[ImGuiCol_ButtonHovered];
    colours[ImGuiCol_SeparatorActive] = colours[ImGuiCol_ButtonActive];
}
