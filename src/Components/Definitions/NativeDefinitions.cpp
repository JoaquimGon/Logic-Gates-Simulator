#include "Components/Definitions/NativeDefinitions.h"

#include "Geometry/GridMetrics.h"

#include <stdexcept>

namespace
{
ComponentDefinition gate(
    const char* id,
    const char* name,
    GateType type,
    const char* shader,
    const char* fragment,
    bool inverted = false
)
{
    const bool unary = type == NOT;
    ComponentDefinition definition;
    definition.identity = {id, 1};
    definition.displayName = name;
    definition.behavior = type;
    definition.layout = {
        (inverted ? 6 : 4) * GridMetrics::Spacing, (unary ? 2 : 4) * GridMetrics::Spacing, {}
    };
    definition.layout.pins.push_back({"in.0", "A", PinType::INPUT, 0, {-2, unary ? 0 : 1}, {}});
    if (!unary)
        definition.layout.pins.push_back({"in.1", "B", PinType::INPUT, 1, {-2, -1}, {}});
    definition.layout.pins.push_back(
        {"out", "Y", PinType::OUTPUT, 0, {unary ? 1 : (inverted ? 3 : 2), 0}, {}}
    );
    definition.pinLayoutRule = unary ? PinLayoutRule::Fixed : PinLayoutRule::SymmetricGateInputs;
    definition.presentation.kind = PresentationKind::NativeSdf;
    definition.presentation.shader = {shader, "shaders/components/gates/gate.vert", fragment};
    definition.presentation.body = {
        unary ? BodyContour::Not
              : (type == AND || type == NAND ? BodyContour::And
                 : type == OR || type == NOR ? BodyContour::Or
                                             : BodyContour::Xor),
        inverted,
        unary                         ? std::array<float, 4>{0.1f, 0.75f, 0.75f, 1}
        : type == AND || type == NAND ? std::array<float, 4>{0.2f, 0.5f, 0.9f, 1}
        : type == OR || type == NOR   ? std::array<float, 4>{0.9f, 0.55f, 0.2f, 1}
                                      : std::array<float, 4>{0.7f, 0.3f, 0.85f, 1}
    };
    return definition;
}

ComponentDefinition source(
    const char* id,
    const char* name,
    NativeBehavior behavior,
    const char* shader,
    const char* fragment
)
{
    ComponentDefinition definition;
    definition.identity = {id, 1};
    definition.displayName = name;
    definition.behavior = behavior;
    definition.layout = {
        3 * GridMetrics::Spacing,
        3 * GridMetrics::Spacing,
        {{"out", "Out", PinType::OUTPUT, 0, {1, 0}, {}}}
    };
    definition.presentation.kind = PresentationKind::NativeSdf;
    definition.presentation.shader = {shader, "shaders/components/gates/gate.vert", fragment};
    const bool clock = std::holds_alternative<ClockBehavior>(behavior);
    definition.presentation.body = {
        clock ? BodyContour::Clock : BodyContour::Input,
        false,
        clock ? std::array<float, 4>{0.18f, 0.65f, 0.60f, 1}
              : std::array<float, 4>{0.85f, 0.82f, 0.25f, 1}
    };
    return definition;
}

ComponentDefinition latch(const char* id, const char* name, LatchType type)
{
    const bool sr = type == LatchType::SR_LATCH;
    ComponentDefinition definition;
    definition.identity = {id, 1};
    definition.displayName = name;
    definition.behavior = type;
    definition.layout = {
        6 * GridMetrics::Spacing,
        4 * GridMetrics::Spacing,
        {{sr ? "s" : "d", sr ? "S" : "D", PinType::INPUT, 0, {-3, 1}, {}},
         {sr ? "r" : "enable", sr ? "R" : "E", PinType::INPUT, 1, {-3, -1}, {}},
         {"q", "Q", PinType::OUTPUT, 0, {3, 1}, {}},
         {"not-q", "~Q", PinType::OUTPUT, 1, {3, -1}, {}}}
    };
    definition.presentation.kind = PresentationKind::NativeSdf;
    definition.presentation.shader = {
        "latch", "shaders/components/gates/gate.vert", "shaders/components/latches/latch.frag"
    };
    definition.presentation.bodyLabel = name;
    definition.presentation.showPinLabels = true;
    return definition;
}

ComponentDefinition output()
{
    ComponentDefinition definition;
    definition.identity = {BuiltinComponentIds::Output, 1};
    definition.displayName = "Output";
    definition.behavior = OutputBehavior{};
    definition.layout = {
        3 * GridMetrics::Spacing,
        3 * GridMetrics::Spacing,
        {{"in", "Signal", PinType::INPUT, 0, {-1, 0}, {}}}
    };
    definition.presentation.kind = PresentationKind::NativeSdf;
    definition.presentation.shader = {
        "outputPin", "shaders/components/gates/gate.vert", "shaders/components/outputPin.frag"
    };
    definition.presentation.body = {BodyContour::Output, false, {0, 1, 0, 1}};
    return definition;
}
} // namespace

const std::vector<ComponentDefinition>& nativeDefinitions()
{
    static const std::vector<ComponentDefinition> definitions{
        source(
            BuiltinComponentIds::Input,
            "Input",
            ManualInputBehavior{},
            "inputPin",
            "shaders/components/inputPin.frag"
        ),
        output(),
        source(
            BuiltinComponentIds::Clock,
            "Clock",
            ClockBehavior{},
            "clock",
            "shaders/components/clock.frag"
        ),
        gate(
            BuiltinComponentIds::Not, "NOT", NOT, "NOTgate", "shaders/components/gates/notGate.frag"
        ),
        gate(
            BuiltinComponentIds::And, "AND", AND, "ANDgate", "shaders/components/gates/andGate.frag"
        ),
        gate(
            BuiltinComponentIds::Nand,
            "NAND",
            NAND,
            "NANDgate",
            "shaders/components/gates/nandGate.frag",
            true
        ),
        gate(BuiltinComponentIds::Or, "OR", OR, "ORgate", "shaders/components/gates/orGate.frag"),
        gate(
            BuiltinComponentIds::Nor,
            "NOR",
            NOR,
            "NORgate",
            "shaders/components/gates/norGate.frag",
            true
        ),
        gate(
            BuiltinComponentIds::Xor, "XOR", XOR, "XORgate", "shaders/components/gates/xorGate.frag"
        ),
        gate(
            BuiltinComponentIds::Nxor,
            "NXOR",
            NXOR,
            "NXORgate",
            "shaders/components/gates/nxorGate.frag",
            true
        ),
        latch(BuiltinComponentIds::SrLatch, "SR LATCH", LatchType::SR_LATCH),
        latch(BuiltinComponentIds::DLatch, "D LATCH", LatchType::D_LATCH)
    };
    return definitions;
}

const ShaderResources& boxShaderResources()
{
    static const ShaderResources resources{
        "box", "shaders/components/gates/gate.vert", "shaders/components/latches/latch.frag"
    };
    return resources;
}

template <typename T>
const std::string& nativeId(T type)
{
    for (const auto& definition : nativeDefinitions())
        if (const auto* behavior = std::get_if<T>(&definition.behavior);
            behavior && *behavior == type)
            return definition.identity.id;
    throw std::invalid_argument("Unknown native behavior.");
}

const std::string& builtinDefinitionId(GateType type)
{
    return nativeId(type);
}

const std::string& builtinDefinitionId(LatchType type)
{
    return nativeId(type);
}
