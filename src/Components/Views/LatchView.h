#pragma once
#include "ComponentView.h"

#include <stdexcept>
#include <utility>

class LatchView : public ComponentView
{
  public:
    LatchView(
        GridCoords gridPos,
        int id,
        glm::vec2 size,
        std::string shaderName,
        std::vector<PinUI> inputs,
        std::vector<PinUI> outputs
    )
        : ComponentView(gridPos, id, size, std::move(shaderName)), m_inputs(std::move(inputs)),
          m_outputs(std::move(outputs))
    {
    }

    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<LatchView>(*this);
    }

    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    const std::string& getLabel() const { return getBodyLabel(); }

    const std::string& getInputLabel(int index) const { return pinLabel(m_inputs, index); }

    const std::string& getOutputLabel(int index) const { return pinLabel(m_outputs, index); }

  private:
    static const std::string& pinLabel(const std::vector<PinUI>& pins, int index)
    {
        for (const auto& pin : pins)
            if (index >= 0 && pin.pin_index == static_cast<unsigned int>(index))
                return pin.label;
        throw std::out_of_range("Missing latch pin label.");
    }

    std::vector<PinUI>& editInputPins() override { return m_inputs; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }

    std::vector<PinUI> m_inputs, m_outputs;
};
