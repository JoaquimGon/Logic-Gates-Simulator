#pragma once

#include "Editor/Actions/EditTypes.h"

class Scene;
class Selection;

/** @brief One in-app design clipboard, retained when switching scenes; owns copied settings and shares
 * immutable custom definitions. */
class EditorClipboard
{
  public:
    bool empty() const { return m_components.empty() && m_wires.empty(); }

    /** @brief Capture selected components, reusable definitions and wire geometry relative to a shared paste origin. */
    bool copy(const Scene& scene, const Selection& selection);

    /** @brief Translate clipboard contents into one edit batch, remapping copied identities and definitions. */
    EditBatch pasteBatch(const Scene& destination, GridCoords center) const;

  private:
    GridCoords m_center{};
    std::vector<CreateComponent> m_components;
    std::vector<std::vector<GridCoords>> m_wires;
    std::vector<ComponentDefinition> m_definitions;
};
