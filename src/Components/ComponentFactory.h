#pragma once
#include "Components/Definitions/ComponentCatalog.h"
#include "Components/Views/ComponentLayout.h"
#include "Components/Views/ComponentView.h"
#include "Simulation/Circuit.h"

#include <memory>
#include <string_view>

struct CreatedComponent
{
    int id;
    std::unique_ptr<ComponentView> view;
};

class ComponentFactory
{
  public:
    static ComponentLayout viewLayout(const ResolvedComponent& resolved);
    static void validatePosition(const ComponentView& view, GridCoords position);
    /** @brief Validates/resolves metadata before adding matching logic and presentation. */
    static CreatedComponent create(
        Circuit& circuit,
        const ComponentCatalog& catalog,
        std::string_view definitionId,
        GridCoords position,
        const ComponentOverrides& overrides = {},
        std::uint32_t version = 0
    );
    /** @brief Converts explicit legacy geometry into identity-preserving instance options. */
    static ComponentOverrides layoutOverrides(const ComponentLayout& layout);
    /** @brief Validates layout edits against the instance's definition and stamps stable
     * IDs/labels. */
    static ComponentLayout validateLayout(
        const ComponentCatalog& catalog, const ComponentView& view, const ComponentLayout& layout
    );
};
