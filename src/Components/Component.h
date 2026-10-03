#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

/** @brief A directed edge between one output pin and one input pin. */
struct Connection
{
    int srcComponentId;
    int srcPinIndex;
    int destComponentId;
    int destPinIndex;

    bool operator==(const Connection&) const = default;
};

class Circuit;

/**
 * @brief Owns fixed-size, zero-based input/output state for any component shape.
 * @details Counts are set at construction. Invalid pin access throws std::out_of_range.
 */
class Component
{
  public:
    Component(int id, int inputPinCount, int outputPinCount) : m_id(id)
    {
        if (inputPinCount < 0 || outputPinCount < 0)
            throw std::invalid_argument("Component pin counts must be non-negative.");
        m_stateInPins.assign(inputPinCount, false);
        m_stateOutPins.assign(outputPinCount, false);
    }

    virtual ~Component() = default;

    int getId() const { return m_id; }

    /** @brief Copies behavior and retained state for an isolated editor transaction. */
    virtual std::unique_ptr<Component> clone() const = 0;

    virtual void evaluate() = 0;

    bool getStateOutPin(int outIndex = 0) const { return m_stateOutPins.at(outIndex); }

    bool getStateInPin(int pinIndex) const { return m_stateInPins.at(pinIndex); }

    void setStateInPin(int pinIndex, bool state) { m_stateInPins.at(pinIndex) = state; }

    const std::vector<bool>& getStateInPins() const { return m_stateInPins; }

    int getInputPinCount() const { return static_cast<int>(m_stateInPins.size()); }

    int getOutputPinCount() const { return static_cast<int>(m_stateOutPins.size()); }

    /** @brief Clock input index for edge-sensitive components; -1 for gates, latches and sources.
     */
    virtual int clockInputPin() const { return -1; }

    /** @brief Called after a clock input changes: true is rising, false is falling.
     * Inputs at this propagation step are delivered before any edge callbacks run.
     */
    virtual void onClockEdge(bool clockState) {}

    const std::vector<Connection>& getOutConnections() const { return m_outConnections; }

    const std::vector<Connection>& getInConnections() const { return m_inConnections; }

    bool hasConnection() const { return !m_outConnections.empty(); }

  protected:
    void setStateOutPin(int pinIndex, bool state) { m_stateOutPins.at(pinIndex) = state; }

    int m_id;

  private:
    friend class Circuit;

    // Only Circuit mutates edges so both endpoint lists stay consistent.
    void addOutConnection(const Connection& connection) { m_outConnections.push_back(connection); }

    void addInConnection(const Connection& connection) { m_inConnections.push_back(connection); }

    void delOutConnection(const Connection& connection)
    {
        std::erase(m_outConnections, connection);
    }

    void delInConnection(const Connection& connection) { std::erase(m_inConnections, connection); }

    void clearConnections()
    {
        m_outConnections.clear();
        m_inConnections.clear();
    }

    std::vector<bool> m_stateInPins;
    std::vector<bool> m_stateOutPins;
    std::vector<Connection> m_outConnections;
    std::vector<Connection> m_inConnections;
};
