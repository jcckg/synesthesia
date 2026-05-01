#pragma once

#include <imgui.h>

#include <span>

namespace DeviceSelector {

struct Item {
    const char* label = "";
    float leftLevel = 0.0f;
    float rightLevel = 0.0f;
};

bool renderCombo(const char* label, int& selectedIndex, std::span<const Item> items);

}
