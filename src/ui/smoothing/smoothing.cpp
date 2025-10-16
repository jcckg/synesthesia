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
// Adaptive smoothing: reduce during onsets, scale by flux, increase for noise
bool SpringSmoother::update(const float deltaTime, const bool onsetDetected,
                            const float spectralFlux, const float spectralFlatness) {
    float adaptiveStiffness = m_baseStiffness;

    constexpr float ONSET_STIFFNESS_REDUCTION = 0.3f;
    if (onsetDetected) {
        adaptiveStiffness *= (1.0f - ONSET_STIFFNESS_REDUCTION);
    }

    constexpr float FLUX_SENSITIVITY = 2.0f;
    const float clampedFlux = std::clamp(spectralFlux * 10.0f, 0.0f, 1.0f);
    const float fluxScale = 1.0f - (clampedFlux * FLUX_SENSITIVITY * 0.5f);
    adaptiveStiffness *= std::max(fluxScale, 0.1f);

    constexpr float FLATNESS_SMOOTHING_BOOST = 1.5f;
    const float clampedFlatness = std::clamp(spectralFlatness, 0.0f, 1.0f);
    const float flatnessScale = 1.0f + (clampedFlatness * FLATNESS_SMOOTHING_BOOST);
    adaptiveStiffness *= flatnessScale;

    adaptiveStiffness = std::clamp(adaptiveStiffness, MIN_STIFFNESS * 0.5f, MAX_STIFFNESS * 2.0f);

    m_stiffness = adaptiveStiffness;
    m_damping = 2.0f * std::sqrt(m_stiffness * m_mass) * 0.5f;

    return update(deltaTime);
}
