#pragma once
#include "Component.h"

#include <algorithm>

class Clock : public Component
{
  public:
    static constexpr float MINIMUM_FREQUENCY_HZ = 0.1f;

    std::unique_ptr<Component> clone() const override { return std::make_unique<Clock>(*this); }

    Clock(int id, float frequencyHz = 1.0f) : Component(id, 0, 1) { setFrequency(frequencyHz); }

    void evaluate() override { /* Driven by timer, not inputs */ }

    bool isClocked() const override { return true; }

    // -------------------------------------------------------------
    // Speed / Frequency Control
    // -------------------------------------------------------------
    void setFrequency(float hz)
    {
        m_frequencyHz = std::max(MINIMUM_FREQUENCY_HZ, hz);
        m_halfPeriod = 0.5f / m_frequencyHz;
    }

    float getFrequency() const { return m_frequencyHz; }

    // -------------------------------------------------------------
    // Play / Pause Control
    // -------------------------------------------------------------
    void togglePause() { m_paused = !m_paused; }

    void setPaused(bool paused) { m_paused = paused; }

    bool isPaused() const { return m_paused; }

    // -------------------------------------------------------------
    // Manual Stepping (forces an edge even when paused)
    // -------------------------------------------------------------
    bool step()
    {
        setStateOutPin(0, !getStateOutPin(0));

        // Pause the clock otherwise it will instantly overwrite
        setPaused(true);
        return true;
    }

    // -------------------------------------------------------------
    // Time Accumulator
    // -------------------------------------------------------------
    bool advanceTime(float deltaTime)
    {
        if (m_paused)
            return false;

        m_timer += deltaTime;
        if (m_timer >= m_halfPeriod)
        {
            m_timer -= m_halfPeriod;
            setStateOutPin(0, !getStateOutPin(0));
            return true; // Edge transition
        }
        return false;
    }

  private:
    float m_frequencyHz = 1.0f;
    float m_halfPeriod = 0.5f;
    float m_timer = 0.0f;
    bool m_paused = false;
};