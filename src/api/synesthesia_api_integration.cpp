#include "synesthesia_api_integration.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Synesthesia {

std::unique_ptr<SynesthesiaAPIIntegration> SynesthesiaAPIIntegration::instance_;
std::mutex SynesthesiaAPIIntegration::instance_mutex_;

SynesthesiaAPIIntegration::SynesthesiaAPIIntegration() 
    : colour_mapper_(std::make_unique<ColourMapper>()) {
}

SynesthesiaAPIIntegration::~SynesthesiaAPIIntegration() {
    stopServer();
}

bool SynesthesiaAPIIntegration::startServer(const API::ServerConfig& config) {
    if (api_server_ && api_server_->isRunning()) {
        return true;
    }
    
    api_server_ = std::make_unique<API::APIServer>(config);
    api_server_->setColourDataProvider([this](uint32_t& sample_rate, uint32_t& fft_size, uint64_t& timestamp, API::SpectralCharacteristics& spectral_characteristics) -> std::vector<API::ColourData> {
        std::lock_guard<std::mutex> lock(data_mutex_);
        sample_rate = last_sample_rate_;
        fft_size = last_fft_size_;
        timestamp = last_timestamp_;
        spectral_characteristics = last_spectral_characteristics_;
        return last_colour_data_;
    });

    api_server_->setFullSpectrumDataProvider([this](uint32_t& sample_rate, uint32_t& fft_size, uint64_t& timestamp) -> std::vector<API::SpectralBin> {
        std::lock_guard<std::mutex> lock(spectral_data_mutex_);
        sample_rate = last_sample_rate_;
        fft_size = last_fft_size_;
        timestamp = last_timestamp_;
        return last_spectral_data_;
    });
    
    api_server_->setConfigUpdateCallback([this](const API::ConfigUpdate& config) {
        updateSmoothingConfig(config.smoothing_enabled != 0, config.smoothing_factor);
        updateFrequencyRange(config.frequency_range_min, config.frequency_range_max);
        
        ColourSpace space = ColourSpace::RGB;
        switch (config.colour_space) {
            case 0: space = ColourSpace::RGB; break;
            case 1: space = ColourSpace::LAB; break;
            case 2: space = ColourSpace::XYZ; break;
        }
        updateColourSpace(space);
    });
    
    return api_server_->start();
}

void SynesthesiaAPIIntegration::stopServer() {
    if (api_server_) {
        api_server_->stop();
        api_server_.reset();
    }
}

bool SynesthesiaAPIIntegration::isServerRunning() const {
    return api_server_ && api_server_->isRunning();
}


