#include "App/CircuitFiles.h"

#include "App/FileDialog.h"
#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Editor/Subcircuits.h"
#include "Persistence/SubcircuitLibrary.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace
{
SubcircuitLibrary& managedStorage(SubcircuitLibrary* library)
{
    if (!library)
        throw std::runtime_error("Library storage is unavailable; check the Messages tab.");
    return *library;
}

DefinitionIdentity nextIdentity(DefinitionIdentity identity, const SubcircuitLibrary& library)
{
    if (identity.id.empty())
        return {newSubcircuitId(), 1};
    identity.version = std::max(identity.version, library.version(identity.id));
    if (identity.version == std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Subcircuit version limit reached.");
    ++identity.version;
    return identity;
}

std::pair<std::filesystem::path, std::string>
storeDesign(SavedSubcircuit& design, SubcircuitLibrary& library, CircuitViews& views)
{
    std::optional<ComponentDefinition> definition;
    std::string problem;
    try
    {
        definition = makeSubcircuit(design.design.scene, design.identity, design.design.name);
        views.validatePublication({*definition});
    }
    catch (const std::invalid_argument& error)
    {
        problem = error.what();
        definition.reset();
    }
    const auto path = library.store(design, !definition);
    if (definition)
        views.publishSubcircuits({*definition});
    views.rememberSubcircuitFile(design.identity.id, path);
    return {path, problem};
}
} // namespace

void openExampleCircuit(
    CircuitExample example, UI& ui, CircuitViews& views, Input& input, CanvasSurface surface
)
{
    auto scene = buildExampleCircuit(example);
    const auto examples = circuitExamples();
    const auto info = std::find_if(
        examples.begin(), examples.end(), [&](const auto& value) { return value.id == example; }
    );
    ui.dismissPopups(input);
    input.cancelCurrentAction();
    views.create(input);
    views.replaceWorkspace(std::move(scene), std::string(info->name), input);
    input.setMode(EditorMode::Interaction);
    ui.layout(views.activeScene().getComponentCatalog(), surface, input);
    if (example == CircuitExample::Counter)
    {
        ui.showClockPanel(input);
        views.activeClockTimeline().record(
            views.activeScene().clockTime(), views.activeScene().clockReadings()
        );
    }
    input.executeViewCommand(Input::ViewCommand::FitCircuit, surface);
    ui.setFileStatus(std::string(info->description));
}

void refreshSubcircuitLibrary(SubcircuitLibrary& library, CircuitViews& views, UI& ui)
{
    std::vector<ComponentDefinition> definitions;
    std::vector<UI::Button> drafts;
    for (const auto& entry : library.entries())
    {
        if (entry.draft)
            drafts.push_back({entry.identity.id, entry.name, {}, 0, 0, entry.path});
        else
        {
            const auto saved = loadSubcircuit(entry.path);
            definitions.push_back(
                makeSubcircuit(saved.design.scene, saved.identity, saved.design.name)
            );
            views.rememberSubcircuitFile(entry.identity.id, entry.path);
        }
    }
    views.loadLibrary(definitions);
    ui.setLibraryDrafts(std::move(drafts));
}

bool performFileCommand(
    UI::FileCommand command,
    GLFWwindow* window,
    UI& ui,
    CircuitViews& views,
    Input& input,
    SubcircuitLibrary* library
)
{
    if (command == UI::FileCommand::OpenStorage)
    {
        openStorageFolder(managedStorage(library).location());
        return true;
    }
    if (command == UI::FileCommand::ChangeStorage)
        return applyFileCommand(command, chooseStorageFolder(window), ui, views, input, library);
    const bool subEditor = views.role(views.activeIndex()) == CircuitViews::Role::Subcircuit;
    if (subEditor && command == UI::FileCommand::Save)
        return applyFileCommand(
            command, managedStorage(library).location(), ui, views, input, library
        );
    const bool importing = command == UI::FileCommand::LoadSubcircuit;
    const bool opening = importing || command == UI::FileCommand::Open;
    const auto current = views.activeFile();
    const auto chosen = !opening && command == UI::FileCommand::Save && !current.empty()
                            ? std::optional{current}
                            : chooseCircuitFile(
                                  window,
                                  !opening,
                                  subEditor ? std::filesystem::path{} : current,
                                  subEditor || importing
                              );
    return applyFileCommand(command, chosen, ui, views, input, library);
}

bool applyFileCommand(
    UI::FileCommand command,
    const std::optional<std::filesystem::path>& chosen,
    UI& ui,
    CircuitViews& views,
    Input& input,
    SubcircuitLibrary* library
)
{
    if (!chosen)
        return false;
    const bool subEditor = views.role(views.activeIndex()) == CircuitViews::Role::Subcircuit;
    ui.dismissPopups(input);
    input.cancelCurrentAction();
    if (command == UI::FileCommand::ChangeStorage)
    {
        auto& storage = managedStorage(library);
        storage.changeLocation(*chosen);
        refreshSubcircuitLibrary(storage, views, ui);
        ui.setFileStatus("Storage location changed; original files kept");
    }
    else if (command == UI::FileCommand::LoadSubcircuit)
    {
        auto& storage = managedStorage(library);
        auto loaded = loadSubcircuit(*chosen);
        // Import is an explicit update; a published ID always advances its version locally.
        if (storage.version(loaded.identity.id) >= loaded.identity.version)
            loaded.identity = nextIdentity(loaded.identity, storage);
        const auto [path, problem] = storeDesign(loaded, storage, views);
        refreshSubcircuitLibrary(storage, views, ui);
        if (!problem.empty())
            views.openSubcircuit(
                std::move(loaded.design.scene), loaded.identity, loaded.design.name, path, input
            );
        ui.setFileStatus(
            problem.empty() ? "Imported subcircuit into library" : "Imported draft: " + problem,
            !problem.empty()
        );
    }
    else if (command == UI::FileCommand::Open)
    {
        auto loaded = loadCircuit(*chosen);
        if (subEditor)
            throw std::invalid_argument("Open a workspace from a workspace tab.");
        views.replaceWorkspace(std::move(loaded.scene), std::move(loaded.name), input);
        views.setActiveFile(*chosen);
        ui.setFileStatus("Opened workspace");
    }
    else if (subEditor && command == UI::FileCommand::ExportSubcircuit)
    {
        auto identity = views.activeDefinition();
        if (identity.id.empty())
            identity = {newSubcircuitId(), 1};
        saveSubcircuit(*chosen, views.activeScene(), identity, views.name(views.activeIndex()));
        // A portable copy is not a managed Save: preserve association and dirty status.
        ui.setFileStatus("Exported subcircuit copy; use Save to keep it in the library");
    }
    else if (subEditor && command == UI::FileCommand::Save)
    {
        auto& storage = managedStorage(library);
        const auto identity = nextIdentity(views.activeDefinition(), storage);
        const auto& name = views.name(views.activeIndex());
        auto canonical = subcircuitFromJson(subcircuitToJson(views.activeScene(), identity, name));
        const auto [path, problem] = storeDesign(canonical, storage, views);
        views.activeDefinition() = identity;
        views.setActiveFile(path);
        views.markSaved(views.activeIndex());
        refreshSubcircuitLibrary(storage, views, ui);
        ui.setFileStatus(
            problem.empty() ? "Saved to library; available in Custom"
                            : "Saved draft; published version unchanged: " + problem,
            !problem.empty()
        );
    }
    else if (!subEditor && (command == UI::FileCommand::Save || command == UI::FileCommand::SaveAs))
    {
        saveCircuit(*chosen, views.activeScene(), views.name(views.activeIndex()));
        views.setActiveFile(*chosen);
        views.markSaved(views.activeIndex());
        ui.setFileStatus("Saved workspace");
    }
    else
        return false;
    return true;
}
