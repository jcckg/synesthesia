#pragma once

#include <string>

namespace CLI {

struct Arguments {
    bool headless = false;
    bool enableAPI = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string audioDevice;

    bool exportGradients = false;
    std::string inputDir;
    std::string outputDir;
    bool copyAudio = false;
    bool trueSize = false;
    bool noSmoothing = false;
    bool noMelWeighting = false;
    int numWorkers = 1;
    int gradientWidth  = 0;
    int gradientHeight = 0;

    static Arguments parseCommandLine(int argc, char* argv[]);
    static void printHelp();
    static void printVersion();
};

}
