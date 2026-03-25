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
                   const std::string& gradientFormat = "png",
                   bool writeLabSidecar = false,
                   bool trueSize = false,
                   int numWorkers = 1,
                   int analysisHop = 1024);
};

}
