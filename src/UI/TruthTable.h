#pragma once

#include "Components/Views/PinUI.h"

#include <string>
#include <vector>

class Component;

/** Read-only learning data. Rows use external pin levels in binary order. */
struct TruthTable
{
    int inputCount = 0;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> notes;
};

bool hasTruthTable(const Component* component);
TruthTable buildTruthTable(
    const Component& component, const std::vector<PinUI>& inputs, const std::vector<PinUI>& outputs
);
