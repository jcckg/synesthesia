#include "timeline_render.h"

#include <algorithm>
#include "imgui_internal.h"
#include <cmath>

#include "renderer/presentation_resources.h"
#include "timeline_labels.h"
#include "timeline_gradient.h"
#include "timeline_rasteriser.h"
#include "timeline_viewport.h"
#include "ui/styling/system_theme/system_theme_detector.h"

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
        state.isScrubberDragging = false;
        ImGui::Dummy(context.size);
        return result;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 size = context.size;
    const ImVec2 regionMax(cursorPos.x + size.x, cursorPos.y + size.y);

    const float topBarHeight = std::min(RULER_HEIGHT, std::max(size.y - 12.0f, 0.0f));
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

    const bool timelineHovered = ImGui::IsWindowHovered();
    const bool rulerHovered = timelineHovered && ImGui::IsMouseHoveringRect(topMin, topMax);
    const bool gradientHovered = timelineHovered && ImGui::IsMouseHoveringRect(
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
    const float previousZoom = state.zoomFactor;
    const float previousViewCentre = state.viewCentreNormalised;

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
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && rulerHovered) {
            const float local = Viewport::clampNormalised((io.MousePos.x - topMin.x) / std::max(size.x, 1.0f));
            float newScrubber = viewStart + local * viewSpan;
            newScrubber = Viewport::clampNormalised(newScrubber);
            state.scrubberNormalisedPosition = newScrubber;
            state.isScrubberDragging = true;
            result.scrubberMoved = true;
            result.newScrubberPosition = newScrubber;
            result.behaviour = TimelineScrubBehaviour::Click;
            viewChanged = true;
        }

        if (state.isScrubberDragging) {
            if (ImGui::IsMousePosValid() && (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
                const float local = Viewport::clampNormalised((io.MousePos.x - topMin.x) / std::max(size.x, 1.0f));
                float newScrubber = viewStart + local * viewSpan;
                newScrubber = Viewport::clampNormalised(newScrubber);
                if (newScrubber != state.scrubberNormalisedPosition) {
                    state.scrubberNormalisedPosition = newScrubber;
                    result.scrubberMoved = true;
                    result.newScrubberPosition = newScrubber;
                    result.behaviour = TimelineScrubBehaviour::Drag;
                    viewChanged = true;
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                state.isScrubberDragging = false;
            }
        }
    } else if (!context.allowScrubbing) {
        state.isScrubberDragging = false;
    }

    if (viewChanged) {
        visibleFraction = Viewport::computeVisibleFraction(state.zoomFactor);
        Viewport::computeViewWindow(state, visibleFraction, viewStart, viewEnd, viewSpan);
    }

    const bool navigating = state.zoomFactor != previousZoom ||
        (!state.trackScrubber && state.viewCentreNormalised != previousViewCentre);
    const float deltaTime = std::max(context.deltaTime, 0.0f);
    if (state.zoomFactor > Viewport::MIN_ZOOM_FACTOR + 1e-3f) {
        state.zoomIndicatorHoldSeconds = navigating ? 1.0f :
            std::max(0.0f, state.zoomIndicatorHoldSeconds - deltaTime);
        const float targetAlpha = state.zoomIndicatorHoldSeconds > 0.0f ? 1.0f : 0.0f;
        const float responseSeconds = targetAlpha > state.zoomIndicatorAlpha ? 0.045f : 0.22f;
        state.zoomIndicatorAlpha += (targetAlpha - state.zoomIndicatorAlpha) *
            -std::expm1(-deltaTime / responseSeconds);
    } else {
        state.zoomIndicatorAlpha = 0.0f;
        state.zoomIndicatorHoldSeconds = 0.0f;
    }

    const bool isLightMode = SystemThemeDetector::isSystemInDarkMode() == false;
    const ImU32 frameColour = ImGui::GetColorU32(ImGuiCol_WindowBg);
    const ImU32 topBarColour = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 gradientBgColour = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 frameBorderColour = ImGui::GetColorU32(ImGuiCol_Border);

    drawList->AddRectFilled(cursorPos, regionMax, frameColour, 4.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilled(topMin, topMax, topBarColour, 4.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilled(gradientMin, gradientMax, gradientBgColour);

    if (context.availableDurationSeconds.has_value()) {
        const double loaded = context.durationSeconds > 0.0
            ? *context.availableDurationSeconds / context.durationSeconds : 0.0;
        const float loadedEnd = static_cast<float>(std::clamp((loaded - static_cast<double>(viewStart)) / static_cast<double>(viewSpan), 0.0, 1.0));
        drawList->PushClipRect(gradientMin, ImVec2(gradientMin.x + size.x * loadedEnd, gradientMax.y), true);
    }
    bool usedHighPrecisionGradient = false;
    if (context.presentationResources != nullptr) {
        const int rasterWidth = static_cast<int>(gradientPixelWidth(size.x, ImGui::GetWindowViewport()->FramebufferScale.x));
        const ImTextureID textureId = context.presentationResources->updateTimelineTexture(
            context.samples,
            context.sampleRevision,
            viewStart,
            viewEnd,
            rasterWidth,
            context.colourSpace,
            context.applyGamutMapping,
            context.colourTrack,
            context.durationSeconds);
        if (textureId != ImTextureID_Invalid) {
            drawList->AddImage(textureId, gradientMin, gradientMax);
            usedHighPrecisionGradient = true;
        }
    }

    if (!usedHighPrecisionGradient) {
        Gradient::drawGradient(drawList,
                               gradientMin,
                               gradientMax,
                               context.samples,
                               viewStart,
                               viewEnd,
                               context.colourSpace,
                               context.applyGamutMapping,
                               context.colourTrack,
                               context.durationSeconds);
    }

    if (context.availableDurationSeconds.has_value()) drawList->PopClipRect();

    const float dividerY = topMax.y - 1.0f;
    drawList->AddLine(ImVec2(gradientMin.x, dividerY),
                      ImVec2(gradientMax.x, dividerY),
                      frameBorderColour,
                      1.0f);
    drawList->AddRect(cursorPos, regionMax, frameBorderColour, 4.0f, 1.0f, ImDrawFlags_RoundCornersTop);

    const double duration = std::max(context.durationSeconds, 0.0);
    const double viewSpanNormalised = static_cast<double>(viewSpan);
    const double visibleDuration = std::max(viewSpanNormalised * duration, 1e-6);

    const double viewCentre = std::clamp(static_cast<double>(state.viewCentreNormalised),
        viewSpanNormalised * 0.5, 1.0 - viewSpanNormalised * 0.5);
    const double visibleCentreSeconds = viewCentre * duration;
    const double visibleStartSeconds = visibleCentreSeconds - visibleDuration * 0.5;
    const double visibleEndSeconds = visibleCentreSeconds + visibleDuration * 0.5;
    const bool showHours = duration >= 3600.0;
    ImFont* labelFont = ImGui::GetFont();
    const float labelFontSize = std::max(10.0f, std::round(ImGui::GetFontSize() * 0.78f));
    const auto referenceLabel = Labels::formatTickLabel(duration,
        Labels::chooseMajorTickStep(visibleDuration), showHours);
    const float labelSpacing = std::max(72.0f,
        labelFont->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, referenceLabel.c_str()).x + 24.0f);
    const double maximumTicks = static_cast<double>(std::clamp(std::floor(size.x / labelSpacing), 1.0f, 16.0f));
    const double majorStep = Labels::chooseMajorTickStep(visibleDuration, maximumTicks);
    const double epsilon = majorStep * 1e-6;
    const double majorSpacing = majorStep / visibleDuration * static_cast<double>(size.x);
    const int minorDivisions = Labels::chooseMinorTickDivisions(majorStep, majorSpacing);
    const double minorStep = majorStep / static_cast<double>(minorDivisions);
    const float tickBottom = dividerY - 3.0f;
    auto majorTick = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    majorTick.x *= 0.8f;
    majorTick.y *= 0.8f;
    majorTick.z *= 0.8f;
    const ImU32 majorTickColour = ImGui::GetColorU32(majorTick);
    const ImU32 minorTickColour = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f);
    const ImU32 labelColour = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
    const auto tickX = [&](double time) {
        return topMin.x + size.x * 0.5f +
            static_cast<float>((time - visibleCentreSeconds) / visibleDuration * static_cast<double>(size.x));
    };

    if (duration > 0.0 && visibleDuration > 0.0) {
        double firstMajor = std::floor((visibleStartSeconds) / majorStep) * majorStep;
        if (firstMajor < 0.0) {
            firstMajor = 0.0;
        }

        drawList->PushClipRect(topMin, topMax, true);
        drawList->PushDrawFlag(ImDrawFlags_TextNoPixelSnap, true);
        for (double t = firstMajor; t <= visibleEndSeconds + epsilon; t += majorStep) {
            if (t >= visibleStartSeconds - epsilon) {
                const float x = tickX(t);
                drawList->AddLine(ImVec2(x, tickBottom - 4.0f), ImVec2(x, tickBottom), majorTickColour, 1.0f);
                const auto label = Labels::formatTickLabel(t, majorStep, showHours);
                const ImVec2 textSize = labelFont->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label.c_str());
                if (textSize.x + 8.0f <= size.x) {
                    float textX = x - textSize.x * 0.5f;
                    if (viewSpan >= 0.999f)
                        textX = std::clamp(textX, topMin.x + 4.0f, topMax.x - textSize.x - 4.0f);
                    const ImVec2 textPos(textX, std::floor(topMin.y + 1.0f));
                    drawList->AddText(labelFont, labelFontSize, textPos, labelColour, label.c_str());
                }
            }

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
                    const float mx = tickX(minorTime);
                    const bool intermediate = i * 2 == minorDivisions ||
                        (minorDivisions >= 10 && minorDivisions % 5 == 0 && i % 5 == 0);
                    const float tickHeight = intermediate ? 3.0f : 2.0f;
                    drawList->AddLine(ImVec2(mx, tickBottom - tickHeight), ImVec2(mx, tickBottom),
                        intermediate ? majorTickColour : minorTickColour, 1.0f);
                }
            }
        }
        drawList->PopDrawFlag();
        drawList->PopClipRect();
    }

    if (rulerHovered && context.allowScrubbing) {
        drawList->AddRectFilled(topMin, topMax, IM_COL32(255, 255, 255, 18), 4.0f, ImDrawFlags_RoundCornersTop);
    }

    const float scrubberLocal = static_cast<float>(0.5 +
        (static_cast<double>(state.scrubberNormalisedPosition) - viewCentre) / viewSpanNormalised);
    const float clampedLocal = std::clamp(scrubberLocal, 0.0f, 1.0f);
    const float scrubberX = topMin.x + clampedLocal * size.x + 0.5f;

    if (state.zoomFactor > Viewport::MIN_ZOOM_FACTOR + 1e-3f && state.zoomIndicatorAlpha > 0.01f) {
        const float indicatorHeight = std::min(2.0f, topBarHeight * 0.1f);
        const float indicatorBottom = topMax.y - 1.0f;
        const float indicatorTop = indicatorBottom - indicatorHeight;
        const float indicatorStart = topMin.x + std::clamp(viewStart, 0.0f, 1.0f) * size.x;
        const float indicatorEnd = topMin.x + std::clamp(viewEnd, 0.0f, 1.0f) * size.x;
        const float barMin = std::min(indicatorStart, indicatorEnd);
        const float barMax = std::max(indicatorStart, indicatorEnd);
        if (barMax > barMin) {
            auto indicatorColour = ImGui::ColorConvertU32ToFloat4(
                isLightMode ? IM_COL32(95, 95, 95, 200) : IM_COL32(190, 190, 190, 180));
            indicatorColour.w *= state.zoomIndicatorAlpha;
            drawList->AddRectFilled(ImVec2(barMin, indicatorTop),
                                    ImVec2(barMax, indicatorBottom),
                                    ImGui::GetColorU32(indicatorColour),
                                    1.0f);
        }
    }

    const float handleHalfWidth = 4.0f;
    const float handleHeight = std::max(topBarHeight - 6.0f, 10.0f);
    ImVec2 handleMin(scrubberX - handleHalfWidth, topMin.y + 3.0f);
    ImVec2 handleMax(scrubberX + handleHalfWidth, handleMin.y + handleHeight);

    const ImU32 scrubberShadow = isLightMode ? IM_COL32(0, 0, 0, 40) : IM_COL32(0, 0, 0, 100);
    const ImU32 scrubberLight = IM_COL32(255, 255, 255, 240);
    drawList->AddLine(ImVec2(scrubberX, handleMax.y),
                      ImVec2(scrubberX, gradientMax.y),
                      scrubberShadow,
                      2.0f);
    drawList->AddLine(ImVec2(scrubberX, handleMax.y),
                      ImVec2(scrubberX, gradientMax.y),
                      scrubberLight,
                      1.0f);

    drawList->AddRectFilled(handleMin, handleMax, isLightMode ? IM_COL32(230, 230, 230, 255) : IM_COL32(200, 200, 200, 235), 2.0f);
    drawList->AddRect(handleMin, handleMax, isLightMode ? IM_COL32(160, 160, 160, 255) : IM_COL32(60, 60, 60, 255), 2.0f);

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
