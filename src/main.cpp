#if defined(__APPLE__) || defined(__linux__)
#include "cli.h"
#include "headless.h"
#include "batch_exporter.h"
#include "misc/misc_commands.h"
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

int app_main(int argc, char** argv);

namespace {

int reportStartupFailure(const std::string& message) {
    std::cerr << message << std::endl;
#if defined(_WIN32)
    MessageBoxA(nullptr, message.c_str(), "Synesthesia failed to start", MB_OK | MB_ICONERROR);
#endif
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
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
                                           args.gradientWidth, args.gradientHeight, args.gradientFormat, args.writeConditionSidecar, args.trueSize,
                                           args.numWorkers, args.analysisHop, args.disableSmoothing);
        }

        if (args.runMisc) {
            return CLI::MiscCommands::run(args);
        }

        if (args.headless) {
            try {
                CLI::HeadlessInterface interface;
                interface.run(
                    args.enableOSC,
                    args.audioDevice,
                    args.oscDestination,
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
    } catch (const std::exception& e) {
        return reportStartupFailure(std::string("Unhandled startup error: ") + e.what());
    } catch (...) {
        return reportStartupFailure("Unhandled startup error: unknown exception");
    }
}
