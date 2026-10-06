#include "Editor/CircuitViews.h"

#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"

#include <stdexcept>
#include <utility>

CircuitViews::CircuitViews()
{
    m_views.push_back(View{"Main"});
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
    return m_views[index].scene->setInterfaceNamingRequired(role == Role::Subcircuit);
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
    std::vector<ComponentDefinition> imported;
    for (const auto& [id, definition] : scene.getComponentCatalog().definitions())
        if (std::holds_alternative<SubcircuitBehavior>(definition.behavior))
            imported.push_back(definition);
    includeLibrary(scene);
    EditBatch updates;
    for (const auto& definition : imported)
        updates.push_back(RegisterComponentDefinition{definition, true});
    std::vector<Scene> staged;
    staged.push_back(std::move(scene));
    for (std::size_t i = 1; i < m_views.size(); ++i)
    {
        staged.push_back(*m_views[i].scene);
        const auto result = EditorActions(staged.back()).apply(updates);
        if (!result)
            throw std::invalid_argument(result.message);
    }
    const auto result =
        EditorActions(mainScene()).restore(staged.front(), mainScene().getRevision());
    if (!result)
        throw std::invalid_argument(result.message);
    for (std::size_t i = 1; i < m_views.size(); ++i)
        *m_views[i].scene = std::move(staged[i]);
    for (const auto& definition : imported)
        m_library.insert_or_assign(definition.identity.id, definition);
    rename(0, std::move(name));
    select(0, input);
    mainScene().propagate();
    mainScene().syncVisuals();
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