void SynesthesiaAPIIntegration::updateColourData(const std::vector<float>& magnitudes, const std::vector<float>& phases, float spectralCentroid, float sampleRate, float r, float g, float b, float L, float a, float b_comp) {
    if (!api_server_ || !api_server_->isRunning()) {
        return;
    }

    auto spectralChars = ColourMapper::calculateSpectralCharacteristics(magnitudes, sampleRate);

    std::vector<API::ColourData> colour_data;
    colour_data.reserve(1);

    API::ColourData data{};
    data.frequency = spectralCentroid;

    if (!magnitudes.empty() && phases.size() == magnitudes.size() &&
        spectralCentroid > 0.0f && magnitudes.size() > 1 && sampleRate > 0.0f) {
        const float fftSize = 2.0f * static_cast<float>(magnitudes.size() - 1);
        const float bin_freq = sampleRate / fftSize;
        const float float_index = spectralCentroid / bin_freq;
        size_t index_floor = static_cast<size_t>(float_index);
        size_t index_ceil = index_floor + 1;

        if (index_ceil < magnitudes.size()) {
            const float fraction = float_index - static_cast<float>(index_floor);

            const float mag1 = magnitudes[index_floor];
            const float mag2 = magnitudes[index_ceil];
            data.magnitude = (1.0f - fraction) * mag1 + fraction * mag2;

            const float phase1 = phases[index_floor];
            const float phase2 = phases[index_ceil];
            float phase_diff = phase2 - phase1;

            constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
            if (phase_diff > std::numbers::pi_v<float>) {
                phase_diff -= TWO_PI;
            } else if (phase_diff < -std::numbers::pi_v<float>) {
                phase_diff += TWO_PI;
            }

            float interpolated_phase = phase1 + fraction * phase_diff;
            data.phase = fmodf(interpolated_phase + std::numbers::pi_v<float>, TWO_PI) - std::numbers::pi_v<float>;
        }
    }

    data.wavelength = ColourMapper::logFrequencyToWavelength(spectralCentroid);

    switch (current_colour_space_) {
        case ColourSpace::RGB:
            data.r = r;
            data.g = g;
            data.b = b;
            break;

        case ColourSpace::LAB:
            data.r = L;
            data.g = a;
            data.b = b_comp;
            break;

            case ColourSpace::XYZ: {
                // Fall back to RGB for now
                data.r = r;
                data.g = g;
                data.b = b;
                break;
            }
        }

        colour_data.push_back(data);

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        last_colour_data_ = std::move(colour_data);

        last_spectral_characteristics_.flatness = spectralChars.flatness;
        last_spectral_characteristics_.centroid = spectralChars.centroid;
        last_spectral_characteristics_.spread = spectralChars.spread;
        last_spectral_characteristics_.normalisedSpread = spectralChars.normalisedSpread;

        last_sample_rate_ = static_cast<uint32_t>(sampleRate);
        const uint32_t derivedFftSize = (magnitudes.size() > 1)
            ? static_cast<uint32_t>((magnitudes.size() - 1) * 2)
            : static_cast<uint32_t>(magnitudes.size() * 2);
        last_fft_size_ = std::max<uint32_t>(1u, derivedFftSize);
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        last_timestamp_ = static_cast<uint64_t>(std::max(duration, static_cast<decltype(duration)>(0)));
    }

    if (full_spectrum_stream_enabled_) {
        std::lock_guard<std::mutex> lock(spectral_data_mutex_);
        last_spectral_data_.clear();
        last_spectral_data_.reserve(magnitudes.size());
        for (size_t i = 0; i < magnitudes.size(); ++i) {
            API::SpectralBin bin{};
            bin.frequency = (static_cast<float>(i) * static_cast<float>(last_sample_rate_)) / static_cast<float>(last_fft_size_);
            bin.magnitude = magnitudes[i];
            bin.phase = phases[i];
            last_spectral_data_.push_back(bin);
        }
    }
}

void SynesthesiaAPIIntegration::updateSmoothingConfig(bool enabled, float factor) {
    smoothing_enabled_ = enabled;
    smoothing_factor_ = std::clamp(factor, 0.0f, 1.0f);
    
    if (api_server_ && api_server_->isRunning()) {
        API::ConfigUpdate config{};
        config.smoothing_enabled = enabled ? 1 : 0;
        config.smoothing_factor = factor;
        config.colour_space = static_cast<uint32_t>(current_colour_space_);
        config.frequency_range_min = frequency_range_min_;
        config.frequency_range_max = frequency_range_max_;
        
        api_server_->broadcastConfigUpdate(config);
    }
}

void SynesthesiaAPIIntegration::updateFrequencyRange(uint32_t min_freq, uint32_t max_freq) {
    frequency_range_min_ = min_freq;
    frequency_range_max_ = max_freq;
}

void SynesthesiaAPIIntegration::updateColourSpace(ColourSpace colour_space) {
    current_colour_space_ = colour_space;
}

void SynesthesiaAPIIntegration::enableFullSpectrumStream(bool enable) {
    full_spectrum_stream_enabled_ = enable;
    if (api_server_) {
        api_server_->enableFullSpectrumStream(enable);
    }
}

std::vector<std::string> SynesthesiaAPIIntegration::getConnectedClients() const {
    if (!api_server_) {
        return {};
    }
    return api_server_->getConnectedClients();
}

size_t SynesthesiaAPIIntegration::getLastDataSize() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_colour_data_.size();
}

uint32_t SynesthesiaAPIIntegration::getCurrentFPS() const {
    if (!api_server_) return 0;
    return api_server_->getCurrentFPS();
}

bool SynesthesiaAPIIntegration::isHighPerformanceMode() const {
    if (!api_server_) return false;
    return api_server_->isHighPerformanceMode();
}

float SynesthesiaAPIIntegration::getAverageFrameTime() const {
    if (!api_server_) return 0.0f;
    return api_server_->getAverageFrameTime();
}

uint64_t SynesthesiaAPIIntegration::getTotalFramesSent() const {
    if (!api_server_) return 0;
    return api_server_->getTotalFramesSent();
}

SynesthesiaAPIIntegration& SynesthesiaAPIIntegration::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<SynesthesiaAPIIntegration>();
    }
    return *instance_;
}


}
