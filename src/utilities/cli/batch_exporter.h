#pragma once

#include <string>

namespace CLI {

class BatchExporter {
public:
    static int run(const std::string& inputDir,
                   const std::string& outputDir,
                   bool copyAudio,
                   int width = 0,
                   int height = 0,
                   bool trueSize = false,
                   bool noSmoothing = false,
                   bool noMelWeighting = false,
                   int numWorkers = 1);
};

}
