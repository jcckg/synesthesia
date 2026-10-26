#pragma once

#include <bgfx/bgfx.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace UI::FileBrowser {

class Artwork {
public:
    Artwork();
    ~Artwork();
    void request(std::string directory);
    void upload();
    void advance(float deltaSeconds);
    float heightRatio() const;
    void release();
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle previousTexture = BGFX_INVALID_HANDLE;
    int previousWidth = 0;
    int previousHeight = 0;
    float blend = 1.0f;
    int width = 0;
    int height = 0;

private:
    struct Image {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };
    Image load(const std::string& directory, uint64_t generation, std::stop_token stop) const;
    void run(std::stop_token stop);
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<std::string> pending_;
    std::optional<Image> result_;
    std::atomic<uint64_t> generation_{0};
    std::jthread worker_;
};

}
