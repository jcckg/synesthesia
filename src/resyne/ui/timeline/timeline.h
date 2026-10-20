#pragma once

#include <imgui.h>

#include <optional>
#include <vector>

#include "resyne/ui/toolbar/tool_state.h"
#include "colour/colour_core.h"
#include "colour/colour_track.h"

namespace Renderer {
class PresentationResources;
}

enum class TimelineScrubBehaviour {
    None,
    Click,
    Drag
};

namespace ReSyne::Timeline {

inline constexpr float RULER_HEIGHT = 23.0f;

struct TimelineSample {
	double timestamp = 0.0;
	ImVec4 colour = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	float labL = 0.0f;
	float labA = 0.0f;
	float labB = 0.0f;
};

struct TimelineState {
    ImVec2 gradientRegionMin = ImVec2(0.0f, 0.0f);
    ImVec2 gradientRegionMax = ImVec2(0.0f, 0.0f);
    bool gradientRegionValid = false;
    float hoverOverlayAlpha = 0.0f;
    float scrubberNormalisedPosition = 0.0f;
    bool isScrubberDragging = false;
    float zoomFactor = 1.0f;
    float zoomIndicatorAlpha = 0.0f;
    float zoomIndicatorHoldSeconds = 0.0f;
    bool isZoomGestureActive = false;
    bool isGrabGestureActive = false;
    ImVec2 zoomGestureStart = ImVec2(0.0f, 0.0f);
    float zoomGestureStartFactor = 1.0f;
    ImVec2 grabGestureStart = ImVec2(0.0f, 0.0f);
    float viewCentreNormalised = 0.5f;
    float grabStartViewCentre = 0.5f;
    bool trackScrubber = false;
};

struct TrackpadGestureInput {
    bool hasPinch = false;
    float pinchDelta = 0.0f;
    bool hasHorizontalPan = false;
    float panDeltaX = 0.0f;
    bool pointerValid = false;
    ImVec2 pointerPos = ImVec2(0.0f, 0.0f);
    float mouseWheelDelta = 0.0f;
};

struct RenderContext {
	const std::vector<TimelineSample>& samples;
    uint64_t sampleRevision = 0;
    double durationSeconds = 0.0;
    ImVec2 size = ImVec2(0.0f, 0.0f);
    float dropFlashAlpha = 0.0f;
    float deltaTime = 0.0f;
    ColourCore::ColourSpace colourSpace = ColourCore::ColourSpace::Rec2020;
    bool applyGamutMapping = true;
    std::optional<float> playbackNormalisedPosition;
    bool allowScrubbing = false;
    bool showDropOverlay = false;
    bool allowZoom = false;
    bool allowGrab = false;
    bool allowTrackpadPan = false;
    const UI::Utilities::ToolState* toolState = nullptr;
    const TrackpadGestureInput* trackpadInput = nullptr;
    Renderer::PresentationResources* presentationResources = nullptr;
    const ColourPresentation::ColourTrack* colourTrack = nullptr;
    std::optional<double> availableDurationSeconds = std::nullopt;
};

struct RenderResult {
    bool scrubberMoved = false;
    float newScrubberPosition = 0.0f;
    TimelineScrubBehaviour behaviour = TimelineScrubBehaviour::None;
};

RenderResult renderTimeline(TimelineState& state, const RenderContext& context);

}
