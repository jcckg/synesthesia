#include "misc/vector_gradient_command.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "colour/colour_core.h"
#include "colour/colour_presentation.h"
#include "misc/presentation_export_utils.h"

namespace fs = std::filesystem;

namespace CLI::Misc {

namespace {

std::string rgbPercentString(const float r, const float g, const float b) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6)
           << "rgb("
           << std::clamp(r, 0.0f, 1.0f) * 100.0f << "% "
           << std::clamp(g, 0.0f, 1.0f) * 100.0f << "% "
           << std::clamp(b, 0.0f, 1.0f) * 100.0f << "%)";
    return stream.str();
}

struct ExactBand {
    int x = 0;
    int width = 1;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

ExactBand convertLabToBand(const int x,
                           const int width,
                           const float labL,
                           const float labA,
                           const float labB,
                           const RSYNPresentationSettings& settings) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourCore::LabtoRGB(
        labL,
        labA,
        labB,
        r,
        g,
        b,
        settings.colourSpace,
        settings.applyGamutMapping);
    ColourPresentation::applyOutputPrecision(r, g, b);

    return ExactBand{
        .x = x,
        .width = width,
        .r = r,
        .g = g,
        .b = b
    };
}

bool sameBandColour(const ExactBand& left, const ExactBand& right) {
    return std::abs(left.r - right.r) <= 1e-7f &&
        std::abs(left.g - right.g) <= 1e-7f &&
        std::abs(left.b - right.b) <= 1e-7f;
}

void buildExactBands(const std::vector<PresentationSample>& samples,
                     const RSYNPresentationSettings& settings,
                     const int width,
                     std::vector<ExactBand>& bands) {
    bands.clear();
    if (samples.empty() || width <= 0) {
        return;
    }

    const std::size_t lastIndex = samples.size() - 1;
    for (int pixelIndex = 0; pixelIndex < width; ++pixelIndex) {
        const float pixelNormalised = width > 1
            ? static_cast<float>(pixelIndex) / static_cast<float>(width - 1)
            : 0.0f;
        const float samplePosition = pixelNormalised * static_cast<float>(lastIndex);
        const std::size_t sampleIndex0 = std::min(static_cast<std::size_t>(samplePosition), lastIndex);
        const std::size_t sampleIndex1 = std::min(sampleIndex0 + 1, lastIndex);
        const float fraction = samplePosition - static_cast<float>(sampleIndex0);

        const float labL = std::lerp(samples[sampleIndex0].labL, samples[sampleIndex1].labL, fraction);
        const float labA = std::lerp(samples[sampleIndex0].labA, samples[sampleIndex1].labA, fraction);
        const float labB = std::lerp(samples[sampleIndex0].labB, samples[sampleIndex1].labB, fraction);

        const ExactBand nextBand = convertLabToBand(pixelIndex, 1, labL, labA, labB, settings);
        if (!bands.empty() && sameBandColour(bands.back(), nextBand) &&
            bands.back().x + bands.back().width == nextBand.x) {
            bands.back().width += 1;
        } else {
            bands.push_back(nextBand);
        }
    }
}

bool writeSvgGradient(const fs::path& outputPath,
                      const std::vector<ExactBand>& bands,
                      const int width,
                      const int height,
                      const std::string& title) {
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream) {
        return false;
    }

    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
           << "\" height=\"" << height
           << "\" viewBox=\"0 0 " << width << ' ' << height
           << "\" shape-rendering=\"crispEdges\">\n";
    stream << "  <title>" << title << "</title>\n";
    for (const auto& band : bands) {
        stream << "  <rect x=\"" << band.x << "\" y=\"0\" width=\"" << band.width
               << "\" height=\"" << height
               << "\" fill=\"" << rgbPercentString(band.r, band.g, band.b)
               << "\"/>\n";
    }
    stream << "</svg>\n";
    return stream.good();
}

int defaultLosslessWidth(const std::vector<PresentationSample>& samples) {
    return std::max(1, static_cast<int>(samples.size()));
}

}

int runVectorGradientCommand(const Arguments& args) {
    if (args.inputDir.empty()) {
        std::cerr << "Error: --misc vector-gradient requires --input <file>\n";
        return 1;
    }
    if (args.outputDir.empty()) {
        std::cerr << "Error: --misc vector-gradient requires --output <file.svg>\n";
        return 1;
    }

    LoadedPresentation loaded;
    std::string errorMessage;
    if (!loadPresentationForInput(args, loaded, errorMessage)) {
        std::cerr << "Error: " << errorMessage << '\n';
        return 1;
    }

    std::vector<PresentationSample> samples;
    if (!collectPresentationSamples(loaded, samples)) {
        std::cerr << "Error: could not collect presentation samples\n";
        return 1;
    }

    const int width = args.gradientWidth > 0 ? args.gradientWidth : defaultLosslessWidth(samples);
    const int height = args.gradientHeight > 0 ? args.gradientHeight : 800;

    std::vector<ExactBand> bands;
    buildExactBands(samples, loaded.metadata.presentationData->settings, width, bands);
    if (bands.empty()) {
        std::cerr << "Error: no exact vector bands generated\n";
        return 1;
    }

    const fs::path outputPath(args.outputDir);
    if (!writeSvgGradient(outputPath, bands, width, height, loaded.inputPath.stem().string())) {
        std::cerr << "Error: failed to write SVG output\n";
        return 1;
    }

    std::cout << "Exported vector gradient: " << outputPath << '\n';
    return 0;
}

}
