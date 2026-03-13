#include "cli.h"

#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include "version.h"

namespace CLI {

Arguments Arguments::parseCommandLine(int argc, char* argv[]) {
    Arguments args;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "-h") == 0) {
            args.headless = true;
        }
        else if (strcmp(argv[i], "--enable-osc") == 0) {
            args.enableOSC = true;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            args.showHelp = true;
        }
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            args.showVersion = true;
        }
        else if (strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) {
            if (i + 1 < argc) {
                args.audioDevice = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--osc-destination") == 0) {
            if (i + 1 < argc) {
                args.oscDestination = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--osc-send-port") == 0) {
            if (i + 1 < argc) {
                args.oscSendPort = std::atoi(argv[++i]);
                args.oscSendPort = std::clamp(args.oscSendPort, 1, 65535);
            }
        }
        else if (strcmp(argv[i], "--osc-receive-port") == 0) {
            if (i + 1 < argc) {
                args.oscReceivePort = std::atoi(argv[++i]);
                args.oscReceivePort = std::clamp(args.oscReceivePort, 1, 65535);
            }
        }
        else if (strcmp(argv[i], "--export-gradients") == 0) {
            args.exportGradients = true;
        }
        else if (strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "-i") == 0) {
            if (i + 1 < argc) {
                args.inputDir = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                args.outputDir = argv[++i];
            }
        }
        else if (strcmp(argv[i], "--copy-audio") == 0) {
            args.copyAudio = true;
        }
        else if (strcmp(argv[i], "--true-size") == 0) {
            args.trueSize = true;
        }
        else if (strcmp(argv[i], "--no-smoothing") == 0) {
            args.noSmoothing = true;
        }
        else if (strcmp(argv[i], "--no-mel-weighting") == 0) {
            args.noMelWeighting = true;
        }
        else if (strcmp(argv[i], "--num-workers") == 0) {
            if (i + 1 < argc) {
                args.numWorkers = std::atoi(argv[++i]);
                if (args.numWorkers < 1) {
                    args.numWorkers = 1;
                }
            }
        }
        else if (strcmp(argv[i], "--width") == 0) {
            if (i + 1 < argc) {
                args.gradientWidth = std::atoi(argv[++i]);
            }
        }
        else if (strcmp(argv[i], "--height") == 0) {
            if (i + 1 < argc) {
                args.gradientHeight = std::atoi(argv[++i]);
            }
        }
        else {
            std::cerr << "Unknown argument: " << argv[i] << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
        }
    }
    
    return args;
}

void Arguments::printHelp() {
    std::cout << "Synesthesia\n\n";
    std::cout << "Usage: Synesthesia [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --headless, -h          Run in headless mode (no GUI)\n";
    std::cout << "  --enable-osc            Start OSC transport automatically\n";
    std::cout << "  --device, -d <name>     Use specific audio device\n";
    std::cout << "  --osc-destination <ip>  OSC loopback/private IPv4 destination (default: 127.0.0.1)\n";
    std::cout << "  --osc-send-port <port>  OSC destination port (default: 7000)\n";
    std::cout << "  --osc-receive-port <p>  OSC receive port (default: 7001)\n";
    std::cout << "  --version, -v           Show version information\n";
    std::cout << "  --help                  Show this help message\n\n";
    std::cout << "Batch export:\n";
    std::cout << "  --export-gradients      Batch export gradient images from audio files\n";
    std::cout << "  --input, -i <dir>       Input directory to scan for audio files\n";
    std::cout << "  --output, -o <dir>      Output directory for gradient PNG images\n";
    std::cout << "  --copy-audio            Also copy audio files alongside gradients\n";
    std::cout << "                          (creates 'gradients/' and 'audio/' subdirectories)\n";
    std::cout << "  --true-size             Use exact analyser frame count as image width\n";
    std::cout << "                          (no temporal interpolation in export)\n";
    std::cout << "  --no-smoothing          Disable analyser critical-band smoothing during export\n";
    std::cout << "  --no-mel-weighting      Disable analyser mel weighting during export\n";
    std::cout << "  --num-workers <n>       Number of worker threads for batch export (default: 1)\n";
    std::cout << "  --width <px>            Force gradient width in pixels\n";
    std::cout << "                          (default: 20px per second of audio)\n";
    std::cout << "  --height <px>           Force gradient height in pixels (default: 800)\n\n";
    std::cout << "Supported audio formats: .wav, .flac, .mp3, .ogg\n\n";
    std::cout << "Examples:\n";
    std::cout << "  Synesthesia --export-gradients -i ~/Music -o ~/GradientExport\n";
    std::cout << "  Synesthesia --export-gradients -i ~/Music -o ~/Export --copy-audio\n\n";
}

void Arguments::printVersion() {
    std::cout << "Synesthesia " << SYNESTHESIA_VERSION_STRING << std::endl;
#ifdef USE_NEON_OPTIMISATIONS
    std::cout << "ARM NEON optimisations: Enabled" << std::endl;
#endif
#ifdef USE_SSE_OPTIMISATIONS
    std::cout << "SSE/AVX optimisations: Enabled" << std::endl;
#endif
#ifdef ENABLE_OSC
    std::cout << "OSC Transport: Enabled" << std::endl;
#else
    std::cout << "OSC Transport: Disabled" << std::endl;
#endif
}

}
