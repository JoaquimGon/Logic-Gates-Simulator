#include "Components/Definitions/ComponentCatalog.h"

#include "Components/Definitions/NativeDefinitions.h"

#include <stdexcept>
#include <utility>

ComponentCatalog::ComponentCatalog()
{
    std::map<std::string, ShaderResources> shaders;
    for (const auto& definition : nativeDefinitions())
    {
        validateDefinition(definition);
        if (!definition.identity.id.starts_with("native.") ||
            !m_definitions.emplace(definition.identity.id, definition).second)
            throw std::invalid_argument("Built-in definitions require distinct native.* IDs.");
        const auto& resources = definition.presentation.shader;
        const auto [existing, inserted] = shaders.emplace(resources.key, resources);
        if (!inserted && (existing->second.vertexPath != resources.vertexPath ||
                          existing->second.fragmentPath != resources.fragmentPath))
            throw std::invalid_argument(
                "A shader key cannot identify different component resources."
            );
    }
}

void ComponentCatalog::registerDefinition(ComponentDefinition definition)
{
    validateDefinition(definition);
    if (definition.identity.id.starts_with("native.") ||
        m_definitions.contains(definition.identity.id) ||
        definition.presentation.kind != PresentationKind::Box)
        throw std::invalid_argument(
            "Custom definitions require a new non-native ID and box presentation."
        );
    const auto id = definition.identity.id;
    m_definitions.emplace(id, std::move(definition));
}

const ComponentDefinition* ComponentCatalog::find(std::string_view id) const
{
    auto found = m_definitions.find(id);
    return found == m_definitions.end() ? nullptr : &found->second;
}

void ComponentCatalog::replaceDefinition(ComponentDefinition definition)
{
    validateDefinition(definition);
    if (definition.identity.id.starts_with("native.") ||
        definition.presentation.kind != PresentationKind::Box)
        throw std::invalid_argument("Only custom box definitions can be replaced.");
    const auto id = definition.identity.id;
    m_definitions.insert_or_assign(id, std::move(definition));
}

ResolvedComponent ComponentCatalog::resolve(
    std::string_view id, const ComponentOverrides& overrides, std::uint32_t version
) const
{
    const auto* definition = find(id);
    if (!definition)
        throw std::invalid_argument("Unknown component definition: " + std::string(id));
    if (version != 0 && version != definition->identity.version)
        throw std::invalid_argument("Unsupported component definition version: " + std::string(id));
    return resolveDefinition(*definition, overrides);
}
