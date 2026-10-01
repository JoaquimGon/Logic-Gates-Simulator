#include "Actions/EditorActions.h"
#include "Components/Views/ClockView.h"
#include "Components/Views/GateView.h"
#include "Components/Views/InputPinView.h"
#include "Components/Views/LatchView.h"
#include "Scene.h"

#include <stdexcept>
#include <utility>

namespace
{
int createdComponent(EditResult result)
{
    if (!result)
        throw std::invalid_argument(result.message);
    return result.createdComponentIds.at(0);
}
} // namespace

Scene::Scene(const Scene& other)
    : m_revision(other.m_revision), m_topologyBuildCount(other.m_topologyBuildCount),
      m_nextPreviewToken(other.m_nextPreviewToken), m_circuit(other.m_circuit),
      m_rejectedConnections(other.m_rejectedConnections), m_topologyResult(other.m_topologyResult),
      m_topologyInvalidNeedsUpdate(other.m_topologyInvalidNeedsUpdate), m_wires(other.m_wires),
      m_nextWireId(other.m_nextWireId), m_nets(other.m_nets), m_nextNetId(other.m_nextNetId),
      m_pinNet(other.m_pinNet)
{
    for (const auto& [id, view] : other.m_componentViews)
        m_componentViews.emplace(id, view->clone());
}

Scene& Scene::operator=(const Scene& other)
{
    if (this != &other)
    {
        Scene copy(other);
        *this = std::move(copy);
    }
    return *this;
}

const ComponentView* Scene::getCommittedComponentView(int componentId) const
{
    auto found = m_componentViews.find(componentId);
    return found != m_componentViews.end() ? found->second.get() : nullptr;
}

int Scene::addGate(
    GateType type,
    GridCoords position,
    glm::vec2 size,
    const std::string& shader,
    std::vector<PinUI> inputs,
    std::vector<PinUI> outputs
)
{
    return createdComponent(EditorActions(*this).apply({CreateGate{
        type,
        position,
        {size, shader, std::move(inputs), std::move(outputs)},
        PlacementPolicy::AllowOverlap
    }}));
}

int Scene::addInputPin(GridCoords position, glm::vec2 size, const std::string& shader, bool state)
{
    return createdComponent(EditorActions(*this).apply(
        {CreateInput{position, size, shader, state, PlacementPolicy::AllowOverlap}}
    ));
}

int Scene::addClock(GridCoords position, glm::vec2 size, const std::string& shader, float frequency)
{
    return createdComponent(EditorActions(*this).apply(
        {CreateClock{position, size, shader, frequency, PlacementPolicy::AllowOverlap}}
    ));
}

int Scene::addLatch(LatchType type, GridCoords position)
{
    return createdComponent(
        EditorActions(*this).apply({CreateLatch{type, position, PlacementPolicy::AllowOverlap}})
    );
}

void Scene::removeComponent(int id)
{
    if (!m_componentViews.contains(id))
        return;
    const auto result = EditorActions(*this).apply({DeleteComponent{id}});
    if (!result)
        throw std::logic_error(result.message);
}

std::optional<WireId> Scene::commitWire(Wire wire)
{
    wire.simplifyPath();
    if (wire.getPath().size() < 2)
        return std::nullopt;
    auto result = EditorActions(*this).apply({AddWire{wire.getPath()}});
    if (!result)
        throw std::invalid_argument(result.message);
    return result.insertedWireIds.at(0);
}

std::optional<Wire> Scene::extractWire(WireId id)
{
    const Wire* wire = getWire(id);
    if (!wire)
        return std::nullopt;
    Wire copy = *wire;
    if (!EditorActions(*this).apply({DeleteWire{id}}))
        return std::nullopt;
    return copy;
}

bool Scene::splitWireAt(WireId id, GridCoords point, Wire& outA, Wire& outB)
{
    const Wire* wire = getWire(id);
    Wire a, b;
    if (!wire || !wire->splitAt(point, a, b) || !EditorActions(*this).apply({DeleteWire{id}}))
        return false;
    outA = std::move(a);
    outB = std::move(b);
    return true;
}

std::pair<WireId, WireId> Scene::addWires(Wire a, Wire b)
{
    auto result = EditorActions(*this).apply({AddWire{a.getPath()}, AddWire{b.getPath()}});
    if (!result)
        throw std::invalid_argument(result.message);
    return {result.insertedWireIds.at(0), result.insertedWireIds.at(1)};
}

bool Scene::removeWire(WireId id)
{
    return static_cast<bool>(EditorActions(*this).apply({DeleteWire{id}}));
}

int Scene::addGateRaw(
    GateType type,
    GridCoords gridPos,
    glm::vec2 size,
    const std::string& shaderName,
    std::vector<PinUI> inputs,
    std::vector<PinUI> outputs
)
{
    int id = m_circuit.addGate(type, static_cast<int>(inputs.size()));
    m_componentViews.emplace(
        id,
        std::make_unique<GateView>(
            gridPos, id, size, shaderName, std::move(inputs), std::move(outputs)
        )
    );

    return id;
}

int Scene::addInputPinRaw(
    GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState
)
{
    int id = m_circuit.addInputPin(initialState);
    m_componentViews.emplace(id, std::make_unique<InputPinView>(gridPos, id, size, shaderName));

    return id;
}

int Scene::addClockRaw(
    GridCoords gridPos, glm::vec2 size, const std::string& shaderName, float frequencyHz
)
{
    int id = m_circuit.addClock(frequencyHz);
    m_componentViews.emplace(id, std::make_unique<ClockView>(gridPos, id, size, shaderName));

    return id;
}

int Scene::addLatchRaw(LatchType type, GridCoords gridPos)
{
    int id = m_circuit.addLatch(type);
    glm::vec2 size = {0.30f, 0.20f}; // 6 x 4 grid cells, the footprint the pins span

    if (type == LatchType::SR_LATCH)
    {
        std::vector<PinUI> inPins = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-3, 1}}, // S
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-3, -1}} // R
        };
        std::vector<PinUI> outPins = {
            {PinType::OUTPUT, 0, PinState::DISCONNECTED, {3, 1}}, // Q
            {PinType::OUTPUT, 1, PinState::DISCONNECTED, {3, -1}} // ~Q
        };
        m_componentViews.emplace(
            id,
            std::make_unique<LatchView>(
                gridPos,
                id,
                size,
                "latch",
                "SR LATCH",
                inPins,
                outPins,
                std::vector<std::string>{"S", "R"},
                std::vector<std::string>{"Q", "~Q"}
            )
        );
    }
    else
    {
        std::vector<PinUI> inPins = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-3, 1}}, // D
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-3, -1}} // E
        };
        std::vector<PinUI> outPins = {
            {PinType::OUTPUT, 0, PinState::DISCONNECTED, {3, 1}}, // Q
            {PinType::OUTPUT, 1, PinState::DISCONNECTED, {3, -1}} // ~Q
        };
        m_componentViews.emplace(
            id,
            std::make_unique<LatchView>(
                gridPos,
                id,
                size,
                "latch",
                "D LATCH",
                inPins,
                outPins,
                std::vector<std::string>{"D", "E"},
                std::vector<std::string>{"Q", "~Q"}
            )
        );
    }

    return id;
}
