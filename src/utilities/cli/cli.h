#pragma once

#include <string>

namespace CLI {

struct Arguments {
    bool headless = false;
    bool enableAPI = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string audioDevice;
    
    static Arguments parseCommandLine(int argc, char* argv[]);
    static void printHelp();
    static void printVersion();
};

}