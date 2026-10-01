#pragma once
#include "ComponentView.h"

#include <string>
#include <utility>
#include <vector>

class LatchView : public ComponentView
{
  public:
    std::unique_ptr<ComponentView> clone() const override
    {
        return std::make_unique<LatchView>(*this);
    }

    LatchView(
        GridCoords gridPos,
        int id,
        glm::vec2 size,
        std::string shaderName,
        std::string label,
        std::vector<PinUI> inputs,
        std::vector<PinUI> outputs,
        std::vector<std::string> inputLabels,
        std::vector<std::string> outputLabels
    )
        : ComponentView(gridPos, id, size, std::move(shaderName)), m_label(std::move(label)),
          m_inputs(std::move(inputs)), m_outputs(std::move(outputs)),
          m_inputLabels(std::move(inputLabels)), m_outputLabels(std::move(outputLabels))
    {
    }

    const std::vector<PinUI>& getInputPins() const override { return m_inputs; }

    const std::vector<PinUI>& getOutputPins() const override { return m_outputs; }

    const std::string& getLabel() const { return m_label; }

    const std::vector<std::string>& getInputLabels() const { return m_inputLabels; }

    const std::string& getInputLabel(int pinIndex) const { return m_inputLabels.at(pinIndex); }

    const std::vector<std::string>& getOutputLabels() const { return m_outputLabels; }

    const std::string& getOutputLabel(int pinIndex) const { return m_outputLabels.at(pinIndex); }

  private:
    std::vector<PinUI>& editInputPins() override { return m_inputs; }

    std::vector<PinUI>& editOutputPins() override { return m_outputs; }


    std::string m_label;
    std::vector<PinUI> m_inputs;
    std::vector<PinUI> m_outputs;
    std::vector<std::string> m_inputLabels;
    std::vector<std::string> m_outputLabels;
};