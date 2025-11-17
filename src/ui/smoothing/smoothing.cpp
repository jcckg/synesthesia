#include "colour_mapper.h"
#include "smoothing.h"

#include <algorithm>
#include <cmath>

SpringSmoother::SpringSmoother(const float stiffness, const float damping, const float mass)
    : m_stiffness(stiffness), m_baseStiffness(stiffness), m_damping(damping), m_mass(mass), m_rgbCacheDirty(true) {
    initialiseToDefaults();
}

void SpringSmoother::initialiseToDefaults() {
    m_channels[0] = {50.0f, 0.0f, 50.0f};
    m_channels[1] = {0.0f, 0.0f, 0.0f};
    m_channels[2] = {0.0f, 0.0f, 0.0f};

    m_currentRGB[0] = m_currentRGB[1] = m_currentRGB[2] = 0.5f;
}

void SpringSmoother::reset(const float r, const float g, const float b) {
    float L, a, b_comp;
    ColourMapper::RGBtoOklab(r, g, b, L, a, b_comp);

    m_channels[0] = {L, 0.0f, L};
    m_channels[1] = {a, 0.0f, a};
    m_channels[2] = {b_comp, 0.0f, b_comp};

    m_currentRGB[0] = r;
    m_currentRGB[1] = g;
    m_currentRGB[2] = b;
    m_rgbCacheDirty = false;
}

void SpringSmoother::setTargetColour(const float r, const float g, const float b) {
    float L, a, b_comp;
    ColourMapper::RGBtoOklab(r, g, b, L, a, b_comp);

    m_channels[0].targetPosition = L;
    m_channels[1].targetPosition = a;
    m_channels[2].targetPosition = b_comp;
}

bool SpringSmoother::update(float deltaTime) {
    deltaTime = std::min(deltaTime, MAX_DELTA_TIME);
    bool significantMovement = false;

    for (int i = 0; i < 3; ++i) {
        SpringState& channel = m_channels[i];
        
        const float displacement = channel.position - channel.targetPosition;
        const float springForce = -m_stiffness * displacement;
        const float dampingForce = -m_damping * channel.velocity;
        const float acceleration = (springForce + dampingForce) / m_mass;

        const float prevVelocity = channel.velocity;
        const float prevPosition = channel.position;

        channel.velocity += acceleration * deltaTime;
        channel.position += channel.velocity * deltaTime;

        if (i == 0) {
            channel.position = std::clamp(channel.position, LAB_L_MIN, LAB_L_MAX);
        } else {
            channel.position = std::clamp(channel.position, LAB_AB_MIN, LAB_AB_MAX);
        }

        const float positionDelta = std::abs(channel.position - prevPosition);
        const float velocityDelta = std::abs(channel.velocity - prevVelocity);

        if (positionDelta > MIN_DELTA || velocityDelta > MIN_DELTA) {
            significantMovement = true;
        }
    }

    if (significantMovement) {
        m_rgbCacheDirty = true;
    }

    return significantMovement;
}

void SpringSmoother::updateRGBCache() const {
    float r, g, b;
    ColourMapper::OklabtoRGB(m_channels[0].position, m_channels[1].position,
                          m_channels[2].position, r, g, b);

    m_currentRGB[0] = std::clamp(r, 0.0f, 1.0f);
    m_currentRGB[1] = std::clamp(g, 0.0f, 1.0f);
    m_currentRGB[2] = std::clamp(b, 0.0f, 1.0f);
    m_rgbCacheDirty = false;
}

void SpringSmoother::getCurrentColour(float& r, float& g, float& b) const {
    if (m_rgbCacheDirty) {
        updateRGBCache();
    }

    r = m_currentRGB[0];
    g = m_currentRGB[1];
    b = m_currentRGB[2];
}

void SpringSmoother::setSmoothingAmount(float smoothingAmount) {
    smoothingAmount = std::clamp(smoothingAmount, 0.0f, 1.0f);

    m_baseStiffness = MIN_STIFFNESS * std::pow(MAX_STIFFNESS / MIN_STIFFNESS, smoothingAmount);
    m_stiffness = m_baseStiffness;
    m_damping = 2.0f * std::sqrt(m_stiffness * m_mass) * 0.5f;
}

float SpringSmoother::getSmoothingAmount() const {
    const float logRatio = std::log(m_baseStiffness / MIN_STIFFNESS) /
                          std::log(MAX_STIFFNESS / MIN_STIFFNESS);
    return std::clamp(logRatio, 0.0f, 1.0f);
}

// Stowell & Plumbley (2007) - adaptive whitening for improved onset detection
// Adaptive smoothing with perceptual cues
bool SpringSmoother::update(const float deltaTime, const SmoothingSignalFeatures& features) {
    float adaptiveStiffness = m_baseStiffness;

    constexpr float ONSET_STIFFNESS_BOOST = 0.45f;
    if (features.onsetDetected) {
        adaptiveStiffness *= (1.0f + ONSET_STIFFNESS_BOOST);
    }

    // Böck & Widmer (2013) SuperFlux: spectral flux spikes reveal transients, so chase them faster
    constexpr float FLUX_STIFFNESS_GAIN = 8.0f;
    const float fluxContribution = std::clamp(features.spectralFlux * FLUX_STIFFNESS_GAIN, 0.0f, 1.0f);
    adaptiveStiffness *= (1.0f + fluxContribution);

    // Peeters (2004): spectral flatness close to 1 indicates noise, so provide extra damping
    constexpr float FLATNESS_SUPPRESSION = 0.6f;
    const float clampedFlatness = std::clamp(features.spectralFlatness, 0.0f, 1.0f);
    const float noiseSuppression = 1.0f - clampedFlatness * FLATNESS_SUPPRESSION;
    adaptiveStiffness *= std::clamp(noiseSuppression, 0.35f, 1.0f);

    constexpr float PSYCHOACOUSTIC_RANGE = 0.5f;
    const float loudness = std::clamp(features.loudnessNormalised, 0.0f, 1.0f);
    const float brightness = std::clamp(features.brightnessNormalised, 0.0f, 1.0f);
    const float psychoScalar = 0.5f * loudness + 0.5f * brightness;
    adaptiveStiffness *= (1.0f + psychoScalar * PSYCHOACOUSTIC_RANGE);

    adaptiveStiffness = std::clamp(adaptiveStiffness, MIN_STIFFNESS * 0.5f, MAX_STIFFNESS * 2.5f);

    m_stiffness = adaptiveStiffness;
    constexpr float BASE_DAMPING_RATIO = 0.65f;
    constexpr float DAMPING_RANGE = 0.25f;
    const float dampingRatio = std::clamp(BASE_DAMPING_RATIO + (1.0f - loudness) * DAMPING_RANGE, 0.55f, 0.9f);
    m_damping = 2.0f * std::sqrt(m_stiffness * m_mass) * dampingRatio;

    return update(deltaTime);
}

bool SpringSmoother::update(const float deltaTime, const bool onsetDetected,
                            const float spectralFlux, const float spectralFlatness) {
    SmoothingSignalFeatures features{};
    features.onsetDetected = onsetDetected;
    features.spectralFlux = spectralFlux;
    features.spectralFlatness = spectralFlatness;
    return update(deltaTime, features);
}
