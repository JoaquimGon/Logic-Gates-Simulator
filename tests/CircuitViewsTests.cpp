#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Graphics/Text/TextGeometry.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void run(GLFWwindow* window)
{
    Input input;
    CircuitViews views;
    views.select(0, input);
    UI ui;
    FontMetrics font;
    for (auto& character : font.cdata)
        character.xadvance = 16;
    ui.setCircuitViews(&views, &font);
    auto layout = [&]
    {
        ui.layout(
            views.activeScene().getComponentCatalog(), input.getCameraFrame(window).surface, input
        );
    };
    layout();
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        {
            layout();
            const bool consumed =
                ui.handleInput(event, views.activeScene(), input, input.getCameraFrame(window));
            layout();
            return consumed;
        }
    );
    auto cursor = [&](double x, double y)
    {
        glfwSetCursorPos(window, x, y);
        input.handleCursorPos(window, x, y);
    };
    auto mouse = [&](int button, int action)
    { input.handleMouseButton(window, button, action, 0); };
    auto click = [&](CanvasViewport bounds, int button = GLFW_MOUSE_BUTTON_LEFT)
    {
        cursor(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        mouse(button, GLFW_PRESS);
        mouse(button, GLFW_RELEASE);
    };
    auto key = [&](int code)
    {
        input.handleKey(code, GLFW_PRESS);
        input.process(window);
        input.handleKey(code, GLFW_RELEASE);
    };
    auto type = [&](const std::string& value)
    {
        for (unsigned char c : value)
            input.handleText(c);
    };
    Scene& main = views.activeScene();
    const auto edit =
        EditorActions(main).apply({CreateComponent{BuiltinComponentIds::Input, {0, 0}}});
    require(static_cast<bool>(edit), "Main fixture could not be created.");
    const int source = edit.createdComponentIds.front();
    main.handleClick(source);
    main.propagate();
    main.syncVisuals();
    input.setPanOffset({0.12f, -0.2f});
    input.setZoom(1.75f);
    require(input.getCanvasViewport()->y == 30, "Tabs did not reserve canvas space.");
    const double width = static_cast<double>(getTextWidth("unnamed", 0.42f, font)) + 44;
    require(
        std::abs(ui.circuitTabs().tabBounds(0).width - width) < 0.01,
        "Tab width is not based on the default name's font metrics."
    );
    const auto add = ui.circuitTabs().addBounds();
    const auto mainTab = ui.circuitTabs().tabBounds(0);
    require(
        add.width == add.height && add.height == 30 &&
            std::abs(add.x - (mainTab.x + mainTab.width)) < 0.01,
        "Plus is not an independent square after Main."
    );
    click(add);
    const auto circuitTab = ui.circuitTabs().tabBounds(1);
    require(
        std::abs(ui.circuitTabs().addBounds().x - (circuitTab.x + circuitTab.width)) < 0.01,
        "Plus did not follow the last circuit tab."
    );
    require(
        views.size() == 2 && views.activeIndex() == 1 && views.name(1) == "unnamed" &&
            views.activeScene().getComponentCount() == 0,
        "Plus did not create and select an empty named scene."
    );
    require(
        input.getPanOffset() == glm::vec2{0} && input.getZoom() == 1,
        "New scene inherited the outgoing camera."
    );
    require(
        std::abs(ui.circuitTabs().tabBounds(1).width - width) < 0.01,
        "Circuit tabs do not have uniform widths."
    );
    const auto paletteButton = ui.buttons().front().bounds;
    cursor(paletteButton.x + 20, paletteButton.y + 20);
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(ui.dragging(), "Palette drag fixture did not begin.");
    cursor(500, 15);
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        !ui.dragging() && !input.getUiCapture().pointer &&
            views.activeScene().getComponentCount() == 0,
        "Releasing a palette drag over the tab strip left capture or created a component."
    );
    const int gate = views.activeScene().addComponent(BuiltinComponentIds::And, {0, 0});
    Scene& second = views.activeScene();
    input.setPanOffset({-0.1f, 0.3f});
    input.setZoom(0.75f);
    click(ui.circuitTabs().tabBounds(0));
    require(
        &views.activeScene() == &main && main.getComponentCount() == 1 &&
            input.getPanOffset() == glm::vec2{0.12f, -0.2f} && input.getZoom() == 1.75f,
        "Returning to Main lost its scene or camera."
    );
    require(
        main.pinState({source, 0}, PinType::OUTPUT) == PinState::ON,
        "Switching views lost the source's signal state."
    );
    require(
        !views.select(999, input) && views.activeIndex() == 0,
        "Invalid scene selection changed the active circuit."
    );

    click(ui.circuitTabs().tabBounds(1));
    require(
        &views.activeScene() == &second && second.getComponentCount() == 1 &&
            input.getPanOffset() == glm::vec2{-0.1f, 0.3f} && input.getZoom() == 0.75f,
        "Returning to a circuit lost its independent camera or contents."
    );
    const auto point = input.getCameraFrame(window).worldToWindow({0, 0});
    cursor(point->x, point->y);
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(!input.isIdle(), "Move cancellation fixture did not start a drag.");
    views.select(0, input);
    layout();
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    require(
        input.isIdle() && input.getSelectedComponentId() == -1 &&
            second.getCommittedComponentView(gate)->getGridPosition() == GridCoords{0, 0} &&
            main.wireCount() == 0,
        "Switching views leaked a drag, selection, or mouse release into another scene."
    );
    const auto restored = EditorActions(main).restore(*edit.change->before, main.getRevision());
    require(
        static_cast<bool>(restored) && main.getComponentCount() == 0 &&
            second.getComponentCount() == 1,
        "Editor snapshot restoration crossed circuit boundaries."
    );

    cursor(600, 250);
    ui.update(10, input);
    const auto tab = ui.circuitTabs().tabBounds(1);
    cursor(tab.x + 10, tab.y + 10);
    ui.update(10.99, input);
    require(!ui.circuitTabs().popupIndex(), "Circuit popup appeared before the hover delay.");
    ui.update(11.01, input);
    require(ui.circuitTabs().popupIndex() == 1, "Circuit popup did not appear after one second.");
    click(ui.circuitTabs().nameBounds());
    require(input.getUiCapture().keyboard, "Circuit name editing did not capture typing.");
    const auto revision = main.getRevision();
    const std::string name = "A much longer circuit name";
    type(name);
    click(ui.circuitTabs().nameBounds());
    key(GLFW_KEY_F2);
    require(
        input.getMode() == EditorMode::Selection, "Typing in a circuit name toggled editor mode."
    );
    key(GLFW_KEY_ENTER);
    require(
        views.name(1) == name && ui.circuitTabs().tabName(1).ends_with("...") &&
            !input.getUiCapture().keyboard && main.getRevision() == revision,
        "Circuit naming failed, leaked a shortcut, or did not shorten the tab title."
    );
    require(
        getTextWidth(ui.circuitTabs().tabName(1), 0.42f, font) <= width - 44,
        "Shortened circuit title overflows its tab."
    );
    key(GLFW_KEY_ESCAPE);
    click(ui.circuitTabs().tabBounds(1), GLFW_MOUSE_BUTTON_RIGHT);
    require(ui.circuitTabs().popupIndex() == 1, "Right-click did not open circuit information.");
    click(ui.circuitTabs().nameBounds());
    type("discarded");
    key(GLFW_KEY_ESCAPE);
    require(
        views.name(1) == name && !input.getUiCapture().keyboard,
        "Escape committed a draft or kept keyboard capture."
    );
    click(ui.circuitTabs().tabBounds(1), GLFW_MOUSE_BUTTON_RIGHT);
    click(ui.circuitTabs().nameBounds());
    type("focus loss");
    input.handleFocus(false);
    input.handleFocus(true);
    require(
        views.name(1) == name && !ui.circuitTabs().popupIndex() && !input.getUiCapture().keyboard,
        "Focus loss retained or committed a circuit-name editor."
    );

    for (int i = 0; i < 12; ++i)
        click(ui.circuitTabs().addBounds());
    const auto last = ui.circuitTabs().tabBounds(views.activeIndex());
    require(
        last.x < 800 && last.x + last.width <= 800.01 && ui.circuitTabs().tabBounds(0).x == 220,
        "Many tabs hid the active circuit or moved Main."
    );
    const auto trailingAdd = ui.circuitTabs().addBounds();
    require(
        trailingAdd.width == trailingAdd.height && trailingAdd.x + trailingAdd.width <= 800.01 &&
            std::abs(trailingAdd.x - (last.x + last.width)) < 0.01,
        "Overflow hid the trailing plus square after creating a circuit."
    );
    glfwSetWindowSize(window, 420, 600);
    layout();
    const auto resizedTab = ui.circuitTabs().tabBounds(views.activeIndex());
    require(
        resizedTab.x < 420 && resizedTab.x + resizedTab.width <= 420.01,
        "Resizing left the active circuit tab outside the visible strip."
    );
    require(
        ui.circuitTabs().addBounds().x + ui.circuitTabs().addBounds().width <= 420.01,
        "Resize hid the trailing plus square."
    );
    glfwSetWindowSize(window, 800, 600);
    layout();
    const float zoom = input.getZoom();
    cursor(650, 15);
    input.handleScroll(window, 0, 100);
    require(
        input.getZoom() == zoom && ui.circuitTabs().tabBounds(1).x >= 220,
        "Scrolling circuit tabs zoomed the canvas or did not reveal earlier circuits."
    );
    click(ui.circuitTabs().tabBounds(0));
    require(
        views.activeIndex() == 0 && views.name(0) == "Main",
        "The permanent Main circuit could not be reached after many tabs."
    );
    key(GLFW_KEY_ESCAPE);
    const auto count = views.size();
    require(
        ui.circuitTabs().closeBounds(0).width == 0 && !views.remove(0, input) &&
            !views.remove(999, input) && views.size() == count,
        "Main or an invalid circuit could be removed."
    );
    views.select(3, input);
    layout();
    auto* retained = &views.activeScene();
    retained->addComponent(BuiltinComponentIds::And, {0, 0});
    input.setPanOffset({0.21f, -0.08f});
    input.setZoom(1.4f);
    click(ui.circuitTabs().closeBounds(2));
    require(
        ui.circuitTabs().confirmingDelete() && ui.circuitTabs().popupIndex() == 2 &&
            views.size() == count && views.activeIndex() == 3 && input.getUiCapture().keyboard &&
            input.getUiCapture().pointer,
        "Close did not request confirmation without switching or deleting the circuit."
    );
    ui.update(100, input);
    key(GLFW_KEY_F2);
    require(
        ui.circuitTabs().confirmingDelete() && input.getMode() == EditorMode::Selection,
        "Hover timing or a shortcut dismissed the deletion confirmation."
    );
    click(ui.circuitTabs().cancelDeleteBounds());
    require(
        views.size() == count && !ui.circuitTabs().popupIndex() && !input.getUiCapture().keyboard &&
            !input.getUiCapture().pointer,
        "Cancel deleted a circuit or retained input capture."
    );
    click(ui.circuitTabs().closeBounds(2));
    key(GLFW_KEY_ESCAPE);
    require(
        views.size() == count && !ui.circuitTabs().confirmingDelete(),
        "Escape did not cancel circuit deletion."
    );
    click(ui.circuitTabs().closeBounds(2));
    input.handleFocus(false);
    input.handleFocus(true);
    require(
        views.size() == count && !ui.circuitTabs().popupIndex() && !input.getUiCapture().pointer,
        "Focus loss retained or confirmed a circuit deletion."
    );
    click(ui.circuitTabs().closeBounds(2));
    click(ui.circuitTabs().deleteBounds());
    require(
        views.size() == count - 1 && views.activeIndex() == 2 && &views.activeScene() == retained &&
            input.getPanOffset() == glm::vec2{0.21f, -0.08f} && input.getZoom() == 1.4f,
        "Deleting an earlier inactive tab lost the active scene or camera."
    );
    const auto dragPoint = input.getCameraFrame(window).worldToWindow({0, 0});
    cursor(dragPoint->x, dragPoint->y);
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
    require(!input.isIdle(), "Active deletion fixture did not start a drag.");
    const auto close = ui.circuitTabs().closeBounds(2);
    cursor(close.x + close.width / 2, close.y + close.height / 2);
    mouse(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
    click(close);
    require(
        input.isIdle() && views.size() == count - 1,
        "Opening confirmation did not cancel a pending drag safely."
    );
    click(ui.circuitTabs().deleteBounds());
    input.process(window);
    require(
        views.size() == count - 2 && views.activeIndex() == 1 && &views.activeScene() == &second &&
            input.isIdle() && input.getSelectedComponentId() == -1 &&
            input.getPanOffset() == glm::vec2{-0.1f, 0.3f} && input.getZoom() == 0.75f,
        "Deleting the active scene did not safely restore its left neighbor."
    );
    while (views.size() > 1)
        require(views.remove(views.size() - 1, input), "Could not remove a remaining circuit.");
    layout();
    input.process(window);
    require(
        views.activeIndex() == 0 && &views.activeScene() == &main &&
            ui.circuitTabs().addBounds().x ==
                ui.circuitTabs().tabBounds(0).x + ui.circuitTabs().tabBounds(0).width,
        "Removing the final circuit did not leave Main and its trailing plus."
    );
    ui.setCircuitViews(nullptr, nullptr);
    input.setUiInputHandler({});
    input.setScene(nullptr);
}
} // namespace

int main()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 600, "Circuit views tests", nullptr, nullptr);
    int result = 0;
    try
    {
        require(window != nullptr, "Cannot create headless test window.");
        run(window);
        std::cout << "PASS: circuit_views\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
