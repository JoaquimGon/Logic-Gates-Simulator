#pragma once
#include "ComponentView.h"
#include <utility>

class GateView : public ComponentView
{
public:
    GateView(GridCoords gridPos, int gateId, glm::vec2 size, std::string shaderName,
        std::vector<PinUI> inPins, std::vector<PinUI> outPins)
        : ComponentView(gridPos, gateId, size, std::move(shaderName)),
        m_inputs(std::move(inPins)), m_outputs(std::move(outPins)) {
    }

    std::vector<PinUI>& getInputPins() override { return m_inputs; }
    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }
    std::vector<PinUI>& getOutputPins() override { return m_outputs; }
    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    // Kept for any leftover callers expecting single-pin convenience access
    PinUI& getOutputPinUI(int index = 0) { return m_outputs[index]; }
    PinUI& getInputPinUI(int index) { return m_inputs[index]; }

private:
    std::vector<PinUI> m_inputs;
    std::vector<PinUI> m_outputs;
};