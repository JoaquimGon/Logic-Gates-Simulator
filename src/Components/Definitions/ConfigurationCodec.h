#pragma once
#include "Components/Definitions/ComponentDefinition.h"

#include <string>
#include <string_view>

class ComponentCatalog;

/** Portable design record; positions, wires, catalog declarations, and runtime state are separate.
 */
struct ComponentConfigurationRecord
{
    DefinitionIdentity definition;
    ComponentConfiguration configuration;
};

/** @brief Encodes a validated configuration deterministically using the version-1 typed text
 * format. */
std::string
encodeConfiguration(const ComponentCatalog& catalog, const ComponentConfigurationRecord& record);
/** @brief Rejects malformed/unsupported records and validates exact definition/schema
 * compatibility. */
ComponentConfigurationRecord
decodeConfiguration(const ComponentCatalog& catalog, std::string_view text);
