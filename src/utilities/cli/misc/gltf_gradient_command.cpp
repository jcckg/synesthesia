#include "misc/gltf_gradient_command.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "misc/presentation_export_utils.h"
#include <tiny_gltf_v3.h>

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

class GltfValues {
public:
    tg3_str string(std::string_view text) {
        strings_.emplace_back(text);
        const auto& stored = strings_.back();
        return {stored.data(), static_cast<uint32_t>(stored.size())};
    }

    tg3_value value(std::string_view text) {
        tg3_value result{};
        result.type = TG3_VALUE_STRING;
        result.string_val = string(text);
        return result;
    }

    tg3_value value(const char* text) { return value(std::string_view(text)); }

    tg3_value value(bool flag) {
        tg3_value result{};
        result.type = TG3_VALUE_BOOL;
        result.bool_val = flag;
        return result;
    }

    tg3_value value(int64_t number) {
        tg3_value result{};
        result.type = TG3_VALUE_INT;
        result.int_val = number;
        return result;
    }

    tg3_value value(int number) { return value(static_cast<int64_t>(number)); }

    tg3_value value(double number) {
        tg3_value result{};
        result.type = TG3_VALUE_REAL;
        result.real_val = number;
        return result;
    }

    tg3_value array(std::initializer_list<tg3_value> values) {
        arrays_.emplace_back(values);
        tg3_value result{};
        result.type = TG3_VALUE_ARRAY;
        result.array_data = arrays_.back().data();
        result.array_count = static_cast<uint32_t>(arrays_.back().size());
        return result;
    }

    tg3_value object(std::initializer_list<std::pair<std::string_view, tg3_value>> fields) {
        auto& stored = objects_.emplace_back();
        stored.reserve(fields.size());
        for (const auto& [key, field] : fields) {
            stored.push_back({string(key), field});
        }
        tg3_value result{};
        result.type = TG3_VALUE_OBJECT;
        result.object_data = stored.data();
        result.object_count = static_cast<uint32_t>(stored.size());
        return result;
    }

private:
    std::deque<std::string> strings_;
    std::deque<std::vector<tg3_value>> arrays_;
    std::deque<std::vector<tg3_kv_pair>> objects_;
};

struct GradientModel {
    GltfValues values;
    tg3_model model{};
    std::vector<uint8_t> bytes;
    std::vector<tg3_buffer_view> views;
    std::vector<tg3_accessor> accessors;
    std::array<double, 3> minPosition{};
    std::array<double, 3> maxPosition{};
    std::array<tg3_str_int_pair, 5> attributes{};
    tg3_value modelExtras{};
    tg3_value primitiveExtras{};
    tg3_str unlitName{};
    tg3_extension unlitExtension{};
    tg3_buffer buffer{};
    tg3_material material{};
    tg3_primitive primitive{};
    tg3_mesh mesh{};
    tg3_node node{};
    tg3_scene scene{};
    int32_t sceneNode = 0;

    GradientModel() = default;
    GradientModel(const GradientModel&) = delete;
    GradientModel& operator=(const GradientModel&) = delete;
};

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

int addAccessor(GradientModel& document,
                const BufferSpan& span,
                const int componentType,
                const int type,
                const std::size_t count,
                const int target) {
    tg3_buffer_view view{};
    view.buffer = 0;
    view.byte_offset = span.byteOffset;
    view.byte_length = span.byteLength;
    view.target = target;
    document.views.push_back(view);

    tg3_accessor accessor{};
    accessor.buffer_view = static_cast<int32_t>(document.views.size() - 1u);
    accessor.component_type = componentType;
    accessor.count = count;
    accessor.type = type;
    document.accessors.push_back(accessor);
    return static_cast<int>(document.accessors.size() - 1u);
}

