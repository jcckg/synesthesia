#if defined(__APPLE__) || defined(__linux__)
#include "cli.h"
#include "headless.h"
#include "batch_exporter.h"
#endif

#include <cstdint>
#include <iostream>

int app_main(int argc, char** argv);

int main(int argc, char* argv[]) {
#if defined(__APPLE__) || defined(__linux__)
    CLI::Arguments args = CLI::Arguments::parseCommandLine(argc, argv);

    if (args.showHelp) {
        CLI::Arguments::printHelp();
        return 0;
    }

    if (args.showVersion) {
        CLI::Arguments::printVersion();
        return 0;
    }

    if (args.exportGradients) {
        if (args.inputDir.empty()) {
            std::cerr << "Error: --export-gradients requires --input <dir>\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
        if (args.outputDir.empty()) {
            std::cerr << "Error: --export-gradients requires --output <dir>\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
        return CLI::BatchExporter::run(args.inputDir, args.outputDir, args.copyAudio,
                                       args.gradientWidth, args.gradientHeight, args.trueSize,
                                       args.noSmoothing, args.noMelWeighting, args.numWorkers);
    }

    if (args.headless) {
        try {
            CLI::HeadlessInterface interface;
            interface.run(
                args.enableOSC,
                args.audioDevice,
                static_cast<uint16_t>(args.oscSendPort),
                static_cast<uint16_t>(args.oscReceivePort)
            );
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Error in headless mode: " << e.what() << std::endl;
            return 1;
        }
    }
#endif

    return app_main(argc, argv);
}
