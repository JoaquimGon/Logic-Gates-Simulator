#pragma once
#include "ComponentView.h"

#include <utility>

class GateView : public ComponentView
{
  public:
    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<GateView>(*this);
    }

    GateView(
        GridCoords gridPos,
        int gateId,
        glm::vec2 size,
        std::string shaderName,
        std::vector<PinUI> inPins,
        std::vector<PinUI> outPins
    )
        : ComponentView(gridPos, gateId, size, std::move(shaderName)), m_inputs(std::move(inPins)),
          m_outputs(std::move(outPins))
    {
    }

    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    // Kept for any leftover callers expecting single-pin convenience access
    const PinUI& getOutputPinUI(int index = 0) const { return m_outputs.at(index); }

    const PinUI& getInputPinUI(int index) const { return m_inputs.at(index); }

  private:
    std::vector<PinUI>& editInputPins() override { return m_inputs; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }


    std::vector<PinUI> m_inputs;
    std::vector<PinUI> m_outputs;
};