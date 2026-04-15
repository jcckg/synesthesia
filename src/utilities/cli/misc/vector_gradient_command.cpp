#include "misc/vector_gradient_command.h"

#include <algorithm>
#include <cctype>
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
#include "resyne/encoding/formats/exporter.h"

namespace fs = std::filesystem;

namespace CLI::Misc {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct LabSample {
    float L = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
    double timestamp = 0.0;
};

enum class VectorTrack {
    Auto,
    Smoothed,
    Analysis
};

bool parseVectorTrack(const std::string& value, VectorTrack& track) {
    const std::string lowered = toLower(value);
    if (lowered == "auto") {
        track = VectorTrack::Auto;
        return true;
    }
    if (lowered == "smoothed") {
        track = VectorTrack::Smoothed;
        return true;
    }
    if (lowered == "analysis") {
        track = VectorTrack::Analysis;
        return true;
    }
    return false;
}

VectorTrack resolveVectorTrack(const RSYNPresentationData& presentation,
                              const VectorTrack requestedTrack) {
    if (requestedTrack != VectorTrack::Auto) {
        return requestedTrack;
    }

    return presentation.settings.smoothingEnabled
        ? VectorTrack::Smoothed
        : VectorTrack::Analysis;
}

bool isRsynPath(const fs::path& path) {
    return toLower(path.extension().string()) == ".rsyn";
}

bool collectLabSamples(const RSYNPresentationData& presentation,
                       const VectorTrack requestedTrack,
                       std::vector<LabSample>& samples) {
    samples.clear();
    samples.reserve(presentation.frames.size());

    const VectorTrack track = resolveVectorTrack(presentation, requestedTrack);
    for (const auto& frame : presentation.frames) {
        LabSample sample{};
        sample.timestamp = frame.timestamp;
        if (track == VectorTrack::Smoothed) {
            sample.L = frame.smoothedLab[0];
            sample.a = frame.smoothedLab[1];
            sample.b = frame.smoothedLab[2];
        } else {
            sample.L = frame.analysis.L;
            sample.a = frame.analysis.a;
            sample.b = frame.analysis.b_comp;
        }
        samples.push_back(sample);
    }

    return !samples.empty();
}

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
                           const float L,
                           const float a,
                           const float bValue,
                           const RSYNPresentationSettings& settings) {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourCore::LabtoRGB(
        L,
        a,
        bValue,
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

void buildExactBands(const std::vector<LabSample>& samples,
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

        const float L = std::lerp(samples[sampleIndex0].L, samples[sampleIndex1].L, fraction);
        const float a = std::lerp(samples[sampleIndex0].a, samples[sampleIndex1].a, fraction);
        const float bValue = std::lerp(samples[sampleIndex0].b, samples[sampleIndex1].b, fraction);

        const ExactBand nextBand = convertLabToBand(pixelIndex, 1, L, a, bValue, settings);
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

int defaultLosslessWidth(const std::vector<LabSample>& samples) {
    return std::max(1, static_cast<int>(samples.size()));
}

}

int runVectorGradientCommand(const Arguments& args) {
    if (args.inputDir.empty()) {
        std::cerr << "Error: --misc vector-gradient requires --input <file.rsyn>\n";
        return 1;
    }
    if (args.outputDir.empty()) {
        std::cerr << "Error: --misc vector-gradient requires --output <file.svg>\n";
        return 1;
    }

    const fs::path inputPath(args.inputDir);
    if (!isRsynPath(inputPath)) {
        std::cerr << "Error: vector-gradient currently requires an .rsyn input\n";
        return 1;
    }

    VectorTrack track = VectorTrack::Auto;
    if (!parseVectorTrack(args.miscTrack, track)) {
        std::cerr << "Error: --misc-track must be 'auto', 'smoothed', or 'analysis'\n";
        return 1;
    }

    AudioMetadata metadata{};
    if (!SequenceExporter::loadFromRsynShell(inputPath.string(), metadata)) {
        std::cerr << "Error: failed to load RSYN metadata/presentation\n";
        return 1;
    }
    if (metadata.presentationData == nullptr || metadata.presentationData->frames.empty()) {
        std::cerr << "Error: RSYN file has no presentation frames\n";
        return 1;
    }

    std::vector<LabSample> samples;
    if (!collectLabSamples(*metadata.presentationData, track, samples)) {
        std::cerr << "Error: could not collect presentation samples\n";
        return 1;
    }

    const int width = args.gradientWidth > 0 ? args.gradientWidth : defaultLosslessWidth(samples);
    const int height = args.gradientHeight > 0 ? args.gradientHeight : 800;

    std::vector<ExactBand> bands;
    buildExactBands(samples, metadata.presentationData->settings, width, bands);
    if (bands.empty()) {
        std::cerr << "Error: no exact vector bands generated\n";
        return 1;
    }

    const fs::path outputPath(args.outputDir);
    if (!writeSvgGradient(outputPath, bands, width, height, inputPath.stem().string())) {
        std::cerr << "Error: failed to write SVG output\n";
        return 1;
    }

    std::cout << "Exported vector gradient: " << outputPath << '\n';
    return 0;
}

}
