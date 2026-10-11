#pragma once

#include "Editor/Actions/EditTypes.h"

class Scene;
class Selection;

/** One in-app design clipboard, retained when switching scenes; owns copied settings and shares
 * immutable custom definitions. */
class EditorClipboard
{
  public:
    bool copy(const Scene& scene, const Selection& selection);
    EditBatch pasteBatch(const Scene& destination, GridCoords center) const;

  private:
    GridCoords m_center{};
    std::vector<CreateComponent> m_components;
    std::vector<std::vector<GridCoords>> m_wires;
    std::vector<ComponentDefinition> m_definitions;
};
