#include "ui/menu/tools_menu.h"

#include <array>
#include <cstdint>

#include "ui/ui.h"
#ifdef ENABLE_OSC
#include "osc/synesthesia_osc_integration.h"
#endif

namespace UI::Menu {

std::span<const Item> toolsMenuItems() {
    static constexpr std::array items{
        Item{.title = "Enable OSC", .command = Command::ToggleOSC, .shortcut = "o",
             .showsSubtitle = true, .shiftShortcut = true, .shortcutTitle = "Enable / disable OSC"}
    };
    return items;
}

State queryToolsCommand(Command command, UIState& state) {
    State result;
#ifdef ENABLE_OSC
    if (command != Command::ToggleOSC) return result;

    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    result.checked = osc.isRunning();
    result.enabled = result.checked ||
        (Synesthesia::OSC::validateOSCDestination(state.oscSettings.destinationHost).valid &&
         state.oscSettings.transmitPort >= 1 && state.oscSettings.transmitPort <= 65535 &&
         state.oscSettings.receivePort >= 1 && state.oscSettings.receivePort <= 65535);
    if (result.checked) {
        const auto config = osc.getConfig();
        result.subtitle = "Send: " + config.destinationHost + ":" + std::to_string(config.transmitPort) +
                          " · Receive: " + std::to_string(config.receivePort);
    } else {
        result.subtitle = osc.getLastError();
    }
#else
    (void)command;
    (void)state;
#endif
    return result;
}

bool executeToolsCommand(Command command, UIState& state) {
    if (!queryToolsCommand(command, state).enabled) return false;

#ifdef ENABLE_OSC
    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    if (osc.isRunning()) {
        state.oscEnabled = false;
        osc.stop();
        UI::Notifications::showStatusPrompt(state.statusPrompt, "OSC Disabled");
    } else {
        auto config = osc.getConfig();
        config.destinationHost = state.oscSettings.destinationHost;
        config.transmitPort = static_cast<std::uint16_t>(state.oscSettings.transmitPort);
        config.receivePort = static_cast<std::uint16_t>(state.oscSettings.receivePort);
        state.oscEnabled = osc.start(config);
        if (state.oscEnabled) {
            const auto activeConfig = osc.getConfig();
            state.oscSettings.destinationHost = activeConfig.destinationHost;
            UI::Notifications::showStatusPrompt(state.statusPrompt,
                "OSC Enabled on Port: " + std::to_string(activeConfig.transmitPort));
        } else {
            UI::Notifications::showStatusPrompt(state.statusPrompt, "OSC could not be enabled: " + osc.getLastError());
        }
    }
    return true;
#else
    return false;
#endif
}

}
