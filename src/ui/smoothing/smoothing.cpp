#include "colour_mapper.h"
#include "smoothing.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMinStiffness = 8.0f;
constexpr float kMaxStiffness = 120.0f;

float smoothingAmountToBaseStiffness(float smoothingAmount) {
    smoothingAmount = std::clamp(smoothingAmount, 0.0f, 1.0f);
    const float responseAmount = 1.0f - smoothingAmount;
    return kMinStiffness * std::pow(kMaxStiffness / kMinStiffness, responseAmount);
}

float baseStiffnessToSmoothingAmount(float stiffness) {
    const float clampedStiffness = std::clamp(stiffness, kMinStiffness, kMaxStiffness);
    const float logRatio = std::log(clampedStiffness / kMinStiffness) /
        std::log(kMaxStiffness / kMinStiffness);
    return std::clamp(1.0f - logRatio, 0.0f, 1.0f);
}

float applyAdaptiveStiffness(const float baseStiffness,
                             const SmoothingSignalFeatures& features) {
    float adaptiveStiffness = baseStiffness;

    constexpr float onsetStiffnessBoost = 0.45f;
    if (features.onsetDetected) {
        adaptiveStiffness *= (1.0f + onsetStiffnessBoost);
    }

    constexpr float fluxStiffnessGain = 8.0f;
    const float fluxContribution = std::clamp(features.spectralFlux * fluxStiffnessGain, 0.0f, 1.0f);
    adaptiveStiffness *= (1.0f + fluxContribution);

    constexpr float flatnessSuppression = 0.6f;
    const float clampedFlatness = std::clamp(features.spectralFlatness, 0.0f, 1.0f);
    const float noiseSuppression = 1.0f - clampedFlatness * flatnessSuppression;
    adaptiveStiffness *= std::clamp(noiseSuppression, 0.35f, 1.0f);

    constexpr float crestStiffnessGain = 0.35f;
    adaptiveStiffness *= (1.0f + features.spectralCrestNorm * crestStiffnessGain);

    constexpr float phaseTransientGain = 0.55f;
    adaptiveStiffness *= (1.0f + features.phaseTransientNorm * phaseTransientGain);

    constexpr float phaseCoherenceGain = 0.12f;
    adaptiveStiffness *= (1.0f + features.phaseCoherenceNorm * phaseCoherenceGain);

    constexpr float phaseInstabilitySuppression = 0.28f;
    const float instabilityPenalty = features.phaseInstabilityNorm * (1.0f - 0.65f * features.phaseTransientNorm);
    adaptiveStiffness *= std::clamp(1.0f - instabilityPenalty * phaseInstabilitySuppression, 0.7f, 1.0f);

    constexpr float psychoacousticRange = 0.5f;
    const float loudness = std::clamp(features.loudnessNormalised, 0.0f, 1.0f);
    const float brightness = std::clamp(features.brightnessNormalised, 0.0f, 1.0f);
    const float psychoScalar = 0.5f * loudness + 0.5f * brightness;
    adaptiveStiffness *= (1.0f + psychoScalar * psychoacousticRange);

    return std::clamp(adaptiveStiffness, kMinStiffness * 0.5f, kMaxStiffness * 2.5f);
}

}

float resolveAdaptiveSmoothingAmount(const float baseSmoothingAmount,
                                     const SmoothingSignalFeatures& features) {
    const float baseStiffness = smoothingAmountToBaseStiffness(baseSmoothingAmount);
    const float adaptiveStiffness = applyAdaptiveStiffness(baseStiffness, features);
    return baseStiffnessToSmoothingAmount(adaptiveStiffness);
}

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

    resetOklab(L, a, b_comp);
}

void SpringSmoother::resetOklab(const float L, const float a, const float b_comp) {
    m_channels[0] = {L, 0.0f, L};
    m_channels[1] = {a, 0.0f, a};
    m_channels[2] = {b_comp, 0.0f, b_comp};

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ColourMapper::OklabtoRGB(L, a, b_comp, r, g, b);
    m_currentRGB[0] = r;
    m_currentRGB[1] = g;
    m_currentRGB[2] = b;
    m_rgbCacheDirty = false;
}

void SpringSmoother::setTargetColour(const float r, const float g, const float b) {
    float L, a, b_comp;
    ColourMapper::RGBtoOklab(r, g, b, L, a, b_comp);

    setTargetOklab(L, a, b_comp);
}

void SpringSmoother::setTargetOklab(const float L, const float a, const float b_comp) {
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

void SpringSmoother::getCurrentOklab(float& L, float& a, float& b) const {
    L = m_channels[0].position;
    a = m_channels[1].position;
    b = m_channels[2].position;
}

void SpringSmoother::setSmoothingAmount(float smoothingAmount) {
    smoothingAmount = std::clamp(smoothingAmount, 0.0f, 1.0f);

    m_baseStiffness = smoothingAmountToBaseStiffness(smoothingAmount);
    m_stiffness = m_baseStiffness;
    m_damping = 2.0f * std::sqrt(m_stiffness * m_mass) * 0.5f;
}

float SpringSmoother::getSmoothingAmount() const {
    return baseStiffnessToSmoothingAmount(m_baseStiffness);
}

// Stowell & Plumbley (2007) - adaptive whitening for improved onset detection
// Adaptive smoothing with perceptual cues
bool SpringSmoother::update(const float deltaTime, const SmoothingSignalFeatures& features) {
    const float adaptiveStiffness = applyAdaptiveStiffness(m_baseStiffness, features);
    const float loudness = std::clamp(features.loudnessNormalised, 0.0f, 1.0f);

    m_stiffness = adaptiveStiffness;
    constexpr float BASE_DAMPING_RATIO = 0.65f;
    constexpr float DAMPING_RANGE = 0.25f;
    // Spectral rolloff → damping: bright timbre (high rolloff) = more responsive colour;
    // warm timbre (low rolloff) = more sluggish, smoother transitions
    constexpr float ROLLOFF_DAMPING_RANGE = 0.15f;
    const float rolloffDampingOffset = (0.5f - features.spectralRolloffNorm) * ROLLOFF_DAMPING_RANGE;
    const float phaseDampingOffset =
        features.phaseInstabilityNorm * 0.12f -
        features.phaseCoherenceNorm * 0.05f -
        features.phaseTransientNorm * 0.04f;
    const float dampingRatio = std::clamp(
        BASE_DAMPING_RATIO + (1.0f - loudness) * DAMPING_RANGE + rolloffDampingOffset + phaseDampingOffset,
        0.45f, 0.95f);
    m_damping = 2.0f * std::sqrt(m_stiffness * m_mass) * dampingRatio;

    return update(deltaTime);
}

bool SpringSmoother::update(const float deltaTime, const bool onsetDetected,
                            const float spectralFlux, const float spectralFlatness) {
    SmoothingSignalFeatures features{};
    features.onsetDetected = onsetDetected;
    features.spectralFlux = spectralFlux;
    features.spectralFlatness = spectralFlatness;
    features.spectralRolloffNorm = 0.5f;  // neutral: no damping offset when rolloff is unavailable
    return update(deltaTime, features);
}
