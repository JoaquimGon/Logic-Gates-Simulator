#pragma once
#include "ComponentView.h"
#include "../Logic/Circuit.h"
#include "../Logic/InputPin.h"

// A manual input switch. One output pin, no inputs. Clicking its body toggles the
// underlying logic InputPin instead of starting a wire-drag on the gate body.
class InputPinView : public ComponentView
{
public:
    InputPinView(GridCoords gridPos, int logicId, glm::vec2 size, std::string shaderName)
        : ComponentView(gridPos, logicId, size, std::move(shaderName))
    {
        m_outputs.push_back(PinUI{ PinType::OUTPUT, 0, PinState::OFF, GridCoords{1, 0} });
    }

    std::vector<PinUI>& getInputPins() override { return m_empty; }
    const std::vector<PinUI>& getInputPins() const override { return m_empty; }
    std::vector<PinUI>& getOutputPins() override { return m_outputs; }
    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    bool onClick(Circuit& circuit) override
    {
        if (auto* pin = dynamic_cast<InputPin*>(circuit.getComponent(m_logicId))) {
            pin->toggle();
            return true; // consumed — Input should not start a drag
        }
        return false;
    }

private:
    std::vector<PinUI> m_outputs;
    std::vector<PinUI> m_empty; // always empty, satisfies the interface
};