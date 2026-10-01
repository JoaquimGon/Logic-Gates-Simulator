#pragma once
#include "Simulation/Circuit.h"
#include "Components/Clock.h"
#include "ComponentView.h"

#include <utility>

class ClockView : public ComponentView
{
  public:
    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<ClockView>(*this);
    }

    ClockView(
        GridCoords gridPos,
        int logicId,
        glm::vec2 size,
        std::string shaderName,
        std::vector<PinUI> outputs
    )
        : ComponentView(gridPos, logicId, size, std::move(shaderName)),
          m_outputs(std::move(outputs))
    {
    }

    const std::vector<PinUI>& getInputPins() const override { return m_empty; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

  private:
    std::vector<PinUI>& editInputPins() override { return m_empty; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }


    std::vector<PinUI> m_outputs;
    std::vector<PinUI> m_empty;
};