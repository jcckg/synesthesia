#include "renderer/osc_routing_window.h"

#include "renderer/render_utils.h"
#include "renderer/styling/platform_styling.h"
#include "osc/synesthesia_osc_integration.h"
#include "ui/ui.h"
#include "ui/styling/styling.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <portable-file-dialogs.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <type_traits>
#include <string_view>

namespace Renderer {

namespace {

constexpr bgfx::ViewId kClearView = 6;
constexpr bgfx::ViewId kUIView = 7;

float toggleWidth() {
    return std::floor(ImGui::GetTextLineHeight() * 0.8f) * 2.0f + 5.0f + ImGui::CalcTextSize("Off").x;
}

bool slidingToggle(const char* id, bool& value) {
    const float lineHeight = ImGui::GetTextLineHeight();
    const float height = std::floor(lineHeight * 0.8f);
    const float width = height * 2.0f;
    const auto position = ImGui::GetCursorScreenPos();
    const bool changed = ImGui::InvisibleButton(id, {toggleWidth(), lineHeight});
    if (changed) value = !value;
    const auto key = ImGui::GetID(id);
    auto* storage = ImGui::GetStateStorage();
    const float target = value ? 1.0f : 0.0f;
    float progress = storage->GetFloat(key, target);
    progress += (target - progress) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
    storage->SetFloat(key, progress);
    const auto red = ImVec4(0.62f, 0.22f, 0.22f, 1.0f);
    const auto green = ImVec4(0.20f, 0.52f, 0.34f, 1.0f);
    const auto colour = ImVec4(red.x + (green.x - red.x) * progress,
        red.y + (green.y - red.y) * progress, red.z + (green.z - red.z) * progress, 1.0f);
    const ImVec2 top(position.x, position.y + std::floor((lineHeight - height) * 0.5f));
    const float knobLeft = top.x + 2.0f + progress * (width - height);
    const float rounding = ImGui::GetStyle().FrameRounding;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(top, {top.x + width, top.y + height}, ImGui::ColorConvertFloat4ToU32(colour), rounding);
    draw->AddRectFilled({knobLeft, top.y + 2.0f}, {knobLeft + height - 4.0f, top.y + height - 2.0f}, IM_COL32(245, 245, 245, 255), rounding);
    if (ImGui::IsItemFocused()) draw->AddRect({top.x - 2, top.y - 2}, {top.x + width + 2, top.y + height + 2},
        ImGui::GetColorU32(ImGuiCol_NavCursor), rounding);
    const char* label = value ? "On" : "Off";
    draw->AddText({position.x + toggleWidth() - ImGui::CalcTextSize(label).x, position.y}, ImGui::GetColorU32(ImGuiCol_Text), label);
    return changed;
}

std::string formattedBytes(std::size_t bytes) {
    char buffer[48];
    if (bytes < 1000) std::snprintf(buffer, sizeof(buffer), "%zu B", bytes);
    else std::snprintf(buffer, sizeof(buffer), "%.2f kB", static_cast<double>(bytes) / 1000.0);
    return buffer;
}

std::string scalarPreview(const Synesthesia::OSC::OSCArgument& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>) return item;
        else if constexpr (std::is_same_v<T, bool>) return item ? "True" : "False";
        else if constexpr (std::is_same_v<T, float>) {
            if (!std::isfinite(item)) return "Unavailable";
            char text[48];
            std::snprintf(text, sizeof(text), "%.5g", static_cast<double>(item));
            return text;
        } else return std::to_string(item);
    }, value);
}

}

OSCRoutingWindow::OSCRoutingWindow() = default;
OSCRoutingWindow::~OSCRoutingWindow() { close(); }

bool OSCRoutingWindow::isOpen() const {
    return window_.handle() != nullptr && uiContext_.isInitialised();
}

bool OSCRoutingWindow::open(bgfx::TextureFormat::Enum colourFormat) {
    if (isOpen()) {
        window_.show();
        return true;
    }
    WindowOptions options;
    options.title = "Synesthesia OSC Routing";
    options.width = 1060;
    options.height = 650;
    options.minimumWidth = 780;
    options.minimumHeight = 360;
    colourFormat_ = colourFormat;
    if (!window_.create(options)) return false;
    Styling::applyPlatformWindowStyling(window_.handle());
    window_.show();
    window_.realiseForRenderer();
    if (!uiContext_.initialise(window_, kUIView, colourFormat == bgfx::TextureFormat::RGBA16F) || !refreshFrameBuffer()) {
        close();
        return false;
    }
    TrackpadGestures::attach(window_.handle(), false);
    Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().setMonitoring(true);
    return true;
}

