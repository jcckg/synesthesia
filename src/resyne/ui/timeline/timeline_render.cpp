#include "timeline_render.h"

#include <algorithm>
#include <cmath>

#include "timeline_labels.h"
#include "timeline_gradient.h"
#include "timeline_viewport.h"

namespace ReSyne::Timeline::Render {

using UI::Utilities::ToolType;

ToolType resolveTool(const RenderContext& context,
                     bool commandLikeDown,
                     bool altDown) {
    ToolType baseTool = ToolType::Cursor;
    if (context.toolState) {
        baseTool = context.toolState->activeTool;
    }

    if (!context.allowGrab && baseTool == ToolType::Grab) {
        baseTool = ToolType::Cursor;
    }

    if (commandLikeDown && context.allowZoom) {
        return ToolType::Zoom;
    }
    if (altDown && context.allowGrab) {
        return ToolType::Grab;
    }
    return baseTool;
}

RenderResult renderTimelineImpl(TimelineState& state, const RenderContext& context) {
    RenderResult result{};

    if (context.size.x <= 1.0f || context.size.y <= 1.0f) {
        state.gradientRegionValid = false;
        ImGui::Dummy(context.size);
        return result;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 size = context.size;
    const ImVec2 regionMax(cursorPos.x + size.x, cursorPos.y + size.y);

    const float minTopBarHeight = 18.0f;
    const float maxTopBarHeight = 28.0f;
    float topBarHeight = std::clamp(size.y * 0.18f, minTopBarHeight, maxTopBarHeight);
    if (size.y - topBarHeight < 18.0f) {
        topBarHeight = std::max(minTopBarHeight, size.y - 18.0f);
    }
    const float gradientHeight = std::max(size.y - topBarHeight, 12.0f);

    const ImVec2 topMin = cursorPos;
    const ImVec2 topMax(cursorPos.x + size.x, cursorPos.y + topBarHeight);
    const ImVec2 gradientMin(cursorPos.x, topMax.y);
    const ImVec2 gradientMax(cursorPos.x + size.x, cursorPos.y + size.y);

    state.gradientRegionMin = gradientMin;
    state.gradientRegionMax = gradientMax;
    state.gradientRegionValid = size.x > 1.0f && gradientHeight > 1.0f;

    const bool interacting = state.isScrubberDragging || state.isGrabGestureActive;
    if (!interacting && context.playbackNormalisedPosition.has_value()) {
        state.scrubberNormalisedPosition = Viewport::clampNormalised(*context.playbackNormalisedPosition);
    }
    state.scrubberNormalisedPosition = Viewport::clampNormalised(state.scrubberNormalisedPosition);

    state.zoomFactor = std::clamp(state.zoomFactor, Viewport::MIN_ZOOM_FACTOR, Viewport::MAX_ZOOM_FACTOR);
    if (state.zoomFactor < Viewport::MIN_ZOOM_FACTOR + 1e-3f) {
        state.zoomFactor = Viewport::MIN_ZOOM_FACTOR;
    }

    const bool gradientHovered = ImGui::IsMouseHoveringRect(
        gradientMin, gradientMax, ImGuiHoveredFlags_None);

    const bool showOverlay = context.showDropOverlay && gradientHovered;
    if (showOverlay) {
        state.hoverOverlayAlpha = std::min(1.0f, state.hoverOverlayAlpha + context.deltaTime * 6.0f);
    } else {
        state.hoverOverlayAlpha = std::max(0.0f, state.hoverOverlayAlpha - context.deltaTime * 3.0f);
    }

    const bool commandLikeDown = io.KeyCtrl || io.KeySuper;
    const bool altDown = io.KeyAlt;
    const ToolType effectiveTool = resolveTool(context, commandLikeDown, altDown);

    if (gradientHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (effectiveTool == ToolType::Zoom && context.allowZoom) {
            state.isZoomGestureActive = true;
            state.zoomGestureStart = io.MousePos;
            state.zoomGestureStartFactor = state.zoomFactor;
            state.isGrabGestureActive = false;
            state.isScrubberDragging = false;
        } else if (effectiveTool == ToolType::Grab && state.zoomFactor > Viewport::MIN_ZOOM_FACTOR + 1e-3f) {
            state.isGrabGestureActive = true;
            state.grabGestureStart = io.MousePos;
            state.grabStartViewCentre = state.viewCentreNormalised;
            state.isScrubberDragging = false;
        }
    }

    bool viewChanged = false;

    if (context.trackpadInput && context.trackpadInput->pointerValid) {
        const auto& gestures = *context.trackpadInput;
        const ImVec2 pointer = gestures.pointerPos;
        const bool pointerInTimeline =
            pointer.x >= topMin.x && pointer.x <= gradientMax.x &&
            pointer.y >= topMin.y && pointer.y <= gradientMax.y;

        if (pointerInTimeline && gestures.hasPinch && context.allowZoom) {
            float viewStart = 0.0f;
            float viewEnd = 1.0f;
            float viewSpan = 1.0f;
            const float previousVisibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
            Viewport::computeViewWindow(state, previousVisibleFraction, viewStart, viewEnd, viewSpan);

            const float pointerLocal = Viewport::clampNormalised(
                (pointer.x - gradientMin.x) / std::max(size.x, 1.0f));
            const float pointerTimeline = viewStart + pointerLocal * viewSpan;

            const float pinchAmount = std::clamp(static_cast<float>(gestures.pinchDelta), -1.5f, 1.5f);
            const float zoomScale = std::exp(pinchAmount * Viewport::TRACKPAD_PINCH_SENSITIVITY);
            float targetZoom = state.zoomFactor * zoomScale;
            targetZoom = std::clamp(targetZoom, Viewport::MIN_ZOOM_FACTOR, Viewport::MAX_ZOOM_FACTOR);

            if (std::fabs(targetZoom - state.zoomFactor) > 0.0005f) {
                state.zoomFactor = targetZoom;

                const float newVisibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
                float newStart = pointerTimeline - pointerLocal * newVisibleFraction;
                float newEnd = newStart + newVisibleFraction;
                Viewport::constrainView(newStart, newEnd);
                state.viewCentreNormalised = std::clamp((newStart + newEnd) * 0.5f, 0.0f, 1.0f);
                state.isZoomGestureActive = false;
                state.isGrabGestureActive = false;
                viewChanged = true;
            }
        }

        if (pointerInTimeline && gestures.hasHorizontalPan &&
            state.zoomFactor > Viewport::MIN_ZOOM_FACTOR + 1e-3f && context.allowTrackpadPan) {
            const float visibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
            const float panAmount = std::clamp(static_cast<float>(gestures.panDeltaX), -80.0f, 80.0f);
            const float delta = panAmount * visibleFraction * Viewport::TRACKPAD_PAN_SENSITIVITY;
            state.viewCentreNormalised = std::clamp(state.viewCentreNormalised - delta, 0.0f, 1.0f);
            state.isGrabGestureActive = false;
            viewChanged = true;
        }
    }

    if (context.trackpadInput && gradientHovered &&
        std::fabs(context.trackpadInput->mouseWheelDelta) > 0.0001f && context.allowZoom &&
        !context.trackpadInput->hasPinch && !context.trackpadInput->pointerValid) {
        float viewStart = 0.0f;
        float viewEnd = 1.0f;
        float viewSpan = 1.0f;
        const float previousVisibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
        Viewport::computeViewWindow(state, previousVisibleFraction, viewStart, viewEnd, viewSpan);

        const float pointerLocal = Viewport::clampNormalised(
            (io.MousePos.x - gradientMin.x) / std::max(size.x, 1.0f));
        const float pointerTimeline = viewStart + pointerLocal * viewSpan;

        constexpr float SCROLL_WHEEL_SENSITIVITY = 0.15f;
        const float scrollAmount = std::clamp(context.trackpadInput->mouseWheelDelta, -3.0f, 3.0f);
        const float zoomScale = std::exp(scrollAmount * SCROLL_WHEEL_SENSITIVITY);
        float targetZoom = state.zoomFactor * zoomScale;
        targetZoom = std::clamp(targetZoom, Viewport::MIN_ZOOM_FACTOR, Viewport::MAX_ZOOM_FACTOR);

        if (std::fabs(targetZoom - state.zoomFactor) > 0.0005f) {
            state.zoomFactor = targetZoom;

            const float newVisibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
            float newStart = pointerTimeline - pointerLocal * newVisibleFraction;
            float newEnd = newStart + newVisibleFraction;
            Viewport::constrainView(newStart, newEnd);
            state.viewCentreNormalised = std::clamp((newStart + newEnd) * 0.5f, 0.0f, 1.0f);
            state.isZoomGestureActive = false;
            state.isGrabGestureActive = false;
            viewChanged = true;
        }
    }

    if (state.isZoomGestureActive) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float deltaY = state.zoomGestureStart.y - io.MousePos.y;
            float targetZoom = state.zoomGestureStartFactor + deltaY * Viewport::ZOOM_SENSITIVITY * state.zoomGestureStartFactor;
            targetZoom = std::clamp(targetZoom, Viewport::MIN_ZOOM_FACTOR, Viewport::MAX_ZOOM_FACTOR);
            if (std::fabs(targetZoom - state.zoomFactor) > 0.0005f) {
                state.zoomFactor = targetZoom;
                viewChanged = true;
            }
        } else {
            state.isZoomGestureActive = false;
        }
    }