tg3_value buildPrimitiveExtras(GltfValues& values,
                               const LoadedPresentation& loaded,
                               const std::vector<PresentationSample>& samples,
                               const HeightProfile& heightProfile) {
    return values.object({
        {"schema", values.value("synesthesia.gltf-gradient.v1")},
        {"track", values.value(presentationTrackName(loaded.resolvedTrack))},
        {"frame_count", values.value(static_cast<int64_t>(samples.size()))},
        {"geometry", values.value("continuous-solid-strip")},
        {"native_display_rgb_attribute", values.value("_SYNESTHESIA_DISPLAY_RGB")},
        {"native_display_rgb_colour_space", values.value(
            colourSpaceName(loaded.metadata.presentationData->settings.colourSpace))},
        {"native_display_rgb_encoding", values.value("display-encoded")},
        {"lab_attribute", values.value("_SYNESTHESIA_LAB")},
        {"loudness_attribute", values.value("_SYNESTHESIA_LOUDNESS")},
        {"height_mapping", values.object({
            {"position_y", values.value("_SYNESTHESIA_LOUDNESS.z")},
            {"minimum_db", values.value(static_cast<double>(kMinimumLoudnessDb))},
            {"maximum_db", values.value(static_cast<double>(kMaximumLoudnessDb))},
            {"mode", values.value(
                heightProfile.normaliseHeight ? "normalised-loudness" : "absolute-db-offset")},
            {"exported_height_range", values.array({
                values.value(0.0),
                values.value(heightProfile.normaliseHeight
                    ? 1.0
                    : static_cast<double>(kMaximumLoudnessDb - kMinimumLoudnessDb))
            })},
            {"components", values.object({
                {"x", values.value("frame loudness dB")},
                {"y", values.value("normalised loudness")},
                {"z", values.value("exported height")}
            })}
        })},
        {"render_colour_attribute", values.object({
            {"name", values.value("COLOR_0")},
            {"colour_space", values.value("linear sRGB")},
            {"purpose", values.value("viewer-friendly render path")}
        })}
    });
}

tg3_value buildModelExtras(GltfValues& values,
                           const LoadedPresentation& loaded,
                           const std::vector<PresentationSample>& samples,
                           const HeightProfile& heightProfile) {
    const double totalLength = resolvedTrackLengthSeconds(loaded.metadata, samples);
    const double lengthExtent = exportedLengthExtent(samples, totalLength, heightProfile);
    return values.object({
        {"input_file", values.value(loaded.inputPath.filename().string())},
        {"input_extension", values.value(loaded.inputPath.extension().string())},
        {"track", values.value(presentationTrackName(loaded.resolvedTrack))},
        {"normalised_height", values.value(heightProfile.normaliseHeight)},
        {"normalised_length", values.value(heightProfile.normaliseLength)},
        {"colour_space", values.value(
            colourSpaceName(loaded.metadata.presentationData->settings.colourSpace))},
        {"duration_seconds", values.value(totalLength)},
        {"sample_rate", values.value(static_cast<double>(loaded.metadata.sampleRate))},
        {"hop_size", values.value(loaded.metadata.hopSize)},
        {"frame_count", values.value(static_cast<int64_t>(samples.size()))},
        {"custom_attributes", values.array({
            values.value("_SYNESTHESIA_DISPLAY_RGB"),
            values.value("_SYNESTHESIA_LAB"),
            values.value("_SYNESTHESIA_LOUDNESS")
        })},
        {"depth_range", values.array({
            values.value(static_cast<double>(-kHalfRibbonDepth)),
            values.value(static_cast<double>(kHalfRibbonDepth))
        })},
        {"length_mapping", values.object({
            {"mode", values.value(
                heightProfile.normaliseLength ? "duration-and-groove-normalised" : "seconds")},
            {"groove_score", values.value(grooveScoreForLength(samples))},
            {"target_extent", values.value(lengthExtent)}
        })},
        {"length_range", values.array({values.value(0.0), values.value(lengthExtent)})}
    });
}

