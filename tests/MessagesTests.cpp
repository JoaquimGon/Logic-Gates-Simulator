#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Text/TextGeometry.h"
#include "UI/UI.h"

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

void messagesPanel(GLFWwindow* window)
{
    CircuitViews views;
    Input input;
    UI ui;
    FontMetrics font;
    for (auto& glyph : font.cdata)
        glyph.xadvance = 18;
    views.select(0, input);
    glfwSetWindowUserPointer(window, &input);
    ui.setCircuitViews(&views, &font);
    auto layout = [&]()
    { ui.layout(views.activeScene().getComponentCatalog(), {800, 800, 800, 800}, input); };
    layout();
    input.setEditErrorHandler([&](const std::string& message) { ui.addMessage(message); });
    input.setUiInputHandler(
        [&](const UiInputEvent& event)
        { return ui.handleInput(event, views.activeScene(), input, input.getCameraFrame(window)); }
    );
    auto click = [&](CanvasViewport box)
    {
        const double x = box.x + box.width / 2, y = box.y + box.height / 2;
        glfwSetCursorPos(window, x, y);
        input.handleCursorPos(window, x, y);
        input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        input.handleMouseButton(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    };
    require(
        ui.activeBottomTab() == UI::BottomTab::Messages &&
            ui.bottomTabBounds(UI::BottomTab::Subcircuit).width == 0,
        "Workspace did not expose only the Messages tab."
    );
    ui.reportSimulation(views.activeScene(), 0);
    require(ui.messages().empty(), "An empty workspace acquired subcircuit validity errors.");
    const auto created = EditorActions(views.activeScene())
                             .apply({CreateComponent{BuiltinComponentIds::And, {0, 0}}});
    input.recordEdit(created);
    require(ui.messages().empty(), "Successful editor actions produced error messages.");
    const auto position = input.getCameraFrame(window).worldToWindow({0, 0});
    require(position.has_value(), "Selection fixture is outside the canvas.");
    click({position->x - 1, position->y - 1, 2, 2});
    const int selected = input.getSelectedComponentId();
    click(ui.bottomTabBounds(UI::BottomTab::Messages));
    require(
        selected != -1 && input.getSelectedComponentId() == selected,
        "Reading Messages cleared the completed component selection."
    );
    const auto before = views.activeScene().getRevision();
    const auto undo = input.getUndoCount();
    const auto overlap = EditorActions(views.activeScene())
                             .apply({CreateComponent{BuiltinComponentIds::And, {0, 0}}});
    require(overlap.error == EditError::Overlap, "Failed-edit fixture did not overlap.");
    input.recordEdit(overlap);
    require(
        ui.messages().size() == 1 && ui.messages().back().find("] Error:") == 9 &&
            views.activeScene().getRevision() == before && input.getUndoCount() == undo,
        "Editor rejection was lost or changed design/history."
    );
    ui.setFileStatus("Cannot open the chosen circuit", true);
    require(
        ui.messages().size() == 1 && ui.messages().back().find("Cannot open") != std::string::npos,
        "Latest file failure was missing or retained older action errors."
    );
    const auto& timed = ui.messages().back();
    require(
        timed.size() > 11 && timed[0] == '[' && timed[3] == ':' && timed[6] == ':' &&
            timed[9] == ']' && timed[10] == ' ',
        "Action timestamp is not [HH:MM:SS]."
    );
    ui.setFileStatus("Saved workspace");
    require(ui.messages().empty(), "A successful save retained its previous failure.");
    ui.addMessage("Main action failed");
    const int selectionBeforeNotice = input.getSelectedComponentId();
    click(ui.messageNoticeBounds());
    require(
        selectionBeforeNotice != -1 && input.getSelectedComponentId() == selectionBeforeNotice,
        "Opening Messages from the notice cleared completed selection."
    );
    const auto mainLog = ui.messages();

    views.create(input, CircuitViews::Role::Subcircuit);
    ui.setFileStatus("Opened subcircuit draft", true);
    layout();
    require(ui.hasUnreadMessages(), "An error before the new view's layout lost its unread mark.");
    ui.clearMessages();
    require(
        ui.messages().empty() && ui.activeBottomTab() == UI::BottomTab::Subcircuit &&
            ui.bottomTabBounds(UI::BottomTab::Messages).width > 0 && ui.fileOptions().size() == 2,
        "Subcircuit log inherited workspace errors or lost its separate overview/menu."
    );
    ui.reportSimulation(views.activeScene(), 1);
    require(
        ui.messages().empty(), "Subcircuit readiness was incorrectly logged as a simulation error."
    );
    ui.addMessage("Subcircuit save failed");
    require(
        ui.hasUnreadMessages() && ui.activeBottomTab() == UI::BottomTab::Subcircuit,
        "New error stole focus or failed to mark the Messages tab."
    );
    require(
        ui.messagesTabLabel() == "Messages (1)" && ui.hasMessageErrors() &&
            ui.messageNoticeBounds().width > 0,
        "Hidden action error lacked persistent indicators."
    );
    click(ui.fileBounds());
    require(ui.fileMenuOpen(), "Notice/menu test failed to open File.");
    const auto viewBeforeNotice = views.activeIndex();
    const auto historyBeforeNotice = input.getUndoCount();
    click(ui.messageNoticeBounds());
    require(
        !ui.fileMenuOpen() && views.activeIndex() == viewBeforeNotice &&
            input.getUndoCount() == historyBeforeNotice && ui.messagesTabLabel() == "Messages (1)",
        "Notice failed to open Messages, lost the badge or changed the circuit view/history."
    );
    require(
        !ui.hasUnreadMessages() && ui.activeBottomTab() == UI::BottomTab::Messages,
        "Reading Messages did not clear the unread indication."
    );
    const auto subLog = ui.messages();
    views.select(0, input);
    layout();
    require(ui.messages() == mainLog, "Switching to Main showed subcircuit-only errors.");
    views.select(1, input);
    layout();
    require(ui.messages() == subLog, "Switching back lost the subcircuit log.");
    click(ui.bottomTabBounds(UI::BottomTab::Messages));
    input.recordEdit(EditResult{});
    require(
        ui.messages().empty() && !ui.hasUnreadMessages(),
        "A successful action failed to dismiss this view's previous error."
    );

    auto& scene = views.activeScene();
    const int oscillator = scene.addComponent(BuiltinComponentIds::Not, {30, 20});
    const auto loop =
        EditorActions(scene).apply({AddWire{{{31, 20}, {31, 24}, {28, 24}, {28, 20}}}});
    require(static_cast<bool>(loop), "Feedback fixture failed.");
    require(
        scene.propagate() == SimulationResult::NON_CONVERGENT &&
            scene.unsettledComponent() == oscillator,
        "Unsettled component was not identified."
    );
    click(ui.bottomTabBounds(UI::BottomTab::Subcircuit));
    ui.reportSimulation(scene, 2);
    require(
        ui.activeBottomTab() == UI::BottomTab::Subcircuit &&
            ui.messagesTabLabel() == "Messages (1)" && ui.hasMessageErrors(),
        "Simulation error stole focus or failed to mark the inactive Messages tab."
    );
    const auto cameraBeforeNotice = input.getPanOffset();
    const auto revisionBeforeNotice = scene.getRevision();
    click(ui.messageNoticeBounds());
    require(
        ui.activeBottomTab() == UI::BottomTab::Messages &&
            ui.messagesTabLabel() == "Messages (1)" && !ui.hasUnreadMessages() &&
            input.getPanOffset() == cameraBeforeNotice &&
            scene.getRevision() == revisionBeforeNotice,
        "Reading the error removed its badge or changed camera/design state."
    );
    const auto fault = ui.simulationIssues();
    ui.reportSimulation(scene, 3);
    require(
        ui.simulationIssues() == fault && fault.size() == 1 &&
            fault[0].location == GridCoords{30, 20} && ui.messages().empty(),
        "Unsettled activity was not a stable, located current error."
    );
    ui.addMessage("Another action failed");
    input.recordEdit(EditResult{});
    ui.reportSimulation(scene, 4);
    require(
        ui.messages().empty() && ui.simulationIssues() == fault,
        "A successful action dismissed an existing circuit fault."
    );
    input.setPanOffset({-2, -2});
    input.setZoom(0.8f);
    const auto revision = scene.getRevision();
    const auto history = input.getUndoCount();
    const auto topology = scene.getTopologyBuildCount();
    const auto link = ui.findMessageBounds(0);
    require(link.width > 0, "Located simulation error did not offer FIND.");
    click(link);
    require(
        input.getPanOffset() == GridSystem::gridToWorld({30, 20}) && input.getZoom() == 0.8f &&
            scene.getRevision() == revision && scene.getTopologyBuildCount() == topology &&
            input.getUndoCount() == history,
        "FIND changed the circuit/history/zoom or did not center the failure location."
    );
    views.select(0, input);
    layout();
    ui.reportSimulation(views.activeScene(), 5);
    require(
        ui.simulationIssues().empty() && ui.messages() == mainLog,
        "Subcircuit faults appeared in Main."
    );
    views.select(1, input);
    layout();
    require(ui.simulationIssues() == fault, "Switching views lost an existing fault.");
    click(ui.bottomTabBounds(UI::BottomTab::Messages));
    scene.removeComponent(oscillator);
    scene.propagate();
    ui.reportSimulation(scene, 6);
    require(
        ui.simulationIssues().empty() && ui.messages().empty() &&
            ui.findMessageBounds(0).width == 0 && scene.unsettledComponent() == -1 &&
            ui.messagesTabLabel() == "Messages" && ui.messageNoticeBounds().width == 0,
        "Repaired fault left an old error, recovery history or stale FIND link."
    );
    scene.addComponent(BuiltinComponentIds::Or, {10, 0});
    require(
        static_cast<bool>(
            EditorActions(scene).apply({AddWire{{{12, 0}, {12, 4}, {8, 4}, {8, 1}}}})
        ),
        "Stable feedback fixture failed."
    );
    scene.propagate();
    ui.reportSimulation(scene, 7);
    require(
        scene.getLastEvalResult() == SimulationResult::OK && ui.simulationIssues().empty(),
        "Valid feedback was reported as a circuit problem."
    );

    views.create(input);
    layout();
    auto& clocks = views.activeScene();
    clocks.addComponent(BuiltinComponentIds::Clock, {0, 0}, {.clockFrequency = 1000.0f});
    clocks.updateClocks(0.5f);
    require(clocks.pendingClockTime() > 0.1, "Backlog fixture did not retain enough time.");
    ui.reportSimulation(clocks, 10);
    require(ui.simulationIssues().empty(), "Brief catch-up produced an immediate warning.");
    views.select(0, input);
    layout();
    views.select(2, input);
    layout();
    ui.reportSimulation(clocks, 100);
    require(ui.simulationIssues().empty(), "Inactive time was treated as processing backlog.");
    ui.reportSimulation(clocks, 101.1);
    ui.reportSimulation(clocks, 102);
    require(
        ui.simulationIssues().size() == 1 && ui.simulationIssues()[0].warning &&
            !ui.simulationIssues()[0].location && ui.findMessageBounds(0).width == 0,
        "Backlog was missing or assigned a misleading gate location."
    );
    require(
        ui.messagesTabLabel() == "Messages (1)" && !ui.hasMessageErrors() &&
            ui.messageNoticeBounds().width > 0,
        "Warnings lacked an amber-only active indicator."
    );
    ui.addMessage("An action failed during catch-up");
    require(
        ui.messagesTabLabel() == "Messages (2)" && ui.hasMessageErrors(),
        "Mixed errors and warnings lost the combined count or error priority."
    );
    input.recordEdit(EditResult{});
    require(ui.simulationIssues().size() == 1, "A successful action hid an ongoing backlog.");
    require(
        ui.messagesTabLabel() == "Messages (1)" && !ui.hasMessageErrors(),
        "Clearing an action error failed to restore warning-only severity."
    );
    for (int i = 0; i < 100 && clocks.pendingClockTime() > 0; ++i)
        clocks.updateClocks(0);
    ui.reportSimulation(clocks, 103);
    require(
        ui.simulationIssues().empty() && ui.messages().empty() &&
            ui.messagesTabLabel() == "Messages" && ui.messageNoticeBounds().width == 0,
        "Catch-up retained old warnings or recovery indicators."
    );
    clocks.addComponent(BuiltinComponentIds::Input, {10, 0});
    const int driver = clocks.addComponent(BuiltinComponentIds::Input, {20, 0});
    require(
        static_cast<bool>(
            EditorActions(clocks).apply({AddWire{{{11, 0}, {11, 4}, {21, 4}, {21, 0}}}})
        ),
        "Short-circuit fixture failed."
    );
    clocks.propagate();
    ui.reportSimulation(clocks, 104);
    require(
        ui.simulationIssues().size() == 1 && ui.simulationIssues()[0].location == GridCoords{21, 0},
        "Short circuit did not identify a conflicting output pin."
    );
    click(ui.findMessageBounds(0));
    require(
        input.getPanOffset() == GridSystem::gridToWorld({21, 0}),
        "Short-circuit FIND did not center the conflicting pin."
    );
    const auto stale = ui.findMessageBounds(0);
    clocks.removeComponent(driver);
    input.setPanOffset({-3, -3});
    click(stale);
    require(
        input.getPanOffset() == glm::vec2{-3, -3},
        "FIND followed an obsolete error while the edited circuit awaited simulation."
    );
    clocks.propagate();
    ui.reportSimulation(clocks, 105);
    require(ui.simulationIssues().empty(), "Repaired short remained listed.");

    for (int i = 0; i < 8; ++i)
    {
        clocks.addComponent(BuiltinComponentIds::Input, {-80, i * 10});
        clocks.addComponent(BuiltinComponentIds::Input, {-70, i * 10});
        require(
            static_cast<bool>(EditorActions(clocks).apply(
                {AddWire{{{-79, i * 10}, {-79, i * 10 + 4}, {-69, i * 10 + 4}, {-69, i * 10}}}}
            )),
            "Multiple short-circuit fixture failed."
        );
    }
    clocks.propagate();
    ui.reportSimulation(clocks, 106);
    require(ui.simulationIssues().size() == 8, "Active errors were merged or lost.");
    const auto box = ui.messageBoxBounds();
    const auto zoom = input.getZoom();
    const auto sceneRevision = clocks.getRevision();
    glfwSetCursorPos(window, box.x + 10, box.y + 10);
    input.handleCursorPos(window, box.x + 10, box.y + 10);
    input.handleScroll(window, 0, -1000);
    require(
        input.getZoom() == zoom && clocks.getRevision() == sceneRevision &&
            ui.findMessageBounds(0).width == 0 && ui.findMessageBounds(7).width > 0,
        "Scrolling zoomed the canvas or mismatched visible FIND links."
    );
    const auto lastTarget = *ui.simulationIssues()[7].location;
    click(ui.findMessageBounds(7));
    require(
        input.getPanOffset() == GridSystem::gridToWorld(lastTarget),
        "Scrolled FIND navigated to a different error."
    );
    ui.addMessage("Routing failure");
    views.activeMessages().entries.back().replace(0, 11, "[00:00:00] ");
    const auto originalEntry = ui.messages().back();
    ui.addMessage("Routing failure");
    require(
        ui.messages().size() == 1 && ui.messages().back() == originalEntry,
        "Repeated action error lost its original timestamp."
    );
    input.recordEdit(EditResult{});
    require(
        ui.messages().empty() && ui.simulationIssues().size() == 8,
        "A successful action failed to clear its old error or hid circuit faults."
    );
    views.select(0, input);
    layout();
    require(ui.messages() == mainLog, "Clearing another view destroyed Main's latest action.");
    input.setEditErrorHandler({});
    input.setUiInputHandler({});
    glfwSetWindowUserPointer(window, nullptr);
}
} // namespace

int main()
{
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(800, 800, "Messages tests", nullptr, nullptr);
    int status = 0;
    try
    {
        require(window != nullptr, "Cannot create test window.");
        messagesPanel(window);
        std::cout << "PASS: messages_panel\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
