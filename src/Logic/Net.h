#pragma once

#include "PinTypes.h"
#include <algorithm>
#include <optional>
#include <vector>

using WireId = int;
constexpr WireId INVALID_WIRE_ID = -1;

using NetId = int;
constexpr NetId INVALID_NET_ID = -1;

struct PinRef {
    int componentId = -1;
    int pinIndex = -1;

    bool isConnected() const { return componentId != -1; }
};

inline bool operator==(const PinRef& a, const PinRef& b) {
    return a.componentId == b.componentId && a.pinIndex == b.pinIndex;
}

inline bool operator!=(const PinRef& a, const PinRef& b) { return !(a == b); }

class Net {
public:
    // Drivers (at most 1 valid driver; >1 signifies a short circuit)
    bool hasDriver() const { return m_drivers.size() == 1; }
    bool shorted() const { return m_drivers.size() > 1; }

    std::optional<PinRef> getDriver() const {
        if (m_drivers.size() == 1) return m_drivers.front();
        return std::nullopt;
    }

    const std::vector<PinRef>& getDrivers() const { return m_drivers; }

    bool addDriver(const PinRef& pin) {
        if (std::find(m_drivers.begin(), m_drivers.end(), pin) == m_drivers.end()) {
            m_drivers.push_back(pin);
            if (m_drivers.size() > 1) {
                m_shorted = true;
                return false;
            }
        }
        return true;
    }

    // Sinks
    const std::vector<PinRef>& getSinks() const { return m_sinks; }

    bool addSink(const PinRef& pin) {
        if (std::find(m_sinks.begin(), m_sinks.end(), pin) != m_sinks.end()) return false;
        m_sinks.push_back(pin);
        return true;
    }

    bool isSink(const PinRef& pin) const {
        return std::find(m_sinks.begin(), m_sinks.end(), pin) != m_sinks.end();
    }

    // Geometry
    const std::vector<WireId>& getGeometry() const { return m_geometry; }

    void addGeometry(WireId id) {
        if (std::find(m_geometry.begin(), m_geometry.end(), id) == m_geometry.end())
            m_geometry.push_back(id);
    }

    // Electrical State
    PinState getState() const { return m_state; }
    void setState(PinState newState) { m_state = newState; }

private:
    std::vector<PinRef> m_drivers;
    std::vector<PinRef> m_sinks;
    PinState            m_state = PinState::DISCONNECTED;
    std::vector<WireId> m_geometry;
    bool                m_shorted = false;
};