#pragma once

#include "Editor/Scene.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Input;

/** Independent editing scenes and the session's published subcircuit library. */
class CircuitViews
{
  public:
    enum class Role
    {
        Workspace,
        Subcircuit
    };

    static const char* roleName(Role role)
    {
        return role == Role::Subcircuit ? "Subcircuit editor" : "Workspace";
    }

    CircuitViews();

    std::size_t size() const { return m_views.size(); }

    std::size_t activeIndex() const { return m_active; }

    Scene& activeScene() { return *m_views[m_active].scene; }

    const Scene& activeScene() const { return *m_views[m_active].scene; }

    Scene& mainScene() { return *m_views[0].scene; }

    const std::string& name(std::size_t index) const { return m_views.at(index).name; }

    Role role(std::size_t index) const
    {
        return m_views.at(index).scene->requiresInterfaceNames() ? Role::Subcircuit
                                                                 : Role::Workspace;
    }

    /** Sets authoring intent; subcircuit conversion fills missing vital names. Main stays a
     * workspace. */
    bool setRole(std::size_t index, Role role);

    /** Adds an empty scene and switches to it, cancelling unfinished editor gestures. */
    void create(Input& input, Role role = Role::Workspace);
    /** Saves the outgoing camera and restores the destination camera and scene. */
    bool select(std::size_t index, Input& input);
    /** Removes a circuit; active removal switches left before freeing its scene. Main stays. */
    bool remove(std::size_t index, Input& input);
    void rename(std::size_t index, std::string name);

    DefinitionIdentity& activeDefinition() { return m_views[m_active].definition; }

    const std::filesystem::path& activeFile() const { return m_views[m_active].file; }

    void setActiveFile(std::filesystem::path path);
    void rememberSubcircuitFile(const std::string& id, std::filesystem::path path);
    void replaceMain(Scene scene, std::string name, Input& input);
    /** Stages updates in every view before replacing any; changed used interfaces reject
     * atomically. */
    void publishSubcircuits(const std::vector<ComponentDefinition>& definitions);
    void editSubcircuit(const ComponentDefinition& definition, Input& input);
    void openSubcircuit(
        Scene scene,
        DefinitionIdentity identity,
        std::string name,
        std::filesystem::path file,
        Input& input
    );
    void includeLibrary(Scene& scene) const;

  private:
    struct View
    {
        std::string name;
        std::unique_ptr<Scene> scene = std::make_unique<Scene>();
        glm::vec2 pan{0};
        float zoom = 1;
        DefinitionIdentity definition;
        std::filesystem::path file;
    };

    std::vector<View> m_views;
    std::size_t m_active = 0;
    std::map<std::string, ComponentDefinition> m_library;
    std::map<std::string, std::filesystem::path> m_libraryFiles;
};
