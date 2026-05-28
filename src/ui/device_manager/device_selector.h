#pragma once

#include <imgui.h>

#include <span>

namespace DeviceSelector {

enum class IndicatorKind {
    Meter,
    Bluetooth
};

struct Item {
    const char* label = "";
    float leftLevel = 0.0f;
    float rightLevel = 0.0f;
    IndicatorKind indicatorKind = IndicatorKind::Meter;
};

bool renderCombo(const char* label, int& selectedIndex, std::span<const Item> items);

}