bool OSCRoutingWindow::isCommandTarget(UI::Menu::Command command) const {
    return isOpen() && window_.isFocused() &&
        (command == UI::Menu::Command::Load || command == UI::Menu::Command::SearchExplorer);
}

UI::Menu::State OSCRoutingWindow::queryCommand(UI::Menu::Command command) const {
    UI::Menu::State result;
    if (command != UI::Menu::Command::Load && command != UI::Menu::Command::SearchExplorer) return result;
    result.enabled = isOpen() && !loadDialog_ && !saveDialog_;
    result.title = command == UI::Menu::Command::Load ? "Load OSC Configuration" : "Search OSC Outputs";
    return result;
}

bool OSCRoutingWindow::executeCommand(UI::Menu::Command command) {
    if (!queryCommand(command).enabled) return false;
    if (command == UI::Menu::Command::Load) openLoadDialog();
    else searchFocusRequested_ = true;
    return true;
}

void OSCRoutingWindow::openLoadDialog() {
    loadDialog_ = std::make_unique<pfd::open_file>("Load OSC Configuration", configurationPath_,
        std::vector<std::string>{"JSON configuration", "*.json"});
}

void OSCRoutingWindow::close() {
    Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance().setMonitoring(false);
    TrackpadGestures::detach(window_.handle());
    scroll_.reset();
    uiContext_.shutdown();
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
        frameBuffer_ = BGFX_INVALID_HANDLE;
        bgfx::frame();
        bgfx::frame();
    }
    window_.destroy();
    nativeWindowHandle_ = nullptr;
    width_ = height_ = 0;
}

bool OSCRoutingWindow::refreshFrameBuffer() {
    const auto size = window_.framebufferSize();
    auto swapChain = window_.swapChain();
    if (size.width <= 0 || size.height <= 0 || swapChain.nwh == nullptr) return false;
    if (bgfx::isValid(frameBuffer_) && width_ == size.width && height_ == size.height && nativeWindowHandle_ == swapChain.nwh) return true;
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
        frameBuffer_ = BGFX_INVALID_HANDLE;
        bgfx::frame();
    }
    width_ = size.width;
    height_ = size.height;
    nativeWindowHandle_ = swapChain.nwh;
    swapChain.formatColor = colourFormat_;
    frameBuffer_ = bgfx::createFrameBuffer(swapChain);
    return bgfx::isValid(frameBuffer_);
}

void OSCRoutingWindow::pollDialogs(UIState& state) {
    using namespace Synesthesia::OSC;
    auto& osc = SynesthesiaOSCIntegration::getInstance();
    if (saveDialog_ && saveDialog_->ready(0)) {
        auto path = saveDialog_->result();
        saveDialog_.reset();
        if (!path.empty()) {
            if (std::filesystem::path(path).extension() != ".json") path += ".json";
            error_ = !saveRoutingConfig(path, osc.getConfig(), message_);
            if (!error_) {
                configurationPath_ = path;
                message_ = "Configuration saved";
            }
            messageRemaining_ = error_ ? 5.4f : 1.4f;
        }
    }
    if (loadDialog_ && loadDialog_->ready(0)) {
        const auto files = loadDialog_->result();
        loadDialog_.reset();
        if (!files.empty()) {
            const auto previous = osc.getConfig();
            auto config = previous;
            error_ = !loadRoutingConfig(files.front(), config, message_);
            if (!error_ && !osc.updateConfig(config)) {
                message_ = "Unable to apply configuration: " + osc.getLastError();
                error_ = true;
                osc.updateConfig(previous);
            }
            if (!error_) {
                state.oscSettings.destinationHost = config.destinationHost;
                state.oscSettings.transmitPort = config.transmitPort;
                state.oscSettings.receivePort = config.receivePort;
                state.oscEnabled = osc.isRunning();
                configurationPath_ = files.front();
                message_ = "Configuration loaded";
            }
            messageRemaining_ = error_ ? 5.4f : 1.4f;
        }
    }
}

