#pragma once
#include "Simulation/Circuit.h"
#include "Components/Clock.h"
#include "ComponentView.h"

#include <utility>

class ClockView : public ComponentView
{
  public:
    ClockView(GridCoords gridPos, int logicId, glm::vec2 size, std::string shaderName)
        : ComponentView(gridPos, logicId, size, std::move(shaderName))
    {
        // 1 output pin sitting 1 cell to the right
        m_outputs.push_back(PinUI{PinType::OUTPUT, 0, PinState::OFF, GridCoords{1, 0}});
    }

    std::vector<PinUI>& getInputPins() override { return m_empty; }

    const std::vector<PinUI>& getInputPins() const override { return m_empty; }

    std::vector<PinUI>& getOutputPins() override { return m_outputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

  private:
    std::vector<PinUI> m_outputs;
    std::vector<PinUI> m_empty;
};