#include "Graphics/Presentation/LabelLayout.h"

#include <algorithm>
#include <cmath>

namespace
{
float fitScale(
    const std::string& text, float width, float height, float maximum, const FontMetrics& font
)
{
    const float naturalWidth = getTextWidth(text, 1, font);
    const float cap = getCapHeight(1, font);
    if (width <= 0 || height <= 0 || naturalWidth <= 0 || cap <= 0)
        return 0;
    return std::min({maximum, width / naturalWidth, height / cap});
}
} // namespace

std::vector<TextRun>
layoutComponentLabels(std::span<const ComponentRenderData> components, const FontMetrics& font)
{
    std::vector<TextRun> runs;
    for (const auto& component : components)
    {
        const auto& body = component.body;
        const auto bounds = body.getBodyBounds();
        const glm::vec2 low{bounds.left, bounds.bottom};
        const glm::vec2 high{bounds.right, bounds.top};
        const float inset = std::min({0.01f, body.size.x * 0.05f, body.size.y * 0.05f});
        if (!component.bodyLabel.empty())
        {
            const bool below = body.style.contour == BodyContour::Input ||
                               body.style.contour == BodyContour::Output;
            float scale = fitScale(
                component.bodyLabel,
                body.size.x * (below ? 2.5f : 0.8f),
                body.size.y * 0.2f,
                0.0012f,
                font
            );
            // Prefer the center; move into a free band when pin labels occupy that row.
            float row = below ? low.y - 0.015f - getCapHeight(scale, font) * 0.5f : body.position.y;
            if (!below && component.showPinLabels && !component.pins.empty())
            {
                float bestClearance = -1;
                for (const float candidate :
                     {body.position.y,
                      body.position.y + body.size.y * 0.3f,
                      body.position.y - body.size.y * 0.3f})
                {
                    float clearance = body.size.y;
                    for (const auto& pin : component.pins)
                        if (!pin.label.empty())
                            clearance = std::min(clearance, std::abs(pin.position.y - candidate));
                    if (clearance > bestClearance)
                    {
                        bestClearance = clearance;
                        row = candidate;
                    }
                }
            }
            if (scale > 0)
                runs.push_back(
                    {component.bodyLabel,
                     {bounds.centerX() - getTextWidth(component.bodyLabel, scale, font) * 0.5f,
                      row - getCapHeight(scale, font) * 0.5f},
                     scale,
                     {1, 1, 1, 0.95f}}
                );
        }
        if (!component.showPinLabels)
            continue;
        for (const auto& pin : component.pins)
        {
            if (pin.label.empty())
                continue;
            const auto relative = (pin.position - body.position) / body.size;
            const bool horizontal = std::abs(relative.x) >= std::abs(relative.y);
            float availableHeight = body.size.y * 0.2f;
            if (horizontal)
                for (const auto& other : component.pins)
                {
                    const float separation = std::abs(other.position.y - pin.position.y);
                    if (separation > 0.00001f)
                        availableHeight = std::min(availableHeight, separation * 0.7f);
                }
            const bool left =
                relative.x < 0 || (relative.x == 0 && pin.direction == PinType::INPUT);
            const float edge =
                horizontal
                    ? std::clamp(
                          pin.position.x + (left ? inset : -inset), low.x + inset, high.x - inset
                      )
                    : body.position.x;
            float width = horizontal ? (left ? high.x - inset - edge : edge - low.x - inset)
                                     : body.size.x - 2 * inset;
            width = std::min(width, body.size.x * 0.38f);
            const float scale = fitScale(pin.label, width, availableHeight, 0.001f, font);
            if (scale <= 0)
                continue;
            const float textWidth = getTextWidth(pin.label, scale, font);
            const float cap = getCapHeight(scale, font);
            glm::vec2 baseline;
            if (horizontal)
                baseline = {
                    left ? edge : edge - textWidth,
                    std::clamp(pin.position.y - cap * 0.5f, low.y + inset, high.y - inset - cap)
                };
            else
                baseline = {
                    std::clamp(
                        pin.position.x - textWidth * 0.5f, low.x + inset, high.x - inset - textWidth
                    ),
                    relative.y > 0
                        ? std::clamp(
                              pin.position.y - inset - cap, low.y + inset, high.y - inset - cap
                          )
                        : std::clamp(pin.position.y + inset, low.y + inset, high.y - inset - cap)
                };
            runs.push_back({pin.label, baseline, scale, {0.75f, 0.85f, 0.95f, 0.85f}});
        }
    }
    return runs;
}