void OSCRoutingWindow::renderContents(UIState& state) {
    using namespace Synesthesia::OSC;
    auto& osc = SynesthesiaOSCIntegration::getInstance();
    auto config = osc.getConfig();
    const auto frame = osc.latestFrame();
    const OSCFrameData emptyFrame;
    const auto& currentFrame = frame ? *frame : emptyFrame;
    const auto outputs = buildOutputs(currentFrame, config.routing);
    const auto& catalogue = outputCatalogue();
    const auto stats = osc.getStats();
    const bool running = osc.isRunning();
    OSCOutputSize total;
    std::size_t enabled = 0;
    std::size_t builtInBytes = 0;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto setting = config.routing.find(outputs[i].address);
        if (setting == config.routing.end() || !setting->second.enabled) continue;
        ++enabled;
        const auto size = outputSize(outputs[i]);
        if (!outputs[i].vector) builtInBytes += size.bytes;
        else {
            total.bytes += size.bytes;
            total.packets += size.packets;
            total.largestPacket = std::max(total.largestPacket, size.largestPacket);
        }
    }
    if (builtInBytes > 0) {
        builtInBytes += 16;
        total.bytes += builtInBytes;
        ++total.packets;
        total.largestPacket = std::max(total.largestPacket, builtInBytes);
    }
    const std::string heading = "OSC Routing   " + std::string(running ? "Sending to " : "Stopped  ") +
        config.destinationHost + ":" + std::to_string(config.transmitPort);
    ImGui::SetCursorPosY(9.0f);
#ifdef __APPLE__
    const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorPosX(std::max(80.0f, right - ImGui::CalcTextSize(heading.c_str()).x));
