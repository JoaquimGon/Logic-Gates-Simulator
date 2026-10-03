#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Graphics/Presentation/ComponentPresentation.h"
#include "Graphics/Presentation/LabelLayout.h"
#include "Graphics/Presentation/OverlayPresentation.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

FontMetrics fontMetrics()
{
    FontMetrics font;
    for (auto& glyph : font.cdata)
        glyph = {0, 0, 24, 32, 0, -32, 24};
    return font;
}

void bodyGeometry();

void instances()
{
    bodyGeometry();
    Scene scene;
    const int first = scene.addComponent(BuiltinComponentIds::And, {-10, 0});
    const int second = scene.addComponent(BuiltinComponentIds::And, {10, 0}, {.inputCount = 6});
    auto data = buildComponentPresentation(scene.getComponentViewMap());
    const auto batches = buildComponentBatches(data);
    require(
        batches.size() == 1 && batches[0].instances.size() == 2,
        "Equal adjacent shaders lost instancing."
    );
    const auto packed = packComponentInstances(batches[0].instances);
    require(
        packed.size() == 16 && packed[3] == 0.2f && packed[11] == 0.6f,
        "Mixed-size instances still share one body size."
    );
    require(data[0].body.id == first && data[1].body.id == second, "View adaptation is unordered.");
    ComponentRenderData a{{3, {0, 0}, {1, 1}, "A", {}}, "", false, {}};
    ComponentRenderData b = a, c = a;
    b.body.id = 2;
    b.body.shader = "B";
    c.body.id = 1;
    std::vector<ComponentRenderData> layered{a, b, c};
    const auto layers = buildComponentBatches(layered);
    require(
        layers.size() == 3 && layers[0].instances[0].id == 1 && layers[1].instances[0].id == 2 &&
            layers[2].instances[0].id == 3,
        "Batch regrouping changed compositing order."
    );
    std::reverse(layered.begin(), layered.end());
    const auto reversed = buildComponentBatches(layered);
    require(
        reversed[0].instances[0].id == 1 && reversed[2].instances[0].id == 3,
        "Container insertion order changed compositing."
    );
    const auto stored = data[0].body.position;
    EditorActions actions(scene);
    auto preview = actions.beginMove(first);
    require(preview && actions.previewMove(*preview, {-5, 5}), "Move preview failed.");
    const auto previewData = buildComponentPresentation(scene.getComponentViewMap());
    require(
        previewData[0].body.position != stored && data[0].body.position == stored,
        "Presentation values borrow mutable views or ignore preview state."
    );
    actions.cancelMove(*preview);
}

void bodyGeometry()
{
    Scene scene;
    auto layout = scene.getComponentCatalog().find(BuiltinComponentIds::Nand)->layout;
    layout.width = 0.173f;
    layout.height = 0.137f;
    const int id = scene.addComponent(BuiltinComponentIds::Nand, {5, -3}, {.layout = layout});
    const auto* view = scene.getCommittedComponentView(id);
    const auto bounds = view->getBodyBounds();
    require(
        view->getSize() == glm::vec2(0.173f, 0.137f), "Fractional size was rounded to grid cells."
    );
    require(
        bounds.centerX() > view->getPosition().x && bounds.width() < view->getSize().x,
        "Inversion bubble bounds still assume a symmetric shader quad."
    );
    const auto pin = view->getAbsolutePinGridPos(view->getOutputPins().front());
    require(pin == GridCoords{8, -3}, "Visual bounds changed the electrical grid anchor.");
    EditorActions actions(scene);
    const auto result = actions.apply({MoveComponent{id, {8, -1}}});
    require(static_cast<bool>(result), "Fractional component failed to move.");
    const auto moved = scene.getCommittedComponentView(id)->getBodyBounds();
    require(
        std::abs(moved.left - bounds.left - 0.15f) < 0.000001f &&
            std::abs(moved.bottom - bounds.bottom - 0.1f) < 0.000001f,
        "Body offset was lost during a grid move."
    );
    require(
        static_cast<bool>(actions.restore(*result.change->before, scene.getRevision())),
        "Bounds snapshot failed to restore."
    );
    require(
        scene.getCommittedComponentView(id)->getBodyBounds().right == bounds.right,
        "Restoration lost fractional/asymmetric bounds."
    );
    const int unary = scene.addComponent(BuiltinComponentIds::Not, {-10, 0});
    require(
        scene.hitTest({-0.42f, 0}, {-8, 0}).type == HitType::NONE,
        "Empty NOT shader margin remained a body target."
    );
    const auto unaryBounds = scene.getCommittedComponentView(unary)->getBodyBounds();
    require(
        std::abs(unaryBounds.right + 0.45f) < 0.000001f,
        "NOT bounds excluded its bubble or included the empty output margin."
    );
    for (const auto& definition : nativeDefinitions())
    {
        const auto& style = definition.presentation.body;
        const auto local = normalizedBodyBounds(style);
        for (int ix = -100; ix <= 100; ++ix)
            for (int iy = -100; iy <= 100; ++iy)
            {
                const float x = ix / 200.0f, y = iy / 200.0f;
                if (bodyContourDistance(style, x, y) < -0.00001f)
                    require(
                        x >= local.left - 0.00001f && x <= local.right + 0.00001f &&
                            y >= local.bottom - 0.00001f && y <= local.top + 0.00001f,
                        "Visible body extends outside its declared bounds."
                    );
            }
    }
}

