#pragma once
#include "Components/Views/PinUI.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct ComponentLayout
{
    glm::vec2 size;
    std::string shader;
    std::vector<PinUI> inputs;
    std::vector<PinUI> outputs;
};

using GateLayout = ComponentLayout;
