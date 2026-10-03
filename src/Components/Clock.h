#pragma once
#include "Component.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

class Clock : public Component
{
  public:
    std::unique_ptr<Component> clone() const override { return std::make_unique<Clock>(*this); }

    Clock(int id, float frequencyHz = 1.0f) : Component(id, 0, 1) { setFrequency(frequencyHz); }

    void evaluate() override { /* Driven by timer, not inputs */ }

    // -------------------------------------------------------------
    // Speed / Frequency Control
    // -------------------------------------------------------------
    void setFrequency(float hz)
    {
        if (!std::isfinite(hz) || hz <= 0)
            throw std::invalid_argument("Clock frequency must be finite and positive.");
        m_frequencyHz = std::max(0.1f, hz);
        m_halfPeriod = 0.5 / m_frequencyHz;
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
    /** @brief Remaining simulated seconds to the next transition; paused clocks are not due. */
    double timeUntilEdge() const
    {
        return m_paused ? std::numeric_limits<double>::infinity()
                        : std::max(0.0, m_halfPeriod - m_timer);
    }

    /** @brief Advances one chronological slice; Circuit stops each slice at the next clock edge. */
    bool advanceSlice(double deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0)
            throw std::invalid_argument("Clock elapsed time must be finite and non-negative.");
        if (m_paused)
            return false;

        if (deltaTime > timeUntilEdge() + m_halfPeriod * 1e-12)
            throw std::invalid_argument(
                "Clock slices must stop at the next edge; use Circuit::updateClocks."
            );

        m_timer += deltaTime;
        if (m_timer >= m_halfPeriod * (1 - 1e-12))
        {
            m_timer = std::max(0.0, m_timer - m_halfPeriod);
            setStateOutPin(0, !getStateOutPin(0));
            return true; // Edge transition
        }
        return false;
    }

  private:
    float m_frequencyHz = 1.0f;
    double m_halfPeriod = 0.5;
    double m_timer = 0.0;
    bool m_paused = false;
};
