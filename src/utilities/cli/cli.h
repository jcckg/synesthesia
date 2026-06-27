#pragma once

#include <string>

namespace CLI {

struct Arguments {
    bool headless = false;
    bool enableOSC = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string audioDevice;
    std::string oscDestination = "127.0.0.1";
    int oscSendPort = 7000;
    int oscReceivePort = 7001;

    bool exportGradients = false;
    bool exportRsyn = false;
    std::string inputDir;
    std::string outputDir;
    bool copyAudio = false;
    bool trueSize = false;
    int numWorkers = 1;
    int gradientWidth  = 0;
    int gradientHeight = 0;
    int analysisHop = 1024;
    int rsynCompressionLevel = 0;
    double storageCutoffGb = 0.0;
    double skipLargeSeconds = 0.0;
    bool randomiseOrder = false;
    bool disableSmoothing = false;

    bool runMisc = false;
    std::string miscCommand;
    std::string miscTrack = "auto";
    bool normaliseHeight = false;
    bool normaliseLength = false;

    static Arguments parseCommandLine(int argc, char* argv[]);
    static void printHelp();
    static void printVersion();
};

}
