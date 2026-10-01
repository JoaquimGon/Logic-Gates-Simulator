#pragma once
#include "Simulation/Circuit.h"
#include "Components/InputPin.h"
#include "ComponentView.h"

#include <utility>

// A manual input switch. One output pin, no inputs. Clicking its body toggles
// the underlying logic InputPin when Interaction mode delegates the click.
// Selection mode moves the body without invoking this handler.
class InputPinView : public ComponentView
{
  public:
    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<InputPinView>(*this);
    }

    InputPinView(GridCoords gridPos, int logicId, glm::vec2 size, std::string shaderName)
        : ComponentView(gridPos, logicId, size, std::move(shaderName))
    {
        m_outputs.push_back(PinUI{PinType::OUTPUT, 0, PinState::OFF, GridCoords{1, 0}});
    }

    const std::vector<PinUI>& getInputPins() const override { return m_empty; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    bool onClick(Circuit& circuit) override
    {
        if (auto* pin = dynamic_cast<InputPin*>(circuit.getComponent(m_logicId)))
        {
            pin->toggle();
            return true; // consumed — Input should not start a drag
        }
        return false;
    }

  private:
    std::vector<PinUI>& editInputPins() override { return m_empty; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }


    std::vector<PinUI> m_outputs;
    std::vector<PinUI> m_empty; // always empty, satisfies the interface
};