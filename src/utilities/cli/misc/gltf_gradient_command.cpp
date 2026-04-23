#include "misc/gltf_gradient_command.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "misc/presentation_export_utils.h"
#include "tiny_gltf.h"

namespace fs = std::filesystem;

namespace CLI::Misc {

namespace {

constexpr float kMinimumLoudnessDb = -70.0f;
constexpr float kMaximumLoudnessDb = 0.0f;
constexpr float kHalfRibbonDepth = 0.5f;

struct HeightProfile {
    bool normaliseHeight = false;
    bool normaliseLength = false;
};

struct ColumnSample {
    PresentationSample sample{};
    float x = 0.0f;
    float height = 0.0f;
};

struct MeshPayload {
    std::vector<float> positions;
    std::vector<float> renderColours;
    std::vector<float> nativeDisplayColours;
    std::vector<float> labValues;
    std::vector<float> loudnessValues;
    std::vector<std::uint32_t> indices;
    std::array<double, 3> minPosition{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()
    };
    std::array<double, 3> maxPosition{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()
    };
};

struct BufferSpan {
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
};

tinygltf::Value makeStringArrayValue(const std::initializer_list<std::string> values) {
    tinygltf::Value::Array array;
    array.reserve(values.size());
    for (const auto& value : values) {
        array.emplace_back(value);
    }
    return tinygltf::Value(std::move(array));
}

tinygltf::Value makeNumberArrayValue(const std::initializer_list<double> values) {
    tinygltf::Value::Array array;
    array.reserve(values.size());
    for (const double value : values) {
        array.emplace_back(value);
    }
    return tinygltf::Value(std::move(array));
}

float absoluteLoudnessHeight(const float loudnessDb) {
    return std::clamp(loudnessDb, kMinimumLoudnessDb, kMaximumLoudnessDb) - kMinimumLoudnessDb;
}

float exportedHeight(const PresentationSample& sample,
                     const HeightProfile& profile) {
    if (profile.normaliseHeight) {
        return std::clamp(sample.loudnessNormalised, 0.0f, 1.0f);
    }

    return absoluteLoudnessHeight(sample.loudnessDb);
}

double resolvedTrackLengthSeconds(const AudioMetadata& metadata,
                                  const std::vector<PresentationSample>& samples) {
    double totalLength = std::max(metadata.durationSeconds, 0.0);
    for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
        const double durationSeconds = resolveFrameDurationSeconds(metadata, samples, sampleIndex);
        totalLength = std::max(totalLength, samples[sampleIndex].timestamp + std::max(durationSeconds, 1e-6));
    }

    if (!(std::isfinite(totalLength) && totalLength > 0.0)) {
        return 1.0;
    }

