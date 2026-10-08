#include "Editor/Actions/EditHistory.h"

#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <algorithm>
#include <set>

void EditHistory::record(const EditResult& result)
{
    if (!result || !result.change)
        return;
    m_redo.clear();
    m_undo.push_back(result.change);
    if (m_undo.size() > Limit)
        m_undo.pop_front();
}

void EditHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
}

EditResult EditHistory::restore(Scene& scene, const Scene& snapshot)
{
    Scene target(snapshot);
    // Keep unused session-library entries added/updated since the historical edit.
    std::set<std::string> used;
    for (const auto& [id, view] : target.getComponentViewMap())
        used.insert(view->getDefinitionIdentity().id);
    EditBatch definitions;
    for (const auto& [id, definition] : scene.getComponentCatalog().definitions())
    {
        if (used.contains(id))
            continue;
        const auto* old = target.getComponentCatalog().find(id);
        if (!old || old->identity.version != definition.identity.version)
            definitions.push_back(RegisterComponentDefinition{definition, old != nullptr});
    }
    const auto prepared = EditorActions(target).apply(definitions);
    if (!prepared)
        return prepared;
    auto result = EditorActions(scene).restore(target, scene.getRevision());
    if (result)
    {
        scene.propagate();
        scene.syncVisuals();
    }
    return result;
}

EditResult EditHistory::undo(Scene& scene)
{
    if (m_undo.empty())
        return {};
    const auto record = m_undo.back();
    auto result = restore(scene, *record->before);
    if (result)
    {
        m_undo.pop_back();
        m_redo.push_back(record);
    }
    return result;
}

EditResult EditHistory::redo(Scene& scene)
{
    if (m_redo.empty())
        return {};
    const auto record = m_redo.back();
    auto result = restore(scene, *record->after);
    if (result)
    {
        m_redo.pop_back();
        m_undo.push_back(record);
    }
    return result;
}

bool EditHistory::referencesDefinition(const std::string& id) const
{
    auto uses = [&](const auto& records)
    {
        return std::any_of(
            records.begin(),
            records.end(),
            [&](const auto& record)
            {
                for (const auto* image : {record->before.get(), record->after.get()})
                    for (const auto& [componentId, view] : image->getComponentViewMap())
                        if (view->getDefinitionIdentity().id == id)
                            return true;
                return false;
            }
        );
    };
    return uses(m_undo) || uses(m_redo);
}
