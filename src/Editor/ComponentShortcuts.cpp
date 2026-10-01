#include "ComponentShortcuts.h"

#include "Components/Definitions/NativeDefinitions.h"

#include <GLFW/glfw3.h>

std::optional<EditOperation> componentShortcut(int key, GridCoords position)
{
    const char* definitionId;
    switch (key)
    {
    case GLFW_KEY_1:
        definitionId = BuiltinComponentIds::Input;
        break;
    case GLFW_KEY_2:
        definitionId = BuiltinComponentIds::Not;
        break;
    case GLFW_KEY_3:
        definitionId = BuiltinComponentIds::And;
        break;
    case GLFW_KEY_4:
        definitionId = BuiltinComponentIds::Nand;
        break;
    case GLFW_KEY_5:
        definitionId = BuiltinComponentIds::Or;
        break;
    case GLFW_KEY_6:
        definitionId = BuiltinComponentIds::Nor;
        break;
    case GLFW_KEY_7:
        definitionId = BuiltinComponentIds::Xor;
        break;
    case GLFW_KEY_8:
        definitionId = BuiltinComponentIds::Nxor;
        break;
    case GLFW_KEY_9:
        definitionId = BuiltinComponentIds::Clock;
        break;
    case GLFW_KEY_U:
        definitionId = BuiltinComponentIds::SrLatch;
        break;
    case GLFW_KEY_I:
        definitionId = BuiltinComponentIds::DLatch;
        break;
    default:
        return std::nullopt;
    }
    return CreateComponent{definitionId, position, {}, PlacementPolicy::FindFree};
}
