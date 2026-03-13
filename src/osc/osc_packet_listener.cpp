#include "osc_packet_listener.h"

#include "osc_addresses.h"

#include "osc/OscReceivedElements.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace Synesthesia::OSC {

namespace {

bool parseBoolean(const osc::ReceivedMessageArgument& argument) {
    if (argument.IsBool()) {
        return argument.AsBoolUnchecked();
    }
    if (argument.IsInt32()) {
        return argument.AsInt32Unchecked() != 0;
    }
    if (argument.IsFloat()) {
        return argument.AsFloatUnchecked() != 0.0f;
    }
    throw osc::WrongArgumentTypeException();
}

float parseFloat(const osc::ReceivedMessageArgument& argument) {
    if (argument.IsFloat()) {
        return argument.AsFloatUnchecked();
    }
    if (argument.IsInt32()) {
        return static_cast<float>(argument.AsInt32Unchecked());
    }
    if (argument.IsDouble()) {
        return static_cast<float>(argument.AsDoubleUnchecked());
    }
    throw osc::WrongArgumentTypeException();
}

std::string toLowerCopy(const char* value) {
    std::string normalised = value;
    std::transform(normalised.begin(), normalised.end(), normalised.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalised;
}

ColourMapper::ColourSpace parseColourSpace(const osc::ReceivedMessageArgument& argument) {
    if (argument.IsString()) {
        const std::string value = toLowerCopy(argument.AsStringUnchecked());
        if (value == "rec2020" || value == "rec.2020" || value == "0") {
            return ColourMapper::ColourSpace::Rec2020;
        }
        if (value == "displayp3" || value == "display p3" || value == "p3" || value == "1") {
            return ColourMapper::ColourSpace::DisplayP3;
        }
        if (value == "srgb" || value == "s-rgb" || value == "2") {
            return ColourMapper::ColourSpace::SRGB;
        }
    }
    if (argument.IsInt32()) {
        switch (argument.AsInt32Unchecked()) {
            case 0:
                return ColourMapper::ColourSpace::Rec2020;
            case 1:
                return ColourMapper::ColourSpace::DisplayP3;
            case 2:
                return ColourMapper::ColourSpace::SRGB;
            default:
                break;
        }
    }
    throw osc::WrongArgumentTypeException();
}

}

OSCPacketListener::OSCPacketListener(OSCCommandQueue& commandQueue, std::atomic<uint64_t>& receivedMessages)
    : commandQueue_(commandQueue), receivedMessages_(receivedMessages) {}

void OSCPacketListener::ProcessMessage(const osc::ReceivedMessage& message, const IpEndpointName& remoteEndpoint) {
    (void)remoteEndpoint;

    receivedMessages_.fetch_add(1, std::memory_order_relaxed);

    try {
        if (std::strcmp(message.AddressPattern(), kControlSmoothingAddress) == 0) {
            osc::ReceivedMessage::const_iterator argument = message.ArgumentsBegin();
            if (argument == message.ArgumentsEnd()) {
                throw osc::MissingArgumentException();
            }

            const bool enabled = parseBoolean(*argument++);
            if (argument == message.ArgumentsEnd()) {
                throw osc::MissingArgumentException();
            }

            const float speed = std::clamp(parseFloat(*argument++), 0.0f, 1.0f);
            if (argument != message.ArgumentsEnd()) {
                throw osc::ExcessArgumentException();
            }

            commandQueue_.push(SetSmoothingEnabledCommand{enabled});
            commandQueue_.push(SetColourSmoothingSpeedCommand{speed});
            return;
        }

        if (std::strcmp(message.AddressPattern(), kControlSpectrumSmoothingAddress) == 0) {
            osc::ReceivedMessage::const_iterator argument = message.ArgumentsBegin();
            if (argument == message.ArgumentsEnd()) {
                throw osc::MissingArgumentException();
            }

            const float amount = std::clamp(parseFloat(*argument++), 0.0f, 1.0f);
            if (argument != message.ArgumentsEnd()) {
                throw osc::ExcessArgumentException();
            }

            commandQueue_.push(SetSpectrumSmoothingCommand{amount});
            return;
        }

        if (std::strcmp(message.AddressPattern(), kControlColourSpaceAddress) == 0) {
            osc::ReceivedMessage::const_iterator argument = message.ArgumentsBegin();
            if (argument == message.ArgumentsEnd()) {
                throw osc::MissingArgumentException();
            }

            const ColourMapper::ColourSpace colourSpace = parseColourSpace(*argument++);
            if (argument != message.ArgumentsEnd()) {
                throw osc::ExcessArgumentException();
            }

            commandQueue_.push(SetColourSpaceCommand{colourSpace});
            return;
        }

        if (std::strcmp(message.AddressPattern(), kControlGamutMappingAddress) == 0) {
            osc::ReceivedMessage::const_iterator argument = message.ArgumentsBegin();
            if (argument == message.ArgumentsEnd()) {
                throw osc::MissingArgumentException();
            }

            const bool enabled = parseBoolean(*argument++);
            if (argument != message.ArgumentsEnd()) {
                throw osc::ExcessArgumentException();
            }

            commandQueue_.push(SetGamutMappingCommand{enabled});
        }
    } catch (const osc::Exception&) {
    }
}

}