bool buildModel(const LoadedPresentation& loaded,
                const std::vector<PresentationSample>& samples,
                const HeightProfile& heightProfile,
                GradientModel& document) {
    MeshPayload payload;
    if (!buildMeshPayload(loaded, samples, heightProfile, payload)) {
        return false;
    }

    auto& model = document.model;
    auto& values = document.values;
    model.asset.version = values.string("2.0");
    model.asset.generator = values.string("Synesthesia CLI gltf-gradient");
    document.modelExtras = buildModelExtras(values, loaded, samples, heightProfile);
    model.ext.extras = &document.modelExtras;
    document.unlitName = values.string("KHR_materials_unlit");
    model.extensions_used = &document.unlitName;
    model.extensions_used_count = 1;

    const int indexAccessor = addAccessor(document, appendBytes(document.bytes, payload.indices),
        TG3_COMPONENT_TYPE_UNSIGNED_INT, TG3_TYPE_SCALAR, payload.indices.size(), TG3_TARGET_ELEMENT_ARRAY_BUFFER);
    const int positionAccessor = addAccessor(document, appendBytes(document.bytes, payload.positions),
        TG3_COMPONENT_TYPE_FLOAT, TG3_TYPE_VEC3, payload.positions.size() / 3u, TG3_TARGET_ARRAY_BUFFER);
    const int colourAccessor = addAccessor(document, appendBytes(document.bytes, payload.renderColours),
        TG3_COMPONENT_TYPE_FLOAT, TG3_TYPE_VEC3, payload.renderColours.size() / 3u, TG3_TARGET_ARRAY_BUFFER);
    const int nativeDisplayAccessor = addAccessor(document, appendBytes(document.bytes, payload.nativeDisplayColours),
        TG3_COMPONENT_TYPE_FLOAT, TG3_TYPE_VEC3, payload.nativeDisplayColours.size() / 3u, TG3_TARGET_ARRAY_BUFFER);
    const int labAccessor = addAccessor(document, appendBytes(document.bytes, payload.labValues),
        TG3_COMPONENT_TYPE_FLOAT, TG3_TYPE_VEC3, payload.labValues.size() / 3u, TG3_TARGET_ARRAY_BUFFER);
    const int loudnessAccessor = addAccessor(document, appendBytes(document.bytes, payload.loudnessValues),
        TG3_COMPONENT_TYPE_FLOAT, TG3_TYPE_VEC3, payload.loudnessValues.size() / 3u, TG3_TARGET_ARRAY_BUFFER);
    document.minPosition = payload.minPosition;
    document.maxPosition = payload.maxPosition;
    auto& positions = document.accessors[static_cast<std::size_t>(positionAccessor)];
    positions.min_values = document.minPosition.data();
    positions.min_values_count = 3;
    positions.max_values = document.maxPosition.data();
    positions.max_values_count = 3;
    model.accessors = document.accessors.data();
    model.accessors_count = static_cast<uint32_t>(document.accessors.size());
    model.buffer_views = document.views.data();
    model.buffer_views_count = static_cast<uint32_t>(document.views.size());
    document.buffer.byte_length = document.bytes.size();
    document.buffer.data = {document.bytes.data(), document.bytes.size()};
    model.buffers = &document.buffer;
    model.buffers_count = 1;

    auto& material = document.material;
    material.name = values.string("GradientSolid");
    material.double_sided = 1;
    material.alpha_mode = values.string("OPAQUE");
    material.alpha_cutoff = 0.5;
    auto& pbr = material.pbr_metallic_roughness;
    std::fill_n(pbr.base_color_factor, 4, 1.0);
    pbr.metallic_factor = 0.0;
    pbr.roughness_factor = 1.0;
    pbr.base_color_texture.index = -1;
    pbr.metallic_roughness_texture.index = -1;
    material.normal_texture.index = -1;
    material.occlusion_texture.index = -1;
    material.emissive_texture.index = -1;
    document.unlitExtension = {document.unlitName, values.object({})};
    material.ext.extensions = &document.unlitExtension;
    material.ext.extensions_count = 1;
    model.materials = &material;
    model.materials_count = 1;

    document.attributes = {{
        {values.string("POSITION"), positionAccessor},
        {values.string("COLOR_0"), colourAccessor},
        {values.string("_SYNESTHESIA_DISPLAY_RGB"), nativeDisplayAccessor},
        {values.string("_SYNESTHESIA_LAB"), labAccessor},
        {values.string("_SYNESTHESIA_LOUDNESS"), loudnessAccessor}
    }};
    auto& primitive = document.primitive;
    primitive.indices = indexAccessor;
    primitive.material = 0;
    primitive.mode = TG3_MODE_TRIANGLES;
    primitive.attributes = document.attributes.data();
    primitive.attributes_count = static_cast<uint32_t>(document.attributes.size());
    document.primitiveExtras = buildPrimitiveExtras(values, loaded, samples, heightProfile);
    primitive.ext.extras = &document.primitiveExtras;
    document.mesh.name = values.string(loaded.inputPath.stem().string() + "_gradient");
    document.mesh.primitives = &primitive;
    document.mesh.primitives_count = 1;
    model.meshes = &document.mesh;
    model.meshes_count = 1;

    auto& node = document.node;
    node.name = values.string(loaded.inputPath.stem().string());
    node.camera = node.skin = node.light = node.emitter = -1;
    node.mesh = 0;
    node.rotation[3] = 1.0;
    std::fill_n(node.scale, 3, 1.0);
    model.nodes = &node;
    model.nodes_count = 1;
    document.scene.name = values.string("Scene");
    document.scene.nodes = &document.sceneNode;
    document.scene.nodes_count = 1;
    model.scenes = &document.scene;
    model.scenes_count = 1;
    model.default_scene = 0;
    return true;
}

