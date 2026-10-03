#pragma once

#include "Components/Definitions/PresentationGeometry.h"
#include "Components/PinTypes.h"

#include <glm/glm.hpp>
#include <span>
#include <string>
#include <vector>

/** Read-only values for one instance; no logic pointers or concrete component classes. */
struct ComponentBodyInstance
{
    int id;
    glm::vec2 position, size;
    std::string shader;
    BodyStyle style;

    BodyBounds getBodyBounds() const
    {
        return bodyBounds(style, position.x, position.y, size.x, size.y);
    }
};

struct RenderPin
{
    PinType direction;
    unsigned int index;
    PinState state;
    glm::vec2 position;
    std::string label;
    std::vector<glm::vec2> lead;
};

struct ComponentRenderData
{
    ComponentBodyInstance body;
    std::string bodyLabel;
    bool showPinLabels;
    std::vector<RenderPin> pins;
};

struct ComponentBatch
{
    std::string shader;
    std::vector<ComponentBodyInstance> instances;
};

/** @brief Orders bodies by stable ID and batches only adjacent equal shaders, preserving blending.
 */
std::vector<ComponentBatch> buildComponentBatches(std::span<const ComponentRenderData> components);

/** @brief Packs position(2), size(2), tint(4) for the component shader's instance attributes. */
std::vector<float> packComponentInstances(std::span<const ComponentBodyInstance> instances);

/** @brief Connects relative lead waypoints (or the anchor) to the declared silhouette. */
std::vector<glm::vec2> buildPinLead(
    const ComponentBodyInstance& body, glm::vec2 anchor, std::span<const glm::vec2> waypoints = {}
);
