#include "misc/misc_commands.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "misc/gltf_gradient_command.h"
#include "misc/vector_gradient_command.h"

namespace CLI {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}

int MiscCommands::run(const Arguments& args) {
    const std::string command = toLower(args.miscCommand);
    if (command == "gltf-gradient") {
        return Misc::runGltfGradientCommand(args);
    }
    if (command == "vector-gradient") {
        return Misc::runVectorGradientCommand(args);
    }

    std::cerr << "Error: unknown misc command '" << args.miscCommand << "'\n";
    return 1;
}

}
