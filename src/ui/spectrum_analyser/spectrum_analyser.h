#pragma once

#include <vector>
#include "imgui.h"
#include "implot.h"

class SpectrumAnalyser {
public:
    void drawSpectrumWindow(const std::vector<float>& contributions,
        const ImVec2& displaySize, float sidebarWidth, bool sidebarOnLeft = false,
        float bottomPanelHeight = 0.0f);
private:
    std::vector<float> frequencies_;
};
