#include "spectrum_analyser.h"
#include "spectrum_curve.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr ImVec4 SPECTRUM_COLOUR = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);
}

void SpectrumAnalyser::drawSpectrumWindow(
    const std::vector<float>& contributions,
    const ImVec2& displaySize,
    float sidebarWidth,
    bool sidebarOnLeft,
    float bottomPanelHeight
) {
    const float spectrumX = sidebarOnLeft ? sidebarWidth : 0.0f;
    const float availableHeight = std::max(0.0f, displaySize.y - bottomPanelHeight);
    const float spectrumHeight = std::min(120.0f, availableHeight * .15f);
    const auto spectrumPos = ImVec2(spectrumX, availableHeight - spectrumHeight);
    const auto spectrumSize = ImVec2(std::max(0.0f, displaySize.x - sidebarWidth), spectrumHeight);
    if (spectrumSize.x < 1.0f || spectrumSize.y < 1.0f) return;

    ImGui::SetNextWindowPos(spectrumPos);
    ImGui::SetNextWindowSize(spectrumSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::Begin("##SpectrumAnalyser", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);

    const auto count = static_cast<std::size_t>(std::clamp(std::ceil(spectrumSize.x * ImGui::GetIO().DisplayFramebufferScale.x), 241.0f, 8192.0f));
    if (frequencies_.size() != count) {
        frequencies_.resize(count);
        for (std::size_t i = 0; i < count; ++i) frequencies_[i] = static_cast<float>(
            ColourPresentation::contributionMinimumFrequency * std::pow(
                ColourPresentation::contributionMaximumFrequency / ColourPresentation::contributionMinimumFrequency,
                static_cast<double>(i) / static_cast<double>(count-1)));
    }
    const auto heights = UI::Spectrum::curveHeights(contributions, count);

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotMinSize, ImVec2(0, 0));
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_AxisBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImPlot::BeginPlot("##Spectrum", spectrumSize,
                          ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect |
                          ImPlotFlags_NoMouseText | ImPlotFlags_NoFrame |
                          ImPlotFlags_NoInputs)) {

        ImPlot::SetupAxis(ImAxis_X1, nullptr,
                         ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks |
                         ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr,
                         ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks |
                         ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisLimits(ImAxis_X1, ColourPresentation::contributionMinimumFrequency, ColourPresentation::contributionMaximumFrequency);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);

        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.04);

        if (contributions.size() == ColourPresentation::contributionBandCount) {
            ImPlotSpec fillStyle;
            fillStyle.FillColor = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
            ImPlot::PlotShaded("##Fill", frequencies_.data(), heights.data(), static_cast<int>(count), 0.0, fillStyle);
            ImPlotSpec lineStyle;
            lineStyle.LineColor = SPECTRUM_COLOUR;
            lineStyle.LineWeight = 1.5f;
            ImPlot::PlotLine("##Line", frequencies_.data(), heights.data(), static_cast<int>(count), lineStyle);
        }

        ImPlot::EndPlot();
    }

    ImPlot::PopStyleColor(6);
    ImPlot::PopStyleVar(3);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
