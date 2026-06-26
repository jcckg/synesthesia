#pragma once

#include <string>

namespace CLI {

class BatchExporter {
public:
    static int runGradients(const std::string& inputDir,
                            const std::string& outputDir,
                            bool copyAudio,
                            int width = 0,
                            int height = 0,
                            bool trueSize = false,
                            int numWorkers = 1,
                            int analysisHop = 1024,
                            bool disableSmoothing = false);

    static int runRsyn(const std::string& inputDir,
                       const std::string& outputDir,
                       int numWorkers = 1,
                       int analysisHop = 1024);
};

}
