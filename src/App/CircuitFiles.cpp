#include "App/CircuitFiles.h"

#include "App/FileDialog.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Persistence/CircuitFile.h"

#include <limits>
#include <stdexcept>

void performFileCommand(
    UI::FileCommand command,
    GLFWwindow* window,
    UI& ui,
    CircuitViews& views,
    Input& input,
    std::filesystem::path& mainFile
)
{
    const bool subEditor = views.role(views.activeIndex()) == CircuitViews::Role::Subcircuit;
    const bool importing = command == UI::FileCommand::LoadSubcircuit;
    const bool opening = importing || command == UI::FileCommand::Open;
    const auto current = subEditor ? views.activeFile() : mainFile;
    const auto chosen = !opening && command == UI::FileCommand::Save && !current.empty()
                            ? std::optional{current}
                            : chooseCircuitFile(window, !opening, current, subEditor || importing);
    applyFileCommand(command, chosen, ui, views, input, mainFile);
}

void applyFileCommand(
    UI::FileCommand command,
    const std::optional<std::filesystem::path>& chosen,
    UI& ui,
    CircuitViews& views,
    Input& input,
    std::filesystem::path& mainFile
)
{
    if (!chosen)
        return;
    const bool subEditor = views.role(views.activeIndex()) == CircuitViews::Role::Subcircuit;
    const bool importing = command == UI::FileCommand::LoadSubcircuit;
    const bool opening = importing || command == UI::FileCommand::Open;
    ui.dismissPopups(input);
    input.cancelCurrentAction();
    if (importing)
    {
        auto loaded = loadSubcircuit(*chosen);
        std::optional<ComponentDefinition> definition;
        std::string problem;
        try
        {
            definition = makeSubcircuit(loaded.design.scene, loaded.identity, loaded.design.name);
        }
        catch (const std::invalid_argument& error)
        {
            problem = error.what();
        }
        if (definition)
        {
            try
            {
                views.publishSubcircuits({*definition});
                views.rememberSubcircuitFile(loaded.identity.id, *chosen);
                ui.setFileStatus("Loaded subcircuit into Custom");
                return;
            }
            catch (const std::invalid_argument& error)
            {
                problem = error.what();
            }
        }
        views.openSubcircuit(
            std::move(loaded.design.scene), loaded.identity, loaded.design.name, *chosen, input
        );
        ui.setFileStatus("Opened draft: " + problem, true);
    }
    else if (opening)
    {
        auto loaded = loadCircuit(*chosen);
        views.replaceMain(std::move(loaded.scene), std::move(loaded.name), input);
        mainFile = *chosen;
        ui.setFileStatus("Opened Main circuit");
    }
    else if (subEditor)
    {
        auto identity = views.activeDefinition();
        if (identity.id.empty())
            identity = {newSubcircuitId(), 1};
        else
        {
            if (identity.version == std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Subcircuit version limit reached.");
            ++identity.version;
        }
        const auto& name = views.name(views.activeIndex());
        // Round-trip drops runtime memory and unused catalog entries before freezing the design.
        auto canonical = subcircuitFromJson(subcircuitToJson(views.activeScene(), identity, name));
        std::optional<ComponentDefinition> definition;
        std::string problem;
        try
        {
            definition = makeSubcircuit(canonical.design.scene, identity, name);
        }
        catch (const std::invalid_argument& error)
        {
            problem = error.what();
        }
        saveSubcircuit(*chosen, views.activeScene(), identity, name);
        views.activeDefinition() = identity;
        views.setActiveFile(*chosen);
        if (definition)
        {
            try
            {
                views.publishSubcircuits({*definition});
            }
            catch (const std::invalid_argument& error)
            {
                problem = error.what();
            }
        }
        ui.setFileStatus(
            problem.empty() ? "Saved and published subcircuit"
                            : "Saved draft; published version unchanged: " + problem,
            !problem.empty()
        );
    }
    else
    {
        saveCircuit(*chosen, views.mainScene(), views.name(0));
        mainFile = *chosen;
        ui.setFileStatus("Saved Main circuit");
    }
}
