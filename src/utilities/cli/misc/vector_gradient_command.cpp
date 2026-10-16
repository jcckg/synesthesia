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
#include "resyne/encoding/formats/rsyn_presentation.h"
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

bool sameBandColour(const ExactBand& left, const ExactBand& right) {
    return std::abs(left.r - right.r) <= 1e-7f &&
        std::abs(left.g - right.g) <= 1e-7f &&
        std::abs(left.b - right.b) <= 1e-7f;
}

void buildExactBands(const std::vector<PresentationSample>& samples,
                     const LoadedPresentation& loaded, int width,
                     std::vector<ExactBand>& bands, bool analyserColumns) {
    bands.clear();
    if (samples.empty() || width <= 0) return;
    const auto& presentation = *loaded.metadata.presentationData;
    const auto track = RSYNPresentation::colourTrack(presentation, loaded.resolvedTrack == PresentationTrack::Smoothed);
    const double duration = RSYNPresentation::durationSeconds(loaded.metadata, track->endTime());
    for (int x = 0; x < width; ++x) {
        auto rgb = analyserColumns
            ? track->rgbAt(samples[x].timestamp, ColourCore::ColourSpace::SRGB, presentation.settings.applyGamutMapping)
            : track->averageRGB(duration * x / width, duration * (x + 1) / width,
                                ColourCore::ColourSpace::SRGB, presentation.settings.applyGamutMapping);
        ColourPresentation::applyOutputPrecision(rgb.r, rgb.g, rgb.b);
        const ExactBand nextBand{x, 1, rgb.r, rgb.g, rgb.b};
        if (!bands.empty() && sameBandColour(bands.back(), nextBand) && bands.back().x + bands.back().width == x)
            ++bands.back().width;
        else bands.push_back(nextBand);
    }
}

// https://www.w3.org/TR/xml/#syntax
std::string xmlText(const std::string& input) {
    std::string escaped;
    for (char value : input) {
        if (value == '&') escaped += "&amp;";
        else if (value == '<') escaped += "&lt;";
        else if (value == '>') escaped += "&gt;";
        else escaped += value;
    }
    return escaped;
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
    stream << "  <title>" << xmlText(title) << "</title>\n";
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
    buildExactBands(samples, loaded, width, bands, args.gradientWidth <= 0);
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