    float visibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);

    if (state.isGrabGestureActive) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float deltaX = io.MousePos.x - state.grabGestureStart.x;
            const float normalisedDelta = (deltaX / std::max(size.x, 1.0f)) * visibleFraction;
            float newCenter = state.grabStartViewCentre - normalisedDelta;
            newCenter = std::clamp(newCenter, 0.0f, 1.0f);
            if (std::fabs(newCenter - state.viewCentreNormalised) > 0.0001f) {
                state.viewCentreNormalised = newCenter;
                viewChanged = true;
            }
        } else {
            state.isGrabGestureActive = false;
        }
    }

    float viewStart = 0.0f;
    float viewEnd = 1.0f;
    float viewSpan = 1.0f;
    Viewport::computeViewWindow(state, visibleFraction, viewStart, viewEnd, viewSpan);

    if (context.allowScrubbing && !state.isGrabGestureActive && !state.isZoomGestureActive) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(topMin, topMax, ImGuiHoveredFlags_None)) {
            const float local = Viewport::clampNormalised((io.MousePos.x - topMin.x) / std::max(size.x, 1.0f));
            float newScrubber = viewStart + local * viewSpan;
            newScrubber = Viewport::clampNormalised(newScrubber);
            state.scrubberNormalisedPosition = newScrubber;
            state.isScrubberDragging = true;
            state.scrubberGrabOffset = 0.0f;
            result.scrubberMoved = true;
            result.newScrubberPosition = newScrubber;
            result.behaviour = TimelineScrubBehaviour::Click;
            viewChanged = true;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                   io.MousePos.x >= topMin.x && io.MousePos.x <= topMax.x &&
                   io.MousePos.y >= topMin.y && io.MousePos.y <= topMax.y) {
            const float scrubberX = topMin.x + ((state.scrubberNormalisedPosition - viewStart) / viewSpan) * size.x;
            if (std::fabs(io.MousePos.x - scrubberX) <= 8.0f &&
                io.MousePos.y >= topMin.y && io.MousePos.y <= topMax.y) {
                state.isScrubberDragging = true;
                state.scrubberGrabOffset = io.MousePos.x - scrubberX;
            }
        }

        if (state.isScrubberDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float adjustedX = io.MousePos.x - state.scrubberGrabOffset;
                const float local = Viewport::clampNormalised((adjustedX - topMin.x) / std::max(size.x, 1.0f));
                float newScrubber = viewStart + local * viewSpan;
                newScrubber = Viewport::clampNormalised(newScrubber);
                if (std::fabs(newScrubber - state.scrubberNormalisedPosition) > 0.0001f) {
                    state.scrubberNormalisedPosition = newScrubber;
                    result.scrubberMoved = true;
                    result.newScrubberPosition = newScrubber;
                    result.behaviour = TimelineScrubBehaviour::Drag;
                    viewChanged = true;
                }
            } else {
                state.isScrubberDragging = false;
                state.scrubberGrabOffset = 0.0f;
            }
        }
    } else if (!context.allowScrubbing) {
            state.isScrubberDragging = false;
    }

    if (viewChanged) {
        visibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
        Viewport::computeViewWindow(state, visibleFraction, viewStart, viewEnd, viewSpan);
    }

    const ImU32 frameColour = IM_COL32(12, 12, 12, 255);
    const ImU32 topBarColour = IM_COL32(18, 18, 18, 255);
    const ImU32 gradientBgColour = IM_COL32(14, 14, 14, 255);
    const ImU32 frameBorderColour = IM_COL32(46, 46, 46, 255);

    drawList->AddRectFilled(cursorPos, regionMax, frameColour, 4.0f);
    drawList->AddRectFilled(topMin, topMax, topBarColour, 4.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilled(gradientMin, gradientMax, gradientBgColour);
    drawList->AddRect(cursorPos, regionMax, frameBorderColour, 4.0f);

    Gradient::drawGradient(drawList,
                           gradientMin,
                           gradientMax,
                           context.samples,
                           viewStart,
                           viewEnd,
                           context.colourSpace);

    drawList->AddLine(ImVec2(gradientMin.x, gradientMin.y),
                      ImVec2(gradientMax.x, gradientMin.y),
                      IM_COL32(58, 58, 58, 255),
                      1.0f);
    drawList->AddRect(gradientMin, gradientMax, IM_COL32(38, 38, 38, 255));

    const double duration = std::max(context.durationSeconds, 0.0);
    const double viewSpanNormalised = static_cast<double>(viewEnd - viewStart);
    const double visibleDuration = std::max(viewSpanNormalised * duration, 1e-6);

    const double majorStep = Labels::chooseMajorTickStep(visibleDuration);
    const double epsilon = majorStep * 0.05;
    const int minorDivisions = 4;
    const double minorStep = minorDivisions > 0 ? majorStep / static_cast<double>(minorDivisions) : majorStep;

    const double visibleStartSeconds = static_cast<double>(viewStart) * duration;
    const double visibleEndSeconds = static_cast<double>(viewEnd) * duration;

    if (duration > 0.0 && visibleDuration > 0.0) {
        double firstMajor = std::floor((visibleStartSeconds) / majorStep) * majorStep;
        if (firstMajor < 0.0) {
            firstMajor = 0.0;
        }

        for (double t = firstMajor; t <= visibleEndSeconds + epsilon; t += majorStep) {
            if (t < visibleStartSeconds - epsilon) {
                continue;
            }

            const float local = static_cast<float>((t - visibleStartSeconds) / visibleDuration);
            const float x = topMin.x + std::clamp(local, 0.0f, 1.0f) * size.x;

            drawList->AddLine(ImVec2(x, topMin.y + 2.0f),
                              ImVec2(x, topMax.y),
                              IM_COL32(180, 180, 180, 220),
                              1.0f);

            if (minorDivisions > 1 && majorStep > 0.0) {
                const double segmentEnd = t + majorStep;
                for (int i = 1; i < minorDivisions; ++i) {
                    const double minorTime = t + i * minorStep;
                    if (minorTime >= segmentEnd - epsilon || minorTime > visibleEndSeconds + epsilon) {
                        break;
                    }
                    if (minorTime < visibleStartSeconds - epsilon) {
                        continue;
                    }
                    const float minorLocal = static_cast<float>((minorTime - visibleStartSeconds) / visibleDuration);
                    const float mx = topMin.x + std::clamp(minorLocal, 0.0f, 1.0f) * size.x;
                    drawList->AddLine(ImVec2(mx, topMax.y - topBarHeight * 0.45f),
                                      ImVec2(mx, topMax.y - 2.0f),
                                      IM_COL32(90, 90, 90, 180),
                                      1.0f);
                }
            }

            std::string label = Labels::formatTickLabel(t, majorStep);
            ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            ImVec2 textPos(x - textSize.x * 0.5f, topMin.y + 4.0f);
            textPos.x = std::clamp(textPos.x, topMin.x + 4.0f, topMax.x - textSize.x - 4.0f);
            drawList->AddText(textPos, IM_COL32(215, 215, 215, 255), label.c_str());
        }
    }

    if (ImGui::IsMouseHoveringRect(topMin, topMax, ImGuiHoveredFlags_None) && context.allowScrubbing) {
        drawList->AddRectFilled(topMin, topMax, IM_COL32(255, 255, 255, 18), 4.0f, ImDrawFlags_RoundCornersTop);
    }

    const float scrubberLocal = (state.scrubberNormalisedPosition - viewStart) / viewSpan;
    const float clampedLocal = std::clamp(scrubberLocal, 0.0f, 1.0f);
    const float scrubberX = topMin.x + clampedLocal * size.x;

    if (state.zoomFactor > Viewport::MIN_ZOOM_FACTOR + 1e-3f) {
        const float indicatorHeight = std::clamp(topBarHeight * 0.2f, 2.0f, 6.0f);
        const float indicatorBottom = topMax.y - 2.0f;
        const float indicatorTop = indicatorBottom - indicatorHeight;
        const float indicatorStart = topMin.x + std::clamp(viewStart, 0.0f, 1.0f) * size.x;
        const float indicatorEnd = topMin.x + std::clamp(viewEnd, 0.0f, 1.0f) * size.x;
        const float barMin = std::min(indicatorStart, indicatorEnd);
        const float barMax = std::max(indicatorStart, indicatorEnd);
        if (barMax > barMin) {
            drawList->AddRectFilled(ImVec2(barMin, indicatorTop),
                                    ImVec2(barMax, indicatorBottom),
                                    IM_COL32(90, 90, 90, 180),
                                    2.0f);
            drawList->AddRect(ImVec2(barMin, indicatorTop),
                              ImVec2(barMax, indicatorBottom),
                              IM_COL32(140, 140, 140, 200),
                              2.0f);
        }
    }

    const float handleHalfWidth = 5.0f;
    const float handleHeight = std::max(topBarHeight - 6.0f, 10.0f);
    ImVec2 handleMin(scrubberX - handleHalfWidth, topMin.y + 3.0f);
    ImVec2 handleMax(scrubberX + handleHalfWidth, handleMin.y + handleHeight);

    const ImU32 scrubberShadow = IM_COL32(0, 0, 0, 140);
    const ImU32 scrubberLight = IM_COL32(230, 230, 230, 220);
    drawList->AddLine(ImVec2(scrubberX, topMax.y),
                      ImVec2(scrubberX, gradientMax.y),
                      scrubberShadow,
                      4.0f);
    drawList->AddLine(ImVec2(scrubberX, topMax.y),
                      ImVec2(scrubberX, gradientMax.y),
                      scrubberLight,
                      2.0f);

    drawList->AddRectFilled(handleMin, handleMax, IM_COL32(200, 200, 200, 235), 3.0f);
    drawList->AddRect(handleMin, handleMax, IM_COL32(60, 60, 60, 255), 3.0f);

    const float overlayAlpha = context.showDropOverlay ? state.hoverOverlayAlpha * 0.3f : 0.0f;
    const float combinedAlpha = overlayAlpha + context.dropFlashAlpha * 0.6f;
    if (combinedAlpha > 0.01f) {
        const float intensity = 0.82f;
        ImVec4 overlayColour(intensity, intensity, intensity, combinedAlpha);
        drawList->AddRectFilled(gradientMin, gradientMax,
                                 ImGui::ColorConvertFloat4ToU32(overlayColour));
    }

    ImGui::Dummy(size);
    return result;
}

}
