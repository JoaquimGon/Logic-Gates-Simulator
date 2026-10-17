#include "App/CircuitFiles.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Graphics/Text/TextGeometry.h"
#include "Persistence/CircuitFile.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}


int named(const Scene& scene, const char* label, const char* definition = nullptr)
{
    for (const auto& [id, view] : scene.getComponentViewMap())
        if (view->getBodyLabel() == label && (!definition || view->getDefinitionIdentity().id == definition))
            return id;
    throw std::runtime_error(std::string("Missing example label: ") + label);
}


bool output(const Scene& scene, const char* label)
{
    return scene.getLogicComponent(named(scene, label, BuiltinComponentIds::Output))->getStateInPin(0);
}


void set(Scene& scene, const char* label, bool value)
{
    const int id = named(scene, label);
    if (scene.getLogicComponent(id)->getStateOutPin(0) != value)
        scene.handleClick(id);
}


/** @brief Verify logic-gate and adder examples against every expected input combination. */
void combinational(Scene& scene, CircuitExample example)
{
    for (int inputs = 0; inputs < (example == CircuitExample::FullAdder ? 8 : 4); ++inputs)
    {
        const bool a = inputs & 1, b = inputs & 2, carry = inputs & 4;
        set(scene, "A", a);
        set(scene, "B", b);
        if (example == CircuitExample::FullAdder)
            set(scene, "Carry in", carry);
        require(
            scene.propagate() == SimulationResult::OK && scene.getShortedNetCount() == 0,
            "Example did not settle without short circuits."
        );
        if (example == CircuitExample::LogicGates)
            require(
                output(scene, "A AND B") == (a && b) && output(scene, "A OR B") == (a || b) &&
                    output(scene, "A XOR B") == (a != b),
                "Logic-gate example has an incorrect truth table."
            );
        else
        {
            const int sum = int(a) + int(b) + (example == CircuitExample::FullAdder ? int(carry) : 0);
            require(
                output(scene, "Sum") == bool(sum & 1) &&
                    output(scene, example == CircuitExample::FullAdder ? "Carry out" : "Carry") == bool(sum & 2),
                "Adder example has an incorrect truth table."
            );
        }
    }
}


/** @brief Verify all example scenes use valid topology and the sequential example follows its expected count sequence.
 */
void circuits()
{
    require(circuitExamples().size() == 4, "Unexpected example catalog.");
    for (const auto& info : circuitExamples())
    {
        auto scene = buildExampleCircuit(info.id);
        require(
            scene.getLastEvalResult() == SimulationResult::OK && scene.getShortedNetCount() == 0,
            "Example opened with a simulation error."
        );
        auto loaded = circuitFromJson(circuitToJson(scene, std::string(info.name)));
        require(loaded.scene.getComponentCount() == scene.getComponentCount(), "Example did not survive save/reopen.");
        if (info.id != CircuitExample::Counter)
        {
            combinational(scene, info.id);
            combinational(loaded.scene, info.id);
        }
        else
        {
            require(
                scene.clocksPaused() && !output(scene, "Bit 0 (1)") && !output(scene, "Bit 1 (2)"),
                "Counter did not start paused at zero."
            );
            for (int edge = 1; edge <= 32; ++edge)
            {
                require(scene.stepAllClocks(1) == 1, "Counter could not single-step.");
                const int value = (edge + 1) / 2 % 4;
                require(
                    output(scene, "Bit 0 (1)") == bool(value & 1) && output(scene, "Bit 1 (2)") == bool(value & 2),
                    "Counter missed a rising edge or counted on a falling edge."
                );
            }
            scene.resetSimulation();
            require(!output(scene, "Bit 0 (1)") && !output(scene, "Bit 1 (2)"), "Counter reset did not restore zero.");
        }
    }
}