struct GltfOutput {
    tg3_write_options options{};
    tg3_error_stack errors{};
    uint8_t* data = nullptr;
    uint64_t size = 0;

    GltfOutput() {
        tg3_write_options_init(&options);
        options.pretty_print = 1;
        options.embed_buffers = 0;
        tg3_error_stack_init(&errors);
    }

    ~GltfOutput() {
        tg3_write_free(data, &options);
        tg3_error_stack_free(&errors);
    }

    GltfOutput(const GltfOutput&) = delete;
    GltfOutput& operator=(const GltfOutput&) = delete;
};

std::string bufferUri(const fs::path& filename) {
    const auto utf8 = filename.u8string();
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : utf8) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex[byte >> 4u]);
            result.push_back(hex[byte & 15u]);
        }
    }
    return result;
}

bool writeBytes(const fs::path& filepath, const uint8_t* bytes, uint64_t size) {
    if (size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    std::ofstream output(filepath, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    output.close();
    return static_cast<bool>(output);
}

bool writeModel(const fs::path& outputPath, GradientModel& document) {
    std::error_code error;
    if (outputPath.has_parent_path()) {
        fs::create_directories(outputPath.parent_path(), error);
        if (error) {
            return false;
        }
    }
    fs::path binaryPath = outputPath;
    binaryPath.replace_extension(".bin");
    document.buffer.uri = document.values.string(bufferUri(binaryPath.filename()));
    GltfOutput output;
    const auto status = tg3_write_to_memory(
        &document.model, &output.errors, &output.data, &output.size, &output.options);
    if (status != TG3_OK) {
        for (uint32_t i = 0; i < tg3_errors_count(&output.errors); ++i) {
            const auto* entry = tg3_errors_get(&output.errors, i);
            if (entry != nullptr && entry->message != nullptr) {
                std::cerr << "Error: " << entry->message << '\n';
            }
        }
        return false;
    }
    return writeBytes(binaryPath, document.bytes.data(), document.bytes.size()) &&
           writeBytes(outputPath, output.data, output.size);
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

    GradientModel model;
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
