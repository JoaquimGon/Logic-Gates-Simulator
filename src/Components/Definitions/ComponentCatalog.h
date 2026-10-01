#pragma once
#include "Components/Definitions/ComponentDefinition.h"

#include <map>
#include <string>
#include <string_view>

class ComponentCatalog
{
  public:
    ComponentCatalog();
    /** @brief Registers validated box definitions referencing supported native behavior.
     * Duplicate IDs and the native.* namespace are rejected before mutation.
     */
    void registerDefinition(ComponentDefinition definition);

    const std::map<std::string, ComponentDefinition, std::less<>>& definitions() const
    {
        return m_definitions;
    }

    const ComponentDefinition* find(std::string_view id) const;
    /** @brief Resolves an exact version, or the registered version when version is zero. */
    ResolvedComponent resolve(
        std::string_view id, const ComponentOverrides& overrides = {}, std::uint32_t version = 0
    ) const;

  private:
    std::map<std::string, ComponentDefinition, std::less<>> m_definitions;
};
