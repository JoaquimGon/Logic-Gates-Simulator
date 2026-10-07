#include "Editor/CircuitViews.h"

#include "Components/Clock.h"
#include "Components/InputPin.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Persistence/CircuitFile.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

CircuitViews::CircuitViews()
{
    m_views.push_back(View{"Main"});
}

std::vector<CircuitViews::SourceSettings> CircuitViews::sourceSettings(const Scene& scene)
{
    std::vector<SourceSettings> result;
    for (const auto& [id, view] : scene.getComponentViewMap())
    {
        const auto* logic = scene.getLogicComponent(id);
        if (const auto* clock = dynamic_cast<const Clock*>(logic))
            result.push_back({id, false, clock->getFrequency(), clock->isPaused()});
        else if (const auto* input = dynamic_cast<const InputPin*>(logic))
            result.push_back({id, input->getState()});
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& a, const auto& b) { return a.componentId < b.componentId; }
    );
    return result;
}

bool CircuitViews::hasUnsavedChanges(std::size_t index) const
{
    const auto& view = m_views.at(index);
    if (!view.savedDesign)
        return true;
    auto sources = sourceSettings(*view.scene);
    const auto currentRole = role(index);
    if (view.checkedRevision == view.scene->getRevision() && view.checkedName == view.name &&
        view.checkedRole == currentRole && view.checkedSources == sources)
        return view.unsaved;
    // Serialize only after a design edit or source setting changes, never for clock edges.
    try
    {
        view.unsaved = currentRole != view.savedRole ||
                       circuitToJson(*view.scene, view.name) != *view.savedDesign;
    }
    catch (const std::exception&)
    {
        view.unsaved = true; // A design which cannot be saved must not appear saved.
    }
    view.checkedRevision = view.scene->getRevision();
    view.checkedName = view.name;
    view.checkedRole = currentRole;
    view.checkedSources = std::move(sources);
    return view.unsaved;
}

void CircuitViews::markSaved(std::size_t index)
{
    auto& view = m_views.at(index);
    view.savedDesign = circuitToJson(*view.scene, view.name);
    view.savedRole = role(index);
    view.checkedRevision = view.scene->getRevision();
    view.checkedName = view.name;
    view.checkedRole = view.savedRole;
    view.checkedSources = sourceSettings(*view.scene);
    view.unsaved = false;
}

void CircuitViews::create(Input& input, Role role)
{
    if (role != Role::Workspace && role != Role::Subcircuit)
        throw std::invalid_argument("Unknown viewpoint role.");
    View view{"unnamed"};
    includeLibrary(*view.scene);
    view.scene->setInterfaceNamingRequired(role == Role::Subcircuit);
    m_views.push_back(std::move(view));
    select(m_views.size() - 1, input);
}

bool CircuitViews::setRole(std::size_t index, Role role)
{
    if (index >= m_views.size() || (role != Role::Workspace && role != Role::Subcircuit) ||
        (index == 0 && role != Role::Workspace))
        return false;
    const auto previous = this->role(index);
    if (!m_views[index].scene->setInterfaceNamingRequired(role == Role::Subcircuit))
        return false;
    if (previous != role)
        m_views[index].file.clear(); // The old path holds a different file format.
    return true;
}

bool CircuitViews::select(std::size_t index, Input& input)
{
    if (index >= m_views.size())
        return false;
    if (index != m_active)
    {
        m_views[m_active].pan = input.getPanOffset();
        m_views[m_active].zoom = input.getZoom();
        m_active = index;
        input.setScene(&activeScene());
        input.setPanOffset(m_views[m_active].pan);
        input.setZoom(m_views[m_active].zoom);
    }
    else
        input.setScene(&activeScene());
    input.setCanvasFocused(true);
    return true;
}

void CircuitViews::rename(std::size_t index, std::string name)
{
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos)
        name = index == 0 ? "Main" : "unnamed";
    else
        name = name.substr(first, name.find_last_not_of(' ') - first + 1);
    m_views.at(index).name = std::move(name);
}

bool CircuitViews::remove(std::size_t index, Input& input)
{
    if (index == 0 || index >= m_views.size())
        return false;
    if (index == m_active)
        select(index - 1, input);
    m_views.erase(m_views.begin() + index);
    if (index < m_active)
        --m_active;
    return true;
}

