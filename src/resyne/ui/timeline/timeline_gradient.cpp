#include "timeline_gradient.h"

#include <algorithm>
#include <vector>
#include <imgui.h>

#include "colour/colour_mapper.h"

namespace {

struct LabColour {
	float L;
	float a;
	float b;
};

ImVec4 labToRGB(const LabColour& lab,
			ColourMapper::ColourSpace colourSpace) {
	float X, Y, Z;
	ColourMapper::LabtoXYZ(lab.L, lab.a, lab.b, X, Y, Z);

	float r, g, b;
	ColourMapper::XYZtoRGB(X, Y, Z, r, g, b, colourSpace, true, false);
	r = std::clamp(r, 0.0f, 1.0f);
	g = std::clamp(g, 0.0f, 1.0f);
	b = std::clamp(b, 0.0f, 1.0f);
	return ImVec4(r, g, b, 1.0f);
}

ImVec4 interpolateLabSpace(const LabColour& labA,
						   const LabColour& labB,
						   float t,
						   ColourMapper::ColourSpace colourSpace) {
	t = std::clamp(t, 0.0f, 1.0f);

	LabColour interpolated{
		std::lerp(labA.L, labB.L, t),
		std::lerp(labA.a, labB.a, t),
		std::lerp(labA.b, labB.b, t)
	};

	return labToRGB(interpolated, colourSpace);
}

}

namespace ReSyne::Timeline::Gradient {

ImVec4 interpolateColour(const TimelineSample& a,
                         const TimelineSample& b,
                         float t,
                         ColourMapper::ColourSpace colourSpace) {
	LabColour labA{a.labL, a.labA, a.labB};
	LabColour labB{b.labL, b.labA, b.labB};
	return interpolateLabSpace(labA, labB, t, colourSpace);
}

void drawGradient(ImDrawList* drawList,
                  const ImVec2& min,
                  const ImVec2& max,
                  const std::vector<TimelineSample>& samples,
                  float visibleStart,
                  float visibleEnd,
                  ColourMapper::ColourSpace colourSpace) {
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
		for (int sub = 0; sub < SUB_DIVISIONS; ++sub) {
			const float t1 = static_cast<float>(sub) / static_cast<float>(SUB_DIVISIONS);
			const float t2 = static_cast<float>(sub + 1) / static_cast<float>(SUB_DIVISIONS);

			const float subX1 = x1 + t1 * segmentWidth;
			const float subX2 = x1 + t2 * segmentWidth;

			// Lab-space interpolated colours at sub-segment boundaries
			const ImVec4 subColour1 = interpolateLabSpace(labColours[i], labColours[i + 1], t1, colourSpace);
			const ImVec4 subColour2 = interpolateLabSpace(labColours[i], labColours[i + 1], t2, colourSpace);

			const ImU32 col1 = ImGui::ColorConvertFloat4ToU32(subColour1);
			const ImU32 col2 = ImGui::ColorConvertFloat4ToU32(subColour2);

			// ImGui does linear RGB interpolation within each rect, but with 8 subdivisions
			// and Lab-space colours at boundaries, the overall result is perceptually smooth
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
