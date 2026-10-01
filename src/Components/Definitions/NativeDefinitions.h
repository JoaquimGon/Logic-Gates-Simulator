#pragma once
#include "Components/Definitions/ComponentDefinition.h"

#include <string>
#include <vector>

namespace BuiltinComponentIds
{
inline constexpr const char* Not = "native.not";
inline constexpr const char* And = "native.and";
inline constexpr const char* Nand = "native.nand";
inline constexpr const char* Or = "native.or";
inline constexpr const char* Nor = "native.nor";
inline constexpr const char* Xor = "native.xor";
inline constexpr const char* Nxor = "native.nxor";
inline constexpr const char* Input = "native.input";
inline constexpr const char* Clock = "native.clock";
inline constexpr const char* SrLatch = "native.sr-latch";
inline constexpr const char* DLatch = "native.d-latch";
} // namespace BuiltinComponentIds

/** @brief The single declaration table for built-in editor defaults, interfaces and assets. */
const std::vector<ComponentDefinition>& nativeDefinitions();
const ShaderResources& boxShaderResources();
const std::string& builtinDefinitionId(GateType type);
const std::string& builtinDefinitionId(LatchType type);
