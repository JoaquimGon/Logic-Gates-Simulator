#pragma once

using WireId = int;
constexpr WireId INVALID_WIRE_ID = -1;

using NetId = int;
constexpr NetId INVALID_NET_ID = -1;

struct PinRef
{
    int componentId = -1;
    int pinIndex = -1;

    bool isConnected() const { return componentId != -1; }
};

inline bool operator==(const PinRef& a, const PinRef& b)
{
    return a.componentId == b.componentId && a.pinIndex == b.pinIndex;
}

inline bool operator!=(const PinRef& a, const PinRef& b)
{
    return !(a == b);
}
