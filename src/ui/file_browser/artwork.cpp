#include "ui/file_browser/artwork.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>

namespace UI::FileBrowser {

Artwork::Artwork() : worker_([this](std::stop_token stop) { run(stop); }) {}

Artwork::~Artwork() {
    worker_.request_stop();
    wake_.notify_all();
}

void Artwork::request(std::string directory) {
    std::lock_guard lock(mutex_);
    ++generation_;
    pending_ = std::move(directory);
    result_.reset();
    wake_.notify_all();
}

void Artwork::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::string directory;
        uint64_t generation;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [this] { return pending_.has_value(); });
            if (stop.stop_requested()) return;
            directory = std::move(*pending_);
            pending_.reset();
            generation = generation_.load();
            if (wake_.wait_for(lock, stop, std::chrono::milliseconds(75),
                    [&] { return generation != generation_.load(); })) continue;
            if (stop.stop_requested()) return;
        }
        Image image;
        try {
            image = load(directory, generation, stop);
        } catch (const std::exception&) {
        }
        std::lock_guard lock(mutex_);
        if (!stop.stop_requested() && generation == generation_.load()) result_ = std::move(image);
    }
}

Artwork::Image Artwork::load(const std::string& directory, uint64_t generation, std::stop_token stop) const {
    struct Candidate { std::filesystem::path path; std::string key; int score; };
    const auto cancelled = [&] { return stop.stop_requested() || generation != generation_.load(); };
    std::vector<Candidate> candidates;
    std::error_code error;
    std::filesystem::directory_iterator iterator(std::filesystem::path(std::u8string(directory.begin(), directory.end())), error);
    const std::filesystem::directory_iterator end;
    size_t visited = 0;
    while (!error && iterator != end && visited++ < 100000) {
        if (cancelled()) return {};
        auto extension = iterator->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if ((extension == ".jpg" || extension == ".jpeg" || extension == ".png") && iterator->is_regular_file(error)) {
            auto key = iterator->path().filename().string();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!key.starts_with("._")) {
                int score = 0;
                for (const auto* preferred : {"cover", "front", "album", "albumart", "folder"}) {
                    if (key.find(preferred) != std::string::npos) score += 10;
                    if (key.substr(0, key.find_last_of('.')) == preferred) score += 20;
                }
                candidates.push_back({iterator->path(), std::move(key), score});
                std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                    return a.score == b.score ? a.key < b.key : a.score > b.score;
                });
                if (candidates.size() > 16) candidates.pop_back();
            }
        }
        error.clear();
        iterator.increment(error);
    }
    for (const auto& candidate : candidates) {
        if (cancelled()) return {};
        const auto size = std::filesystem::file_size(candidate.path, error);
        if (error || size == 0 || size > 32u * 1024u * 1024u) continue;
        std::ifstream input(candidate.path, std::ios::binary);
        std::vector<unsigned char> encoded(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size))) continue;
        int decodedWidth, decodedHeight, channels;
        if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &decodedWidth, &decodedHeight, &channels) ||
            decodedWidth <= 0 || decodedHeight <= 0 || static_cast<uint64_t>(decodedWidth) * static_cast<uint64_t>(decodedHeight) > 16000000) continue;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &decodedWidth, &decodedHeight, &channels, 4), stbi_image_free);
        if (!decoded || cancelled()) continue;
        Image image;
        const double scale = std::min(1.0, 1024.0 / static_cast<double>(std::max(decodedWidth, decodedHeight)));
        image.width = std::max(1, static_cast<int>(static_cast<double>(decodedWidth) * scale));
        image.height = std::max(1, static_cast<int>(static_cast<double>(decodedHeight) * scale));
        image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
        for (int y = 0; y < image.height; ++y) {
            if (cancelled()) return {};
            const int sourceY = y * decodedHeight / image.height;
            for (int x = 0; x < image.width; ++x) {
                const int sourceX = x * decodedWidth / image.width;
                std::copy_n(decoded.get() + (static_cast<size_t>(sourceY) * static_cast<size_t>(decodedWidth) + static_cast<size_t>(sourceX)) * 4,
                    4, image.pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4);
            }
        }
        return image;
    }
    return {};
}

void Artwork::upload() {
    std::optional<Image> image;
    {
        std::lock_guard lock(mutex_);
        image = std::move(result_);
        result_.reset();
    }
    if (!image) return;
    bgfx::TextureHandle incoming = BGFX_INVALID_HANDLE;
    if (!image->pixels.empty()) {
        incoming = bgfx::createTexture2D(static_cast<uint16_t>(image->width), static_cast<uint16_t>(image->height), false, 1,
            bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            bgfx::copy(image->pixels.data(), static_cast<uint32_t>(image->pixels.size())));
    }
    if (bgfx::isValid(texture) && (blend >= 0.5f || !bgfx::isValid(previousTexture))) {
        if (bgfx::isValid(previousTexture)) bgfx::destroy(previousTexture);
        previousTexture = texture;
        previousWidth = width;
        previousHeight = height;
    } else if (bgfx::isValid(texture)) {
        bgfx::destroy(texture);
    }
    texture = incoming;
    width = image->width;
    height = image->height;
    blend = 0.0f;
}

void Artwork::advance(float deltaSeconds) {
    blend = std::min(1.0f, blend + std::max(0.0f, deltaSeconds) / 0.18f);
    if (blend == 1.0f && bgfx::isValid(previousTexture)) {
        bgfx::destroy(previousTexture);
        previousTexture = BGFX_INVALID_HANDLE;
        previousWidth = previousHeight = 0;
    }
}

float Artwork::heightRatio() const {
    if (bgfx::isValid(texture) && width > 0 && height > 0) return static_cast<float>(height) / static_cast<float>(width);
    if (bgfx::isValid(previousTexture) && previousWidth > 0 && previousHeight > 0) {
        return static_cast<float>(previousHeight) / static_cast<float>(previousWidth) * (1.0f - blend);
    }
    return 0.0f;
}

void Artwork::release() {
    if (bgfx::isValid(texture)) bgfx::destroy(texture);
    if (bgfx::isValid(previousTexture)) bgfx::destroy(previousTexture);
    texture = previousTexture = BGFX_INVALID_HANDLE;
    width = height = previousWidth = previousHeight = 0;
    blend = 1.0f;
}

}
