#pragma once

class SpringSmoother {
public:
    explicit SpringSmoother(float stiffness = 8.0f, float damping = 1.0f, float mass = 0.3f);

    void reset(float r, float g, float b);
    void setTargetColour(float r, float g, float b);
    bool update(float deltaTime);
    bool update(float deltaTime, bool onsetDetected, float spectralFlux, float spectralFlatness);
    void getCurrentColour(float& r, float& g, float& b) const;
    void setSmoothingAmount(float smoothingAmount);
    [[nodiscard]] float getSmoothingAmount() const;

private:
    struct SpringState {
        float position = 0.0f;
        float velocity = 0.0f; 
        float targetPosition = 0.0f;
    };

    static constexpr float MIN_DELTA = 0.0001f;
    static constexpr float MAX_DELTA_TIME = 0.05f;
    static constexpr float MIN_STIFFNESS = 8.0f;
    static constexpr float MAX_STIFFNESS = 120.0f;
    static constexpr float LAB_L_MIN = 0.0f;
    static constexpr float LAB_L_MAX = 100.0f;
    static constexpr float LAB_AB_MIN = -100.0f;
    static constexpr float LAB_AB_MAX = 100.0f;

    SpringState m_channels[3];
    float m_stiffness;
    float m_baseStiffness;
    float m_damping;
    float m_mass;

    mutable float m_currentRGB[3];
    mutable bool m_rgbCacheDirty;

    void updateRGBCache() const;
    void initialiseToDefaults();
};
