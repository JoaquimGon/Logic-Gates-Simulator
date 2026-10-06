#pragma once
#include "ComponentView.h"

/** A labeled box with arbitrary input/output pins. */
class BoxView : public ComponentView
{
  public:
    BoxView(
        GridCoords position,
        int id,
        glm::vec2 size,
        std::string shader,
        std::vector<PinUI> inputs,
        std::vector<PinUI> outputs
    )
        : ComponentView(position, id, size, std::move(shader)), m_inputs(std::move(inputs)),
          m_outputs(std::move(outputs))
    {
    }

    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<BoxView>(*this);
    }

    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

  private:
    std::vector<PinUI>& editInputPins() override { return m_inputs; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }

    std::vector<PinUI> m_inputs, m_outputs;
};
