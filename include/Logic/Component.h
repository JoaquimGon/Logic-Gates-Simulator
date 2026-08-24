#pragma once
#include <vector>

struct Connection {
    int gateId;
    int pinIndex;
};

class Component
{
public:
    explicit Component(int id) : m_id(id) {}
    virtual ~Component() = default;

    int getId() const { return m_id; }

    // ----- Simulation contract every component must implement -----
    virtual void evaluate() = 0;                                  // recompute output(s) from input(s)
    virtual bool getStateOutPin(int outIndex = 0) const = 0;
    virtual void setStateInPin(int pinIndex, bool state) = 0;
    virtual std::vector<bool> getStateInPins() const = 0;
    virtual int getInputPinCount()  const = 0;
    virtual int getOutputPinCount() const = 0;

    // ----- Connections: identical for every component, so NOT virtual -----
    void addOutConnection(int gateId, int pinIndex) { m_outConnections.push_back({ gateId, pinIndex }); }
    void addInConnection(int gateId, int pinIndex) { m_inConnections.push_back({ gateId, pinIndex }); }
    void delOutConnection(int destGateId, int destPinIndex) {
        std::erase_if(m_outConnections, [&](const Connection& c) {
            return c.gateId == destGateId && c.pinIndex == destPinIndex;
            });
    }
    void delInConnection(int srcGateId, int srcPinIndex) {
        std::erase_if(m_inConnections, [&](const Connection& c) {
            return c.gateId == srcGateId && c.pinIndex == srcPinIndex;
            });
    }
    const std::vector<Connection>& getOutConnections() const { return m_outConnections; }
    const std::vector<Connection>& getInConnections()  const { return m_inConnections; }
    bool hasConnection() const { return !m_outConnections.empty(); }

protected:
    int m_id;
    std::vector<Connection> m_outConnections;
    std::vector<Connection> m_inConnections;
};