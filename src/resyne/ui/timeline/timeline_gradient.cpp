#include "timeline_gradient.h"

#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <imgui.h>

#include "colour/colour_core.h"

namespace {

struct LabColour {
	float L;
	float a;
	float b;
};

ImVec4 labToRGB(const LabColour& lab,
			ColourCore::ColourSpace colourSpace,
			const bool applyGamutMapping) {
	float r, g, b;
	ColourCore::LabtoRGB(lab.L, lab.a, lab.b, r, g, b, colourSpace, applyGamutMapping);
	r = std::clamp(r, 0.0f, 1.0f);
	g = std::clamp(g, 0.0f, 1.0f);
	b = std::clamp(b, 0.0f, 1.0f);
	return ImVec4(r, g, b, 1.0f);
}

void labSpanToRGB(std::span<const LabColour> labs,
				  std::span<ImVec4> colours,
				  const ColourCore::ColourSpace colourSpace,
				  const bool applyGamutMapping) {
	const size_t size = std::min(labs.size(), colours.size());
	if (size == 0) {
		return;
	}

	thread_local std::vector<float> LValues;
	thread_local std::vector<float> AValues;
	thread_local std::vector<float> BValues;
	thread_local std::vector<float> RValues;
	thread_local std::vector<float> GValues;
	thread_local std::vector<float> BOutValues;

	if (LValues.size() < size) {
		LValues.resize(size);
		AValues.resize(size);
		BValues.resize(size);
		RValues.resize(size);
		GValues.resize(size);
		BOutValues.resize(size);
	}

	for (size_t i = 0; i < size; ++i) {
		LValues[i] = labs[i].L;
		AValues[i] = labs[i].a;
		BValues[i] = labs[i].b;
	}

	for (size_t i = 0; i < size; ++i) {
		ColourCore::LabtoRGB(
			LValues[i],
			AValues[i],
			BValues[i],
			RValues[i],
			GValues[i],
			BOutValues[i],
			colourSpace,
			applyGamutMapping);
	}

	for (size_t i = 0; i < size; ++i) {
		colours[i] = ImVec4(
			std::clamp(RValues[i], 0.0f, 1.0f),
			std::clamp(GValues[i], 0.0f, 1.0f),
			std::clamp(BOutValues[i], 0.0f, 1.0f),
			1.0f);
	}
}

ImVec4 interpolateLabSpace(const LabColour& labA,
						   const LabColour& labB,
						   float t,
						   ColourCore::ColourSpace colourSpace,
						   const bool applyGamutMapping) {
	t = std::clamp(t, 0.0f, 1.0f);

	LabColour interpolated{
		std::lerp(labA.L, labB.L, t),
		std::lerp(labA.a, labB.a, t),
		std::lerp(labA.b, labB.b, t)
	};

	return labToRGB(interpolated, colourSpace, applyGamutMapping);
}

}

namespace ReSyne::Timeline::Gradient {

ImVec4 interpolateColour(const TimelineSample& a,
                         const TimelineSample& b,
                         float t,
                         ColourCore::ColourSpace colourSpace,
                         const bool applyGamutMapping) {
	LabColour labA{a.labL, a.labA, a.labB};
	LabColour labB{b.labL, b.labA, b.labB};
	return interpolateLabSpace(labA, labB, t, colourSpace, applyGamutMapping);
}

void drawGradient(ImDrawList* drawList,
                  const ImVec2& min,
                  const ImVec2& max,
                  const std::vector<TimelineSample>& samples,
                  float visibleStart,
                  float visibleEnd,
                  ColourCore::ColourSpace colourSpace,
                  const bool applyGamutMapping) {
	const float width = max.x - min.x;
	const float height = max.y - min.y;
	if (samples.empty() || width <= 0.0f || height <= 0.0f || visibleEnd <= visibleStart) {
		return;
	}

	const size_t sampleCount = samples.size();
	std::vector<LabColour> labColours(sampleCount);
	for (size_t i = 0; i < sampleCount; ++i) {
		labColours[i] = LabColour{samples[i].labL, samples[i].labA, samples[i].labB};
	}

	if (sampleCount == 1) {
		drawList->AddRectFilled(
			min,
			max,
			ImGui::ColorConvertFloat4ToU32(samples.front().colour));
		return;
	}

	const float visibleSpan = visibleEnd - visibleStart;
	const float invSegmentCount = 1.0f / static_cast<float>(sampleCount - 1);

	// Number of sub-divisions per segment for smooth Lab interpolation
	// Higher = smoother but slower; 8 provides good balance
	constexpr int SUB_DIVISIONS = 8;

	for (size_t i = 0; i < sampleCount - 1; ++i) {
		const float segmentStart = static_cast<float>(i) * invSegmentCount;
		const float segmentEnd = static_cast<float>(i + 1) * invSegmentCount;

		if (segmentEnd < visibleStart || segmentStart > visibleEnd) {
			continue;
		}

		const float clippedStart = std::max(segmentStart, visibleStart);
		const float clippedEnd = std::min(segmentEnd, visibleEnd);
		if (clippedEnd <= clippedStart) {
			continue;
		}

		const float localStart = (clippedStart - visibleStart) / visibleSpan;
		const float localEnd = (clippedEnd - visibleStart) / visibleSpan;
		const float x1 = min.x + localStart * width;
		const float x2 = min.x + localEnd * width;
		if (x2 <= x1) {
			continue;
		}

		// Subdivide segment for perceptually uniform Lab-space interpolation
		const float segmentWidth = x2 - x1;
		std::array<LabColour, SUB_DIVISIONS + 1> subLabs{};
		std::array<ImVec4, SUB_DIVISIONS + 1> subColours{};
		for (int sub = 0; sub <= SUB_DIVISIONS; ++sub) {
			const float t = static_cast<float>(sub) / static_cast<float>(SUB_DIVISIONS);
			subLabs[static_cast<size_t>(sub)] = {
				std::lerp(labColours[i].L, labColours[i + 1].L, t),
				std::lerp(labColours[i].a, labColours[i + 1].a, t),
				std::lerp(labColours[i].b, labColours[i + 1].b, t)
			};
		}

		labSpanToRGB(subLabs, subColours, colourSpace, applyGamutMapping);

		for (int sub = 0; sub < SUB_DIVISIONS; ++sub) {
			const float t1 = static_cast<float>(sub) / static_cast<float>(SUB_DIVISIONS);
			const float t2 = static_cast<float>(sub + 1) / static_cast<float>(SUB_DIVISIONS);
			const float subX1 = x1 + t1 * segmentWidth;
			const float subX2 = x1 + t2 * segmentWidth;
			const ImU32 col1 = ImGui::ColorConvertFloat4ToU32(subColours[static_cast<size_t>(sub)]);
			const ImU32 col2 = ImGui::ColorConvertFloat4ToU32(subColours[static_cast<size_t>(sub + 1)]);
			drawList->AddRectFilledMultiColor(
				ImVec2(subX1, min.y),
				ImVec2(subX2, max.y),
				col1,
				col2,
				col2,
				col1);
		}
	}
}

}
