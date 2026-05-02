#include "device_selector.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui_internal.h>
#include <string>

namespace DeviceSelector {
namespace {

constexpr float kMeterWidth = 13.0f;
constexpr float kMeterHeight = 18.0f;
constexpr float kIndicatorSpacing = 8.0f;
constexpr float kMinimumComboWidth = 150.0f;
constexpr int kMeterSegments = 18;

float selectorTextWidth(std::span<const Item> items) {
    float width = ImGui::CalcTextSize("Select an audio device").x;
    for (const Item& item : items) {
        width = std::max(width, ImGui::CalcTextSize(item.label).x);
    }
    return width;
}

float selectorWidth(std::span<const Item> items) {
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (availableWidth <= 0.0f) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float arrowWidth = ImGui::GetFrameHeight();
        const float desiredWidth = selectorTextWidth(items) +
                                   kMeterWidth +
                                   kIndicatorSpacing +
                                   style.FramePadding.x * 2.0f +
                                   arrowWidth;
        return std::max(kMinimumComboWidth, desiredWidth);
    }
    return std::max(kMinimumComboWidth, availableWidth);
}

float popupWidth(std::span<const Item> items, const float comboWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float desiredWidth = selectorTextWidth(items) +
                               kMeterWidth +
                               kIndicatorSpacing +
                               style.FramePadding.x * 2.0f;
    return std::max(comboWidth, desiredWidth);
}

int validSelectionIndex(const int selectedIndex, std::span<const Item> items) {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size())) {
        return -1;
    }
    return selectedIndex;
}

ImVec4 levelColour(const float normalisedPosition) {
    const float t = std::clamp(normalisedPosition, 0.0f, 1.0f);
    const ImVec4 textColour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool lightMode = textColour.x + textColour.y + textColour.z < 1.5f;
    if (t < 0.5f) {
        const float mix = t * 2.0f;
        if (lightMode) {
            return ImVec4(0.92f, 0.34f + mix * 0.34f, 0.16f, 1.0f);
        }
        return ImVec4(1.0f, 0.20f + mix * 0.70f, 0.12f, 1.0f);
    }

    const float mix = (t - 0.5f) * 2.0f;
    if (lightMode) {
        return ImVec4(0.92f - mix * 0.62f, 0.68f - mix * 0.10f, 0.16f + mix * 0.12f, 1.0f);
    }
    return ImVec4(1.0f - mix * 0.72f, 0.90f - mix * 0.08f, 0.12f + mix * 0.16f, 1.0f);
}

void drawMeterBar(ImDrawList* drawList, const ImVec2 min, const ImVec2 max, const float level) {
    const ImVec4 textColour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool lightMode = textColour.x + textColour.y + textColour.z < 1.5f;
    const ImU32 backgroundColour = lightMode
        ? ImGui::GetColorU32(ImVec4(0.96f, 0.965f, 0.975f, 1.0f))
        : ImGui::GetColorU32(ImVec4(0.16f, 0.16f, 0.17f, 0.75f));
    const ImU32 borderColour = lightMode
        ? ImGui::GetColorU32(ImVec4(0.76f, 0.78f, 0.82f, 0.55f))
        : ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.07f, 0.95f));
    drawList->AddRectFilled(min, max, backgroundColour, 0.0f);

    const float clampedLevel = std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, 1.0f);
    const float segmentHeight = (max.y - min.y) / static_cast<float>(kMeterSegments);

    for (int segment = 0; segment < kMeterSegments; ++segment) {
        const float segmentThreshold = static_cast<float>(segment + 1) / static_cast<float>(kMeterSegments);
        if (clampedLevel < segmentThreshold) {
            continue;
        }

        const float y2 = max.y - static_cast<float>(segment) * segmentHeight;
        const float y1 = y2 - std::max(1.0f, segmentHeight - 1.0f);
        drawList->AddRectFilled(
            ImVec2(min.x + 1.0f, y1),
            ImVec2(max.x - 1.0f, y2),
            ImGui::GetColorU32(levelColour(static_cast<float>(segment) / static_cast<float>(kMeterSegments - 1))));
    }

    drawList->AddRect(min, max, borderColour, 0.0f, 0, lightMode ? 0.55f : 1.0f);
}