#endif
    ImGui::TextUnformatted(heading.c_str());
    ImGui::SetCursorPos({6.0f, 9.0f + ImGui::GetTextLineHeight() + 8.0f});
    const bool dialogOpen = loadDialog_ || saveDialog_;
    ImGui::BeginDisabled(dialogOpen || state.nativeMenuTracking);
    if (ImGui::Button("Load")) openLoadDialog();
    ImGui::SameLine();
    if (ImGui::Button("Save")) saveDialog_ = std::make_unique<pfd::save_file>("Save OSC Configuration",
        configurationPath_.empty() ? "Synesthesia OSC.json" : configurationPath_, std::vector<std::string>{"JSON configuration", "*.json"});
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        config.routing = defaultRouting();
        osc.setRouting(config.routing);
        message_ = "Default routing restored";
        error_ = false;
        messageRemaining_ = 1.4f;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Format")) ImGui::OpenPopup("Data format");
    if (ImGui::BeginPopup("Data format")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted("Vectors use float32 values. Each message starts with frame ID, source ID and source timestamp in microseconds (int64), followed by offset and total length (int32). Remaining arguments are values. Assemble chunks by source, frame and address; discard incomplete older frames. A zero total marks unavailable data.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Spectrum phases are in radians. Contribution bands run from 20 Hz to 20 kHz in 241 logarithmic steps. Smoothing off selects source values; on selects Synesthesia's existing presented values at the same address. Fixed-stage outputs have no smoothing switch. Frequency outputs identify vector coordinates.");
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Enabled only", &enabledOnly_)) scroll_.reset();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (searchFocusRequested_ && !dialogOpen && !state.nativeMenuTracking) {
        ImGui::SetKeyboardFocusHere();
        searchFocusRequested_ = false;
    }
    if (ImGui::InputTextWithHint("##Search", "Filter outputs or addresses", search_.data(), search_.size())) scroll_.reset();
    ImGui::TextDisabled("%zu enabled  |  %s/update  |  %zu %s  |  max %s  |  %.1f kB/s",
        enabled, formattedBytes(total.bytes).c_str(), total.packets, total.packets == 1 ? "packet" : "packets",
        formattedBytes(total.largestPacket).c_str(), stats.bytesPerSecond / 1000.0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Encoded OSC sizes include addresses, arguments, padding and bundle overhead.\nTransfer rate is measured while sending. Disabled rows show their potential size.\nOptional packets are limited to 1,200 B; the existing built-in bundle is preserved.");
    const auto setupColumns = [] {
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 68);
        ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Smoothing", ImGuiTableColumnFlags_WidthFixed, 86);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 128);
        ImGui::TableSetupColumn("Send", ImGuiTableColumnFlags_WidthFixed, toggleWidth());
    };
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6, 3});
    if (ImGui::BeginTable("Output headings", 5, tableFlags)) {
        setupColumns();
        ImGui::TableHeadersRow();
        ImGui::EndTable();
    }
    std::vector<std::size_t> visible;
    const ImGuiTextFilter filter(search_.data());
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto& route = catalogue[i];
        const auto setting = config.routing.find(route.address);
        if (enabledOnly_ && (setting == config.routing.end() || !setting->second.enabled)) continue;
        const auto searchable = route.group + " " + route.name + " " + route.address;
        if (filter.PassFilter(searchable.c_str())) visible.push_back(i);
    }
    const bool visibleChanged = visible != visibleRows_;
    if (visibleChanged) {
        selection_.Clear();
        visibleRows_ = visible;
    }
    selection_.UserData = &visibleRows_;
    selection_.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* selection, int row) {
        return static_cast<ImGuiID>((*static_cast<const std::vector<std::size_t>*>(selection->UserData))[static_cast<std::size_t>(row)]);
    };
    const float rowHeight = ImGui::GetTextLineHeight() + UI::Input::ScrollMotion::rowPadding;
    const float listHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 listSize(ImGui::GetContentRegionAvail().x, listHeight);
    const float contentHeight = static_cast<float>(visible.size()) * rowHeight;
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        ImGui::IsMouseHoveringRect(origin, {origin.x + listSize.x, origin.y + listSize.y});
    scrolling_.advanceRegion(scroll_, ImGui::GetID("Output list"), hovered, rowHeight,
        std::max(0.0f, contentHeight - listHeight), ImGui::IsWindowContentHoverable(ImGui::GetCurrentWindow()) && !dialogOpen);
    ImGui::SetNextWindowContentSize({0, contentHeight});
    ImGui::SetNextWindowScroll({0, scroll_.position});
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    if (ImGui::BeginChild("Output list", listSize, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        UI::Input::overlayScrollbar(scroll_, false);
        const bool toggleSend = ImGui::IsWindowFocused() && !dialogOpen && !ImGui::GetIO().WantTextInput &&
            selection_.Size > 0 && (ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        auto* selectionIO = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape |
            ImGuiMultiSelectFlags_NoAutoClearOnReselect, selection_.Size, static_cast<int>(visible.size()));
        if (!toggleSend) selection_.ApplyRequests(selectionIO);
        if (ImGui::BeginTable("Outputs", 5, tableFlags | ImGuiTableFlags_RowBg)) {
            setupColumns();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()), rowHeight);
            if (!visibleChanged && selectionIO->RangeSrcItem >= 0 && selectionIO->RangeSrcItem < static_cast<ImGuiSelectionUserData>(visible.size()))
                clipper.IncludeItemByIndex(static_cast<int>(selectionIO->RangeSrcItem));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto index = visible[static_cast<std::size_t>(row)];
                    const auto& route = catalogue[index];
                    const auto& output = outputs[index];
                    const auto found = config.routing.find(route.address);
                    auto setting = found == config.routing.end() ? OSCRouteSettings{} : found->second;
                    ImGui::PushID(route.address.c_str());
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", formattedBytes(outputSize(output).bytes).c_str());
                    ImGui::TableSetColumnIndex(1);
                    const auto prefixLength = std::string_view("/synesthesia/frame/").size();
                    const char* address = route.address.c_str() + prefixLength;
                    ImGui::SetNextItemSelectionUserData(row);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, UI::Input::ScrollMotion::rowPadding});
                    ImGui::Selectable(address, selection_.Contains(static_cast<ImGuiID>(index)),
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0, ImGui::GetTextLineHeight()});
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemFocused()) {
                        const auto* list = ImGui::GetCurrentWindow();
                        const float top = ImGui::GetItemRectMin().y - list->InnerRect.Min.y;
                        const float bottom = ImGui::GetItemRectMax().y - list->InnerRect.Max.y;
                        if (top < 0.0f) scroll_.target = std::max(0.0f, scroll_.position + top);
                        else if (bottom > 0.0f) scroll_.target = scroll_.position + bottom;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s / %s\n%s\n%s%s", route.group.c_str(), route.name.c_str(),
                        route.address.c_str(), route.type.c_str(), route.builtIn ? " / built-in" : " / optional");
                    if (ImGui::BeginPopupContextItem("Address")) {
                        if (ImGui::MenuItem("Copy address")) ImGui::SetClipboardText(route.address.c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::TableSetColumnIndex(2);
                    bool rowChanged = false;
                    if (route.supportsSmoothing) {
                        rowChanged |= slidingToggle("Smoothing", setting.smoothing);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use the existing presented result instead of its source value. Follows Synesthesia's smoothing settings; no additional filter is applied.");
                    } else {
                        ImGui::TextDisabled("-");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This output has a fixed stage or no smoothed equivalent");
                    }
                    ImGui::TableSetColumnIndex(3);
                    if (!frame || !output.available) ImGui::TextDisabled("Unavailable");
                    else if (output.vector) ImGui::Text("%zu values", output.samples.size());
                    else ImGui::TextUnformatted(scalarPreview(output.value).c_str());
                    ImGui::TableSetColumnIndex(4);
                    rowChanged |= slidingToggle("Send", setting.enabled);
                    if (rowChanged) {
                        config.routing[route.address] = setting;
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        if (visibleChanged) selectionIO->RangeSrcReset = true;
        selectionIO = ImGui::EndMultiSelect();
        if (!toggleSend) selection_.ApplyRequests(selectionIO);
        if (toggleSend) {
            bool enable = false;
            for (const auto index : visible) {
                if (!selection_.Contains(static_cast<ImGuiID>(index))) continue;
                const auto setting = config.routing.find(catalogue[index].address);
                if (setting == config.routing.end() || !setting->second.enabled) enable = true;
            }
            for (const auto index : visible) {
                if (!selection_.Contains(static_cast<ImGuiID>(index))) continue;
                config.routing[catalogue[index].address].enabled = enable;
                changed = true;
            }
        }
        if (visible.empty()) ImGui::TextDisabled("No matching outputs");
        UI::Input::overlayScrollbar(scroll_, true);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    if (changed) osc.setRouting(config.routing);
    if (!message_.empty() && messageRemaining_ > 0.0f) {
        const float alpha = std::clamp(messageRemaining_ / 0.4f, 0.0f, 1.0f);
        const float padding = ImGui::GetStyle().WindowPadding.x;
        const float wrapWidth = std::max(1.0f, listSize.x - padding * 2.0f);
        const float height = std::min(listHeight, ImGui::CalcTextSize(message_.c_str(), nullptr, false, wrapWidth).y + padding * 2.0f);
        const ImVec2 top(origin.x, origin.y + listHeight - height);
        ImGui::SetCursorScreenPos(top);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        if (ImGui::BeginChild("Routing status", {listSize.x, height}, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(error_ ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", message_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

void OSCRoutingWindow::renderFrame(UIState& state) {
    if (!isOpen()) return;
    if (window_.shouldClose() && !state.nativeMenuTracking) {
        close();
        return;
    }
    if (!refreshFrameBuffer()) return;
    uiContext_.beginFrame(!state.nativeMenuTracking);
    scrolling_.beginFrame(TrackpadGestures::consume(window_.handle()), !state.nativeMenuTracking);
    messageRemaining_ = std::max(0.0f, messageRemaining_ - ImGui::GetIO().DeltaTime);
    if (messageRemaining_ == 0.0f) message_.clear();
    if (!state.nativeMenuTracking) pollDialogs(state);
    StyleState routingStyle;
    UIStyler::applyCustomStyleWithTheme(ImGui::GetStyle(), routingStyle, state.styleState.currentTheme);
    const bool dark = state.styleState.currentTheme == UITheme::Dark;
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, UIStyler::windowBackgroundColour(state.styleState.currentTheme));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, dark ? ImVec4(0.04f, 0.04f, 0.04f, 1.0f) : ImVec4(0.935f, 0.935f, 0.935f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 3.5f});
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("OSC Routing", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) renderContents(state);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    UIStyler::restoreOriginalStyle(ImGui::GetStyle(), routingStyle);
    uiContext_.endFrame();
    bgfx::setViewFrameBuffer(kClearView, frameBuffer_);
    bgfx::setViewRect(kClearView, 0, 0, static_cast<uint16_t>(width_), static_cast<uint16_t>(height_));
    const auto background = UIStyler::windowBackgroundColour(state.styleState.currentTheme);
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, packRgba8(background.x, background.y, background.z, background.w), 1.0f, 0);
    bgfx::touch(kClearView);
    bgfx::setViewFrameBuffer(kUIView, frameBuffer_);
    uiContext_.renderDrawData();
}

}
