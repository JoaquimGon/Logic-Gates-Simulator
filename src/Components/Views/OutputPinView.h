#pragma once
#include "ComponentView.h"

/** Passive output bulb; clicking it never changes the incoming signal. */
class OutputPinView : public ComponentView
{
  public:
    OutputPinView(
        GridCoords position, int id, glm::vec2 size, std::string shader, std::vector<PinUI> inputs
    )
        : ComponentView(position, id, size, std::move(shader)), m_inputs(std::move(inputs))
    {
    }

    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<OutputPinView>(*this);
    }

    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_empty; }

  private:
    std::vector<PinUI>& editInputPins() override { return m_inputs; }

    std::vector<PinUI>& editOutputPins() override { return m_empty; }

    std::vector<PinUI> m_inputs, m_empty;
};