void leads()
{
    Scene scene;
    int x = 0;
    for (const auto& definition : nativeDefinitions())
    {
        scene.addComponent(definition.identity.id, {x, 0});
        x += 20;
    }
    for (int count : {3, 4, 6, 50, 255})
        for (auto id :
             {BuiltinComponentIds::And,
              BuiltinComponentIds::Or,
              BuiltinComponentIds::Xor,
              BuiltinComponentIds::Nand,
              BuiltinComponentIds::Nor,
              BuiltinComponentIds::Nxor})
        {
            scene.addComponent(id, {x, 0}, {.inputCount = count});
            x += 20;
        }
    const auto data = buildComponentPresentation(scene.getComponentViewMap());
    for (const auto& component : data)
        for (const auto& pin : component.pins)
        {
            require(
                pin.lead.size() >= 2 && pin.lead.back() == pin.position,
                "Lead lost its actual electrical anchor."
            );
            const auto contact = (pin.lead.front() - component.body.position) / component.body.size;
            require(
                bodyContourDistance(component.body.style, contact.x, contact.y) <= 0.00002f,
                "Lead floats outside the declared silhouette."
            );
            require(
                std::abs(contact.x) <= 0.5001f && std::abs(contact.y) <= 0.5001f,
                "Native contact extends past placement bounds."
            );
        }
    ComponentDefinition box;
    box.identity = {"custom.routed", 1};
    box.displayName = "Routed box";
    box.behavior = NOT;
    box.presentation.shader = boxShaderResources();
    box.layout = {
        0.3f,
        0.2f,
        {{"in", "Data", PinType::INPUT, 0, {-5, 2}, {{-3, 1}, {-5, 1}, {-5, 2}}},
         {"out", "Result", PinType::OUTPUT, 0, {3, 0}, {}}}
    };
    EditorActions actions(scene);
    auto result = actions.apply(
        {RegisterComponentDefinition{box}, CreateComponent{box.identity.id, {1000, 0}}}
    );
    require(static_cast<bool>(result), "Valid orthogonal lead was rejected.");
    const int instance = result.createdComponentIds[0];
    const auto routed = buildComponentPresentation(scene.getComponentViewMap());
    const auto& pin = routed.back().pins[0];
    require(
        pin.lead.size() == 4 && pin.lead[1] == GridSystem::gridToWorld({997, 1}) &&
            pin.lead.back() == GridSystem::gridToWorld({995, 2}),
        "Explicit lead route was discarded."
    );
    auto layout = ComponentLayout{
        scene.getComponentView(instance)->getSize(),
        "box",
        scene.getComponentView(instance)->getInputPins(),
        scene.getComponentView(instance)->getOutputPins()
    };
    layout.inputs[0].lead = {{-3, 0}, {-5, 0}, {-5, 2}};
    auto edit = actions.apply({ConfigureComponent{instance, layout}});
    require(edit && edit.change, "Lead-only edit was treated as a no-op.");
    require(
        static_cast<bool>(actions.restore(*edit.change->before, scene.getRevision())),
        "Lead restoration failed."
    );
    require(
        scene.getComponentView(instance)->getInputPins()[0].lead == box.layout.pins[0].lead,
        "Snapshot lost lead geometry."
    );
    layout.inputs[0].lead[1] = {-4, 1};
    require(!actions.apply({ConfigureComponent{instance, layout}}), "Diagonal lead accepted.");
    layout.inputs[0].lead = {{10, 0}, {-5, 0}, {-5, 2}};
    require(
        static_cast<bool>(actions.apply({ConfigureComponent{instance, layout}})),
        "Lead edit failed."
    );
    require(
        !actions.apply({MoveComponent{instance, {std::numeric_limits<int>::max() - 3, 0}}}),
        "Move overflowed a lead while its pins remained valid."
    );
}

