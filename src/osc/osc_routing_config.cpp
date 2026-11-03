#include "osc_config.h"
#include "version.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Synesthesia::OSC {

namespace {

using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void fields(const Json& object, std::initializer_list<const char*> names) {
    require(object.is_object() && object.size() == names.size(), "Unexpected configuration fields");
    for (const auto* name : names) require(object.contains(name), std::string("Missing configuration field: ") + name);
}

const OSCRoute& findRoute(const std::string& address) {
    const auto& catalogue = outputCatalogue();
    const auto route = std::find_if(catalogue.begin(), catalogue.end(), [&](const auto& item) { return item.address == address; });
    require(route != catalogue.end(), "Unsupported OSC output: " + address);
    return *route;
}

void validateSettings(const OSCRoute& route, const OSCRouteSettings& settings) {
    require(!settings.smoothing || route.supportsSmoothing, "Smoothing is unavailable for " + route.address);
}

Json arrayTransportFormat() {
    return {{"layout", "frame-indexed"}, {"spectrum_chunk_size", kSpectrumChunkSize},
        {"band_count", ColourPresentation::contributionBandCount}, {"frame_index_modulus", kFrameIndexModulus}};
}

}

bool saveRoutingConfig(const std::string& path, const OSCConfig& config, std::string& error) {
    std::filesystem::path temporary;
    try {
        require(validateOSCDestination(config.destinationHost).valid, "Invalid OSC destination");
        require(config.transmitPort > 0 && config.receivePort > 0, "Invalid OSC port");
        Json routes = Json::array();
        for (const auto& route : outputCatalogue()) {
            const auto setting = config.routing.find(route.address);
            if (setting == config.routing.end()) continue;
            validateSettings(route, setting->second);
            routes.push_back({{"address", route.address}, {"type", route.type},
                {"smoothing_available", route.supportsSmoothing}, {"enabled", setting->second.enabled},
                {"smoothing", setting->second.smoothing}});
        }
        for (const auto& [address, setting] : config.routing) validateSettings(findRoute(address), setting);
        Json document{{"format", "synesthesia-osc-routing"}, {"format_version", 3},
            {"app_version", SYNESTHESIA_VERSION_STRING},
            {"array_transport", arrayTransportFormat()},
            {"band_magnitude_smoothing", config.smoothBandMagnitudes},
            {"connection", {{"destination", config.destinationHost}, {"transmit_port", config.transmitPort}, {"receive_port", config.receivePort}}},
            {"routes", std::move(routes)}};
        require(!path.empty(), "No configuration file selected");
        temporary = std::filesystem::path(path + "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file << document.dump(2) << '\n';
            file.close();
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), std::filesystem::path(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
#else
        std::filesystem::rename(temporary, path);
#endif
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        error = "Unable to save configuration: " + std::string(exception.what());
        return false;
    }
}

bool loadRoutingConfig(const std::string& path, OSCConfig& config, std::string& error) {
    try {
        require(std::filesystem::file_size(path) <= 2 * 1024 * 1024, "Configuration file is too large");
        std::ifstream file(path, std::ios::binary);
        require(file.is_open(), "Unable to open configuration file");
        const auto document = Json::parse(file);
        require(document.at("format") == "synesthesia-osc-routing", "Unrecognised configuration format");
        require(document.at("format_version").is_number_integer() &&
            (document.at("format_version") == 1 || document.at("format_version") == 2 || document.at("format_version") == 3), "Unsupported configuration format version");
        const bool indexedArrays = document.at("format_version") == 3;
        const bool magnitudeSmoothing = document.at("format_version") != 1;
        if (indexedArrays) {
            fields(document, {"format", "format_version", "app_version", "connection", "routes", "band_magnitude_smoothing", "array_transport"});
            require(document.at("array_transport") == arrayTransportFormat(), "OSC array transport has changed");
        } else if (magnitudeSmoothing) fields(document, {"format", "format_version", "app_version", "connection", "routes", "band_magnitude_smoothing"});
        else fields(document, {"format", "format_version", "app_version", "connection", "routes"});
        require(document.at("app_version").is_string() && !document.at("app_version").get<std::string>().empty(), "Invalid app version");
        const auto& connection = document.at("connection");
        fields(connection, {"destination", "transmit_port", "receive_port"});
        OSCConfig candidate = config;
        candidate.smoothBandMagnitudes = false;
        if (magnitudeSmoothing) {
            require(document.at("band_magnitude_smoothing").is_boolean(), "Invalid band magnitude smoothing switch");
            candidate.smoothBandMagnitudes = document.at("band_magnitude_smoothing").get<bool>();
        }
        candidate.destinationHost = connection.at("destination").get<std::string>();
        const auto destination = validateOSCDestination(candidate.destinationHost);
        require(destination.valid, destination.errorMessage);
        candidate.destinationHost = destination.canonicalHost;
        const auto readPort = [&](const char* name) {
            const auto& value = connection.at(name);
            require(value.is_number_integer() && value >= 1 && value <= 65535, "Invalid OSC port");
            return value.get<uint16_t>();
        };
        candidate.transmitPort = readPort("transmit_port");
        candidate.receivePort = readPort("receive_port");
        const auto& routes = document.at("routes");
        require(routes.is_array() && routes.size() <= outputCatalogue().size(), "Invalid OSC route list");
        candidate.routing.clear();
        for (const auto& item : routes) {
            fields(item, {"address", "type", "smoothing_available", "enabled", "smoothing"});
            const auto address = item.at("address").get<std::string>();
            const auto& route = findRoute(address);
            require(item.at("type") == route.type, "Output type has changed: " + address);
            require(item.at("smoothing_available").is_boolean() && item.at("smoothing_available") == route.supportsSmoothing, "Output processing has changed: " + address);
            require(item.at("enabled").is_boolean() && item.at("smoothing").is_boolean(), "Invalid output switch: " + address);
            OSCRouteSettings setting{item.at("enabled").get<bool>(), item.at("smoothing").get<bool>()};
            require(indexedArrays || !setting.enabled || route.type != "float32[]",
                "This configuration enables the previous OSC array format. Recreate its array routes for the current frame-indexed format.");
            validateSettings(route, setting);
            require(candidate.routing.emplace(address, setting).second, "Duplicate OSC output: " + address);
        }
        config = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = "Unable to load configuration: " + std::string(exception.what());
        return false;
    }
}

}