void drawStereoMeter(ImDrawList* drawList, const Item& item, const ImVec2 min, const ImVec2 max) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = max.y - min.y;
    const ImVec2 meterPos(
        min.x + style.FramePadding.x,
        min.y + (height - kMeterHeight) * 0.5f);
    constexpr float barWidth = 5.0f;
    constexpr float gap = 1.0f;
    drawMeterBar(drawList, meterPos, ImVec2(meterPos.x + barWidth, meterPos.y + kMeterHeight), item.leftLevel);
    drawMeterBar(
        drawList,
        ImVec2(meterPos.x + barWidth + gap, meterPos.y),
        ImVec2(meterPos.x + barWidth * 2.0f + gap, meterPos.y + kMeterHeight),
        item.rightLevel);
}

void drawPreview(const Item& item, const char* label) {
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImRect previewRect = g.ComboPreviewData.PreviewRect;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawStereoMeter(drawList, item, previewRect.Min, previewRect.Max);

    const ImVec2 textMin(
        previewRect.Min.x + style.FramePadding.x + kMeterWidth + kIndicatorSpacing,
        previewRect.Min.y + style.FramePadding.y);
    const ImVec2 textMax(
        previewRect.Max.x - style.FramePadding.x,
        previewRect.Max.y);
    ImGui::RenderTextClipped(
        textMin,
        textMax,
        label != nullptr ? label : "",
        nullptr,
        nullptr);
}

const char* previewLabel(const int selectedIndex, std::span<const Item> items) {
    const int validIndex = validSelectionIndex(selectedIndex, items);
    if (validIndex < 0) {
        return "Select an audio device";
    }
    return items[static_cast<size_t>(validIndex)].label;
}

Item previewItem(const int selectedIndex, std::span<const Item> items) {
    const int validIndex = validSelectionIndex(selectedIndex, items);
    if (validIndex < 0) {
        return Item{previewLabel(selectedIndex, items), 0.0f, 0.0f};
    }
    return items[static_cast<size_t>(validIndex)];
}

std::string paddedLabel(const char* label) {
    return "    " + std::string(label != nullptr ? label : "");
}

}

bool renderCombo(const char* label, int& selectedIndex, std::span<const Item> items) {
    if (items.empty()) {
        return false;
    }

    const float comboWidth = selectorWidth(items);
    const float desiredPopupWidth = popupWidth(items, comboWidth);
    const char* previewText = previewLabel(selectedIndex, items);
    const Item currentPreviewItem = previewItem(selectedIndex, items);

    ImGui::SetNextItemWidth(comboWidth);
    ImGui::SetNextWindowSizeConstraints(ImVec2(desiredPopupWidth, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    const bool open = ImGui::BeginCombo(label, nullptr, ImGuiComboFlags_CustomPreview);

    bool changed = false;
    if (open) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float itemWidth = std::max(0.0f, desiredPopupWidth - style.WindowPadding.x * 2.0f);

        for (size_t i = 0; i < items.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool selected = selectedIndex == static_cast<int>(i);
            const std::string itemLabel = paddedLabel(items[i].label);
            if (ImGui::Selectable(itemLabel.c_str(), selected, 0, ImVec2(itemWidth, 0.0f))) {
                selectedIndex = static_cast<int>(i);
                changed = true;
            }
            drawStereoMeter(
                ImGui::GetWindowDrawList(),
                items[i],
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax());
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
    if (ImGui::BeginComboPreview()) {
        drawPreview(currentPreviewItem, previewText);
        ImGui::EndComboPreview();
    }

    return changed;
}

}