    return totalLength;
}

double grooveScoreForLength(const std::vector<PresentationSample>& samples) {
    if (samples.size() < 2U) {
        return 0.5;
    }

    double minLoudness = 1.0;
    double maxLoudness = 0.0;
    double sum = 0.0;
    double sumSquares = 0.0;
    double meanDelta = 0.0;

    double previous = std::clamp(static_cast<double>(samples.front().loudnessNormalised), 0.0, 1.0);
    for (const auto& sample : samples) {
        const double current = std::clamp(static_cast<double>(sample.loudnessNormalised), 0.0, 1.0);
        minLoudness = std::min(minLoudness, current);
        maxLoudness = std::max(maxLoudness, current);
        sum += current;
        sumSquares += current * current;
        meanDelta += std::abs(current - previous);
        previous = current;
    }

    const double count = static_cast<double>(samples.size());
    const double mean = sum / count;
    const double variance = std::max(0.0, (sumSquares / count) - (mean * mean));
    const double standardDeviation = std::sqrt(variance);
    const double averageDelta = meanDelta / static_cast<double>(samples.size() - 1U);
    const double range = maxLoudness - minLoudness;

    return std::clamp(
        range * 0.45 +
        averageDelta * 3.5 +
        standardDeviation * 0.75,
        0.0,
        1.0);
}

double exportedLengthExtent(const std::vector<PresentationSample>& samples,
                            const double totalLength,
                            const HeightProfile& profile) {
    if (!profile.normaliseLength) {
        return totalLength;
    }

    const double grooveScore = grooveScoreForLength(samples);
    const double baseExtent = 1.0 + 2.0 * std::log10(1.0 + totalLength * 2.0);
    const double dynamicExtent = baseExtent * std::lerp(0.85, 1.25, grooveScore);
    return std::clamp(dynamicExtent, 1.0, totalLength);
}

void appendVertex(MeshPayload& payload,
                  const PresentationSample& sample,
                  const float x,
                  const float y,
                  const float z) {
    payload.positions.push_back(x);
    payload.positions.push_back(y);
    payload.positions.push_back(z);

    payload.renderColours.push_back(sample.linearRenderR);
    payload.renderColours.push_back(sample.linearRenderG);
    payload.renderColours.push_back(sample.linearRenderB);

    payload.nativeDisplayColours.push_back(sample.displayR);
    payload.nativeDisplayColours.push_back(sample.displayG);
    payload.nativeDisplayColours.push_back(sample.displayB);

    payload.labValues.push_back(sample.labL);
    payload.labValues.push_back(sample.labA);
    payload.labValues.push_back(sample.labB);

    payload.loudnessValues.push_back(sample.loudnessDb);
    payload.loudnessValues.push_back(sample.loudnessNormalised);
    payload.loudnessValues.push_back(y);

    payload.minPosition[0] = std::min(payload.minPosition[0], static_cast<double>(x));
    payload.minPosition[1] = std::min(payload.minPosition[1], static_cast<double>(y));
    payload.minPosition[2] = std::min(payload.minPosition[2], static_cast<double>(z));
    payload.maxPosition[0] = std::max(payload.maxPosition[0], static_cast<double>(x));
    payload.maxPosition[1] = std::max(payload.maxPosition[1], static_cast<double>(y));
    payload.maxPosition[2] = std::max(payload.maxPosition[2], static_cast<double>(z));
}

bool buildColumnSamples(const LoadedPresentation& loaded,
                        const std::vector<PresentationSample>& samples,
                        const HeightProfile& heightProfile,
                        std::vector<ColumnSample>& columns) {
    columns.clear();
    if (samples.empty()) {
        return false;
    }

    columns.reserve(samples.size() + 1U);
    const double totalLength = resolvedTrackLengthSeconds(loaded.metadata, samples);
    const double lengthExtent = exportedLengthExtent(samples, totalLength, heightProfile);

    const auto mapX = [&heightProfile, totalLength, lengthExtent](const double timestamp) {
        if (!heightProfile.normaliseLength) {
            return static_cast<float>(timestamp);
        }
        return static_cast<float>((timestamp / totalLength) * lengthExtent);
    };

    for (const auto& sample : samples) {
        columns.push_back(ColumnSample{
            .sample = sample,
            .x = mapX(sample.timestamp),
            .height = exportedHeight(sample, heightProfile)
        });
    }

    ColumnSample endColumn{};
    endColumn.sample = samples.back();
    endColumn.x = mapX(totalLength);
    endColumn.height = exportedHeight(samples.back(), heightProfile);
    columns.push_back(std::move(endColumn));
    return columns.size() >= 2U;
}

bool buildMeshPayload(const LoadedPresentation& loaded,
                      const std::vector<PresentationSample>& samples,
                      const HeightProfile& heightProfile,
                      MeshPayload& payload) {
    payload = MeshPayload{};
    if (samples.empty()) {
        return false;
    }

    std::vector<ColumnSample> columns;
    if (!buildColumnSamples(loaded, samples, heightProfile, columns)) {
        return false;
    }

    payload.positions.reserve(columns.size() * 12U);
    payload.renderColours.reserve(columns.size() * 12U);
    payload.nativeDisplayColours.reserve(columns.size() * 12U);
    payload.labValues.reserve(columns.size() * 12U);
    payload.loudnessValues.reserve(columns.size() * 12U);
    payload.indices.reserve((columns.size() - 1U) * 24U + 12U);

    for (const auto& column : columns) {
        appendVertex(payload, column.sample, column.x, 0.0f, -kHalfRibbonDepth);
        appendVertex(payload, column.sample, column.x, column.height, -kHalfRibbonDepth);
        appendVertex(payload, column.sample, column.x, 0.0f, kHalfRibbonDepth);
        appendVertex(payload, column.sample, column.x, column.height, kHalfRibbonDepth);
    }

    for (std::size_t columnIndex = 0; columnIndex + 1U < columns.size(); ++columnIndex) {
        const auto baseIndex = static_cast<std::uint32_t>(columnIndex * 4U);
        const auto nextIndex = static_cast<std::uint32_t>((columnIndex + 1U) * 4U);

        constexpr std::array<std::uint32_t, 24> kSpanIndices{
            0U, 4U, 5U, 0U, 5U, 1U,
            2U, 3U, 7U, 2U, 7U, 6U,
            1U, 5U, 7U, 1U, 7U, 3U,
            0U, 2U, 6U, 0U, 6U, 4U
        };
        for (const std::uint32_t index : kSpanIndices) {
            payload.indices.push_back(index < 4U ? baseIndex + index : nextIndex + (index - 4U));
        }
    }

    constexpr std::array<std::uint32_t, 6> kStartCapIndices{0U, 1U, 3U, 0U, 3U, 2U};
    constexpr std::array<std::uint32_t, 6> kEndCapIndices{0U, 3U, 1U, 0U, 2U, 3U};
    const auto startBase = static_cast<std::uint32_t>(0U);
    const auto endBase = static_cast<std::uint32_t>((columns.size() - 1U) * 4U);
    for (const std::uint32_t index : kStartCapIndices) {
        payload.indices.push_back(startBase + index);
    }
    for (const std::uint32_t index : kEndCapIndices) {
        payload.indices.push_back(endBase + index);
    }

    return !payload.indices.empty();
}

void appendPadding(std::vector<std::uint8_t>& buffer, const std::size_t alignment) {
    if (alignment == 0U) {
        return;
    }

    const std::size_t remainder = buffer.size() % alignment;
    if (remainder == 0U) {
        return;
    }

    buffer.insert(buffer.end(), alignment - remainder, std::uint8_t{0});
}

template <typename T>
BufferSpan appendBytes(std::vector<std::uint8_t>& buffer,
                       const std::vector<T>& values,
                       const std::size_t alignment = alignof(T)) {
    appendPadding(buffer, std::max<std::size_t>(4U, alignment));

    BufferSpan span{};
    span.byteOffset = buffer.size();
    span.byteLength = values.size() * sizeof(T);
    if (!values.empty()) {
        const auto* rawBytes = reinterpret_cast<const std::uint8_t*>(values.data());
        buffer.insert(buffer.end(), rawBytes, rawBytes + span.byteLength);
    }
    return span;
}

int addBufferView(tinygltf::Model& model,
                  const int bufferIndex,
                  const BufferSpan& span,
                  const int target) {
    tinygltf::BufferView view;
    view.buffer = bufferIndex;
    view.byteOffset = span.byteOffset;
    view.byteLength = span.byteLength;
    view.target = target;
    model.bufferViews.push_back(std::move(view));
    return static_cast<int>(model.bufferViews.size() - 1);
}

int addAccessor(tinygltf::Model& model,
                const int bufferViewIndex,
                const int componentType,
                const int type,
                const std::size_t count,
                const std::vector<double>& minValues = {},
                const std::vector<double>& maxValues = {}) {
    tinygltf::Accessor accessor;
    accessor.bufferView = bufferViewIndex;
    accessor.byteOffset = 0;
    accessor.componentType = componentType;
    accessor.count = count;
    accessor.type = type;
    accessor.minValues = minValues;
    accessor.maxValues = maxValues;
    model.accessors.push_back(std::move(accessor));
    return static_cast<int>(model.accessors.size() - 1);
}

tinygltf::Value buildPrimitiveExtras(const LoadedPresentation& loaded,
                                     const std::vector<PresentationSample>& samples,
                                     const HeightProfile& heightProfile) {
    tinygltf::Value::Object object;
    object["schema"] = tinygltf::Value("synesthesia.gltf-gradient.v1");
    object["track"] = tinygltf::Value(presentationTrackName(loaded.resolvedTrack));
    object["frame_count"] = tinygltf::Value(static_cast<int>(samples.size()));
    object["geometry"] = tinygltf::Value("continuous-solid-strip");
    object["native_display_rgb_attribute"] = tinygltf::Value("_SYNESTHESIA_DISPLAY_RGB");
    object["native_display_rgb_colour_space"] =
        tinygltf::Value(colourSpaceName(loaded.metadata.presentationData->settings.colourSpace));
    object["native_display_rgb_encoding"] = tinygltf::Value("display-encoded");
    object["lab_attribute"] = tinygltf::Value("_SYNESTHESIA_LAB");
    object["loudness_attribute"] = tinygltf::Value("_SYNESTHESIA_LOUDNESS");
    object["height_mapping"] = tinygltf::Value(
        tinygltf::Value::Object{
            {"position_y", tinygltf::Value("_SYNESTHESIA_LOUDNESS.z")},
            {"minimum_db", tinygltf::Value(static_cast<double>(kMinimumLoudnessDb))},
            {"maximum_db", tinygltf::Value(static_cast<double>(kMaximumLoudnessDb))},
            {"mode", tinygltf::Value(heightProfile.normaliseHeight ? "normalised-loudness" : "absolute-db-offset")},
            {"exported_height_range", makeNumberArrayValue({
                0.0,
                heightProfile.normaliseHeight ? 1.0 : static_cast<double>(kMaximumLoudnessDb - kMinimumLoudnessDb)
            })},
            {"components", tinygltf::Value(
                tinygltf::Value::Object{
                    {"x", tinygltf::Value("frame loudness dB")},
                    {"y", tinygltf::Value("normalised loudness")},
                    {"z", tinygltf::Value("exported height")}
                })}
        });

    // glTF core vertex colours are linear multipliers, so Synesthesia's native
    // display-space colours are preserved separately in custom underscore-prefixed
    // attributes as allowed by the core attribute semantic rules.
    object["render_colour_attribute"] = tinygltf::Value(
        tinygltf::Value::Object{
            {"name", tinygltf::Value("COLOR_0")},
            {"colour_space", tinygltf::Value("linear sRGB")},
            {"purpose", tinygltf::Value("viewer-friendly render path")}
        });
    return tinygltf::Value(std::move(object));
}

tinygltf::Value buildModelExtras(const LoadedPresentation& loaded,
                                 const std::vector<PresentationSample>& samples,
                                 const HeightProfile& heightProfile) {
    tinygltf::Value::Object object;
    object["input_file"] = tinygltf::Value(loaded.inputPath.filename().string());
    object["input_extension"] = tinygltf::Value(loaded.inputPath.extension().string());
    object["track"] = tinygltf::Value(presentationTrackName(loaded.resolvedTrack));
    object["normalised_height"] = tinygltf::Value(heightProfile.normaliseHeight);
    object["normalised_length"] = tinygltf::Value(heightProfile.normaliseLength);
    object["colour_space"] =
        tinygltf::Value(colourSpaceName(loaded.metadata.presentationData->settings.colourSpace));
    const double totalLength = resolvedTrackLengthSeconds(loaded.metadata, samples);
    const double lengthExtent = exportedLengthExtent(samples, totalLength, heightProfile);
    object["duration_seconds"] = tinygltf::Value(totalLength);
    object["sample_rate"] = tinygltf::Value(static_cast<double>(loaded.metadata.sampleRate));
    object["hop_size"] = tinygltf::Value(loaded.metadata.hopSize);
    object["frame_count"] = tinygltf::Value(static_cast<int>(samples.size()));
    object["custom_attributes"] = makeStringArrayValue({
        "_SYNESTHESIA_DISPLAY_RGB",
        "_SYNESTHESIA_LAB",
        "_SYNESTHESIA_LOUDNESS"
    });
    object["depth_range"] = makeNumberArrayValue({
        static_cast<double>(-kHalfRibbonDepth),
        static_cast<double>(kHalfRibbonDepth)
    });
    object["length_mapping"] = tinygltf::Value(
        tinygltf::Value::Object{
            {"mode", tinygltf::Value(heightProfile.normaliseLength ? "duration-and-groove-normalised" : "seconds")},
            {"groove_score", tinygltf::Value(grooveScoreForLength(samples))},
            {"target_extent", tinygltf::Value(lengthExtent)}
        });
    object["length_range"] = makeNumberArrayValue({
        0.0,
        lengthExtent
    });
    return tinygltf::Value(std::move(object));
}

bool buildModel(const LoadedPresentation& loaded,
                const std::vector<PresentationSample>& samples,
                const HeightProfile& heightProfile,
                tinygltf::Model& model) {
    MeshPayload payload;
    if (!buildMeshPayload(loaded, samples, heightProfile, payload)) {
        return false;
    }

    model = tinygltf::Model{};
    model.asset.version = "2.0";
    model.asset.generator = "Synesthesia CLI gltf-gradient";
    model.extras = buildModelExtras(loaded, samples, heightProfile);
    model.extensionsUsed.push_back("KHR_materials_unlit");

    std::vector<std::uint8_t> rawBuffer;
    const BufferSpan indexSpan = appendBytes(rawBuffer, payload.indices);
    const BufferSpan positionSpan = appendBytes(rawBuffer, payload.positions);
    const BufferSpan colourSpan = appendBytes(rawBuffer, payload.renderColours);
    const BufferSpan nativeDisplaySpan = appendBytes(rawBuffer, payload.nativeDisplayColours);
    const BufferSpan labSpan = appendBytes(rawBuffer, payload.labValues);
    const BufferSpan loudnessSpan = appendBytes(rawBuffer, payload.loudnessValues);

    tinygltf::Buffer buffer;
    buffer.data = std::move(rawBuffer);
    model.buffers.push_back(std::move(buffer));

    const int indexView = addBufferView(model, 0, indexSpan, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
    const int positionView = addBufferView(model, 0, positionSpan, TINYGLTF_TARGET_ARRAY_BUFFER);
    const int colourView = addBufferView(model, 0, colourSpan, TINYGLTF_TARGET_ARRAY_BUFFER);
    const int nativeDisplayView = addBufferView(model, 0, nativeDisplaySpan, TINYGLTF_TARGET_ARRAY_BUFFER);
    const int labView = addBufferView(model, 0, labSpan, TINYGLTF_TARGET_ARRAY_BUFFER);
    const int loudnessView = addBufferView(model, 0, loudnessSpan, TINYGLTF_TARGET_ARRAY_BUFFER);

    const int indexAccessor = addAccessor(
        model,
        indexView,
        TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT,
        TINYGLTF_TYPE_SCALAR,
        payload.indices.size());
    const int positionAccessor = addAccessor(
        model,
        positionView,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3,
        payload.positions.size() / 3U,
        {payload.minPosition[0], payload.minPosition[1], payload.minPosition[2]},
        {payload.maxPosition[0], payload.maxPosition[1], payload.maxPosition[2]});
    const int colourAccessor = addAccessor(
        model,
        colourView,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3,
        payload.renderColours.size() / 3U);
    const int nativeDisplayAccessor = addAccessor(
        model,
        nativeDisplayView,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3,
        payload.nativeDisplayColours.size() / 3U);
    const int labAccessor = addAccessor(
        model,
        labView,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3,
        payload.labValues.size() / 3U);
    const int loudnessAccessor = addAccessor(
        model,
        loudnessView,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        TINYGLTF_TYPE_VEC3,
        payload.loudnessValues.size() / 3U);

    tinygltf::Material material;
    material.name = "GradientSolid";
    material.doubleSided = true;
    material.pbrMetallicRoughness.baseColorFactor = {1.0, 1.0, 1.0, 1.0};
    material.pbrMetallicRoughness.metallicFactor = 0.0;
    material.pbrMetallicRoughness.roughnessFactor = 1.0;
    material.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object{});
    model.materials.push_back(std::move(material));

    tinygltf::Primitive primitive;
    primitive.indices = indexAccessor;
    primitive.material = 0;
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
    primitive.attributes["POSITION"] = positionAccessor;
    primitive.attributes["COLOR_0"] = colourAccessor;
    primitive.attributes["_SYNESTHESIA_DISPLAY_RGB"] = nativeDisplayAccessor;
    primitive.attributes["_SYNESTHESIA_LAB"] = labAccessor;
    primitive.attributes["_SYNESTHESIA_LOUDNESS"] = loudnessAccessor;
    primitive.extras = buildPrimitiveExtras(loaded, samples, heightProfile);

    tinygltf::Mesh mesh;
    mesh.name = loaded.inputPath.stem().string() + "_gradient";
    mesh.primitives.push_back(std::move(primitive));
    model.meshes.push_back(std::move(mesh));

    tinygltf::Node node;
    node.name = loaded.inputPath.stem().string();
    node.mesh = 0;
    model.nodes.push_back(std::move(node));

    tinygltf::Scene scene;
    scene.name = "Scene";
    scene.nodes.push_back(0);
    model.scenes.push_back(std::move(scene));
    model.defaultScene = 0;

    return true;
}

bool writeModel(const fs::path& outputPath,
                const tinygltf::Model& model) {
    if (outputPath.has_parent_path()) {
        fs::create_directories(outputPath.parent_path());
    }
    tinygltf::TinyGLTF gltf;
    return gltf.WriteGltfSceneToFile(
        &model,
        outputPath.string(),
        false,
        false,
        true,
        false);
}

}

int runGltfGradientCommand(const Arguments& args) {
    if (args.inputDir.empty()) {
        std::cerr << "Error: --misc gltf-gradient requires --input <file>\n";
        return 1;
    }
    if (args.outputDir.empty()) {
        std::cerr << "Error: --misc gltf-gradient requires --output <file.gltf>\n";
        return 1;
    }

    const fs::path outputPath(args.outputDir);
    std::string outputExtension = outputPath.extension().string();
    std::transform(outputExtension.begin(), outputExtension.end(), outputExtension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (outputExtension != ".gltf") {
        std::cerr << "Error: gltf-gradient requires an .gltf output path\n";
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

    const HeightProfile heightProfile{
        .normaliseHeight = args.normaliseHeight,
        .normaliseLength = args.normaliseLength
    };

    tinygltf::Model model;
    if (!buildModel(loaded, samples, heightProfile, model)) {
        std::cerr << "Error: failed to build GLTF model\n";
        return 1;
    }

    if (!writeModel(outputPath, model)) {
        std::cerr << "Error: failed to write GLTF output\n";
        return 1;
    }

    std::cout << "Exported GLTF gradient: " << outputPath << '\n';
    return 0;
}

}
