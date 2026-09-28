#pragma once
#include "Component.h"

#include <algorithm>

class Clock : public Component
{
  public:
    Clock(int id, float frequencyHz = 1.0f) : Component(id) { setFrequency(frequencyHz); }

    void evaluate() override { /* Driven by timer, not inputs */ }

    bool getStateOutPin(int /*outIndex*/ = 0) const override { return m_state; }

    void setStateInPin(int, bool) override {}

    std::vector<bool> getStateInPins() const override { return {}; }

    int getInputPinCount() const override { return 0; }

    int getOutputPinCount() const override { return 1; }

    bool isClocked() const override { return true; }

    // -------------------------------------------------------------
    // Speed / Frequency Control
    // -------------------------------------------------------------
    void setFrequency(float hz)
    {
        m_frequencyHz = std::max(0.1f, hz);
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
        m_state = !m_state;

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
            m_state = !m_state;
            return true; // Edge transition
        }
        return false;
    }

  private:
    float m_frequencyHz = 1.0f;
    float m_halfPeriod = 0.5f;
    float m_timer = 0.0f;
    bool m_state = false;
    bool m_paused = false;
};