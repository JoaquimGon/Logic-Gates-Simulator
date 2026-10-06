#pragma once

#include "Editor/Actions/EditTypes.h"

class Scene;

enum class InterfaceKind
{
    Input,
    Output,
    Clock
};

struct InterfaceComponent
{
    int id;
    InterfaceKind kind;
    std::string name;
};

/** Committed input/output/clock instances, ordered by kind and component ID. */
std::vector<InterfaceComponent> interfaceComponents(const Scene& scene);
bool blankComponentName(const std::string& name);
/** Assigns first available numbered names without replacing existing labels. */
EditBatch missingInterfaceNames(const Scene& scene);