/** @brief Verify the File examples submenu routes selection without replacing an existing workspace. */
void menus()
{
    CircuitViews views;
    Input input;
    FontMetrics font;
    UI ui;
    views.select(0, input);
    views.mainScene().addComponent(BuiltinComponentIds::Input, {0, 0});
    views.setActiveFile("existing.json");
    ui.setCircuitViews(&views, &font);
    const auto original = circuitToJson(views.mainScene(), "Main");
    for (const auto surface : {CanvasSurface{900, 700, 900, 700}, CanvasSurface{320, 500, 640, 1000}})
    {
        ui.layout(views.activeScene().getComponentCatalog(), surface, input);
        const auto frame = CanvasCamera{}.frame(surface);
        auto hover = [&](CanvasViewport box)
        {
            UiInputEvent event{UiInputKind::Cursor};
            event.x = box.x + box.width / 2;
            event.y = box.y + box.height / 2;
            ui.handleInput(event, views.activeScene(), input, frame);
        };
        auto click = [&](CanvasViewport box)
        {
            hover(box);
            UiInputEvent event{UiInputKind::MouseButton};
            event.code = GLFW_MOUSE_BUTTON_LEFT;
            event.x = box.x + box.width / 2;
            event.y = box.y + box.height / 2;
            event.action = GLFW_PRESS;
            ui.handleInput(event, views.activeScene(), input, frame);
            event.action = GLFW_RELEASE;
            ui.handleInput(event, views.activeScene(), input, frame);
        };
        auto escape = [&]
        {
            UiInputEvent event{UiInputKind::Key};
            event.code = GLFW_KEY_ESCAPE;
            event.action = GLFW_PRESS;
            ui.handleInput(event, views.activeScene(), input, frame);
            event.action = GLFW_RELEASE;
            ui.handleInput(event, views.activeScene(), input, frame);
        };
        click(ui.fileBounds());
        const auto parent = ui.fileOptionBounds(static_cast<int>(ui.fileCommands().size()));
        hover(parent);
        require(
            ui.examplesMenuOpen() && !ui.takeFileCommand(), "Examples hover queued a File command or failed to expand."
        );
        const auto bounds = ui.examplesMenuBounds();
        require(
            bounds.x >= 0 && bounds.x + bounds.width <= surface.windowWidth &&
                bounds.y + bounds.height <= surface.windowHeight,
            "Examples submenu escaped the window."
        );
        escape();
        require(
            !ui.examplesMenuOpen() && ui.fileMenuOpen() && input.getUiCapture().keyboard,
            "Escape did not collapse only the Examples submenu."
        );
        click(parent);
        click(ui.exampleOptionBounds(1));
        const auto selected = ui.takeExample();
        require(
            selected == CircuitExample::HalfAdder && !ui.takeExample() && !ui.fileMenuOpen() && !ui.takeFileCommand(),
            "Example selection did not queue one distinct request and close the menu."
        );
        click(ui.fileBounds());
        click(ui.fileOptionBounds(static_cast<int>(ui.fileCommands().size()) + 1));
        require(
            ui.takeLicenseRequest() && !ui.takeLicenseRequest() && !ui.fileMenuOpen() && !ui.takeFileCommand(),
            "License menu selection did not queue one distinct request and close the menu."
        );
        const auto oldSize = views.size();
        openExampleCircuit(*selected, ui, views, input, surface);
        require(
            views.size() == oldSize + 1 && views.name(views.activeIndex()) == "Half adder" &&
                views.activeFile().empty() && circuitToJson(views.mainScene(), "Main") == original &&
                input.getMode() == EditorMode::Interaction && input.getUndoCount() == 0,
            "Opening an example replaced existing work or inherited file/history state."
        );
        combinational(views.activeScene(), *selected);
    }
    openExampleCircuit(CircuitExample::Counter, ui, views, input, {900, 700, 900, 700});
    require(
        ui.activeBottomTab() == UI::BottomTab::Clocks && ui.bottomVisible() && views.activeScene().clocksPaused(),
        "Counter did not open with visible clock controls and a paused clock."
    );
    ui.setCircuitViews(nullptr, nullptr);
    input.setScene(nullptr);
}
} // namespace


int main()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    int result = 0;
    try
    {
        circuits();
        menus();
        std::cout << "PASS: example_circuits\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