void CircuitViews::includeLibrary(Scene& scene) const
{
    EditBatch batch;
    for (const auto& [id, definition] : m_library)
        if (!scene.getComponentCatalog().find(id))
            batch.push_back(RegisterComponentDefinition{definition});
    const auto result = EditorActions(scene).apply(batch);
    if (!result)
        throw std::invalid_argument(result.message);
}

void CircuitViews::setActiveFile(std::filesystem::path path)
{
    m_views[m_active].file = path;
    const auto& id = m_views[m_active].definition.id;
    if (!id.empty())
        m_libraryFiles[id] = std::move(path);
}

void CircuitViews::rememberSubcircuitFile(const std::string& id, std::filesystem::path path)
{
    m_libraryFiles[id] = std::move(path);
}

void CircuitViews::replaceMain(Scene scene, std::string name, Input& input)
{
    replaceWorkspaceAt(0, std::move(scene), std::move(name), input);
}

void CircuitViews::replaceWorkspace(Scene scene, std::string name, Input& input)
{
    replaceWorkspaceAt(m_active, std::move(scene), std::move(name), input);
}

void CircuitViews::replaceWorkspaceAt(
    std::size_t index, Scene scene, std::string name, Input& input
)
{
    std::vector<ComponentDefinition> imported;
    for (const auto& [id, definition] : scene.getComponentCatalog().definitions())
        if (std::holds_alternative<SubcircuitBehavior>(definition.behavior))
            imported.push_back(definition);
    includeLibrary(scene);
    EditBatch updates;
    for (const auto& definition : imported)
        updates.push_back(RegisterComponentDefinition{definition, true});
    std::vector<Scene> staged;
    for (std::size_t i = 0; i < m_views.size(); ++i)
    {
        staged.push_back(i == index ? std::move(scene) : *m_views[i].scene);
        const auto result = EditorActions(staged.back()).apply(updates);
        if (!result)
            throw std::invalid_argument(result.message);
    }
    const auto result = EditorActions(*m_views[index].scene)
                            .restore(staged[index], m_views[index].scene->getRevision());
    if (!result)
        throw std::invalid_argument(result.message);
    for (std::size_t i = 0; i < m_views.size(); ++i)
        if (i != index)
            *m_views[i].scene = std::move(staged[i]);
    for (const auto& definition : imported)
        m_library.insert_or_assign(definition.identity.id, definition);
    rename(index, std::move(name));
    select(index, input);
    activeScene().propagate();
    activeScene().syncVisuals();
    markSaved(index);
}

void CircuitViews::publishSubcircuits(const std::vector<ComponentDefinition>& definitions)
{
    if (definitions.empty())
        return;
    EditBatch batch;
    for (const auto& definition : definitions)
        batch.push_back(RegisterComponentDefinition{definition, true});
    std::vector<Scene> staged;
    for (const auto& view : m_views)
    {
        staged.push_back(*view.scene);
        const auto result = EditorActions(staged.back()).apply(batch);
        if (!result)
            throw std::invalid_argument(result.message);
    }
    // EditorActions produced complete reversible scenes; retain stable scene addresses for Input.
    for (std::size_t i = 0; i < m_views.size(); ++i)
        *m_views[i].scene = std::move(staged[i]);
    for (const auto& definition : definitions)
        m_library.insert_or_assign(definition.identity.id, definition);
}

void CircuitViews::openSubcircuit(
    Scene scene,
    DefinitionIdentity identity,
    std::string name,
    std::filesystem::path file,
    Input& input
)
{
    for (std::size_t i = 1; i < m_views.size(); ++i)
        if (role(i) == Role::Subcircuit && m_views[i].definition.id == identity.id)
        {
            select(i, input); // Keep an existing editor's unsaved work.
            return;
        }
    scene.setInterfaceNamingRequired(true);
    includeLibrary(scene);
    View view{std::move(name)};
    view.scene = std::make_unique<Scene>(std::move(scene));
    view.definition = std::move(identity);
    view.file = std::move(file);
    m_views.push_back(std::move(view));
    markSaved(m_views.size() - 1);
    select(m_views.size() - 1, input);
}

void CircuitViews::editSubcircuit(const ComponentDefinition& definition, Input& input)
{
    const auto* sub = std::get_if<SubcircuitBehavior>(&definition.behavior);
    if (!sub)
        throw std::invalid_argument("This component is not a subcircuit.");
    auto found = m_libraryFiles.find(definition.identity.id);
    openSubcircuit(
        *sub->authored,
        definition.identity,
        definition.displayName,
        found == m_libraryFiles.end() ? std::filesystem::path{} : found->second,
        input
    );
}