void text()
{
    const auto font = fontMetrics();
    std::vector<TextVertex> world, screen;
    buildTextGeometry("H", 10, 20, 0.25f, {1, 1, 1, 1}, font, world);
    buildTextGeometry("H", 10, 20, 0.25f, {1, 1, 1, 1}, font, screen, TextSpace::Screen);
    require(
        world.size() == 6 && world[0].pos.x == 10 && world[0].pos.y == 28 && screen[0].pos.y == 12,
        "World/screen glyph coordinates diverged from their baselines."
    );
    require(packTextVertices(world).size() == 48, "Text vertex packing changed.");
    ComponentRenderData box{
        {1, {2, 3}, {0.3f, 0.2f}, "box", {}},
        "A LONG BODY LABEL",
        true,
        {{PinType::INPUT, 0, PinState::OFF, {1.85f, 3}, "A very long input label", {}},
         {PinType::OUTPUT, 0, PinState::ON, {2.15f, 3}, "Output", {}},
         {PinType::INPUT, 1, PinState::OFF, {2, 3.1f}, "Top", {}},
         {PinType::OUTPUT, 1, PinState::OFF, {2, 2.9f}, "Bottom", {}}}
    };
    const std::vector<ComponentRenderData> data{box};
    const auto labels = layoutComponentLabels(data, font);
    require(labels.size() == 5, "Generic body or differently oriented pin labels were omitted.");
    for (const auto& run : labels)
        require(
            run.scale > 0 && run.baseline.x >= 1.85f &&
                run.baseline.x + getTextWidth(run.text, run.scale, font) <= 2.15001f &&
                run.baseline.y >= 2.9f &&
                run.baseline.y + getCapHeight(run.scale, font) <= 3.10001f,
            "Label overflowed its component bounds."
        );
    for (const auto contour : {BodyContour::Input, BodyContour::Output})
    {
        auto port = box;
        port.body.style.contour = contour;
        port.showPinLabels = false;
        const auto named = layoutComponentLabels(std::vector<ComponentRenderData>{port}, font);
        require(
            named.size() == 1 &&
                named[0].baseline.y + getCapHeight(named[0].scale, font) < bodyBounds(
                                                                               port.body.style,
                                                                               port.body.position.x,
                                                                               port.body.position.y,
                                                                               port.body.size.x,
                                                                               port.body.size.y
                                                                           )
                                                                               .bottom,
            "Input/output name covered the symbol instead of appearing beneath it."
        );
    }
    DebugMetrics metrics{};
    metrics.evalResult = SimulationResult::OK;
    require(layoutDebugOverlay(metrics, false, 800, 600, font).empty(), "F3 hid no metrics.");
    for (auto result : {SimulationResult::NON_CONVERGENT, SimulationResult::CONNECTION_REJECTED})
    {
        metrics.evalResult = result;
        const auto warning = layoutDebugOverlay(metrics, false, 800, 600, font);
        require(
            warning.size() == 2 && warning[0].text.find("SIMULATION PAUSED") != std::string::npos,
            "F3 suppressed the wiring warning."
        );
        require(
            warning[0].baseline.x + getTextWidth(warning[0].text, warning[0].scale, font) == 784,
            "Overlay stopped right-aligning in screen pixels."
        );
    }
    require(
        layoutDebugOverlay(metrics, true, 0, 0, font).empty(),
        "Minimized canvas emitted overlay text."
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::pair<const char*, void (*)()> groups[] = {
            {"render_instance_contract", instances},
            {"component_pin_leads", leads},
            {"text_presentation", text}
        };
        bool matched = false;
        for (const auto& [name, run] : groups)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                matched = true;
                std::cout << "PASS: " << name << '\n';
            }
        require(matched, "Unknown presentation test.");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
