#pragma once

#include "Editor/Actions/EditHistory.h"
#include "Editor/Scene.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Input;

/** @brief Independent editing scenes and the session's published subcircuit library. */
class CircuitViews
{
  public:
    enum class Role
    {
        Workspace,
        Subcircuit
    };

    static const char* roleName(Role role) { return role == Role::Subcircuit ? "Subcircuit editor" : "Workspace"; }

    struct SimulationIssue
    {
        std::string message;
        std::optional<GridCoords> location;
        bool warning = false;

        bool operator==(const SimulationIssue&) const = default;
    };

    /** @brief Session-only diagnostics belong to the view, never to the saved circuit. */

    struct Messages
    {
        std::vector<std::string> entries;
        std::vector<SimulationIssue> issues;
        bool unread = false;
        int scroll = 0;
        SimulationResult simulationResult = SimulationResult::OK;
        std::size_t shortedNets = 0;
        double backlogSince = -1;
        bool behind = false;
        std::string fileStatus;
        bool fileError = false;
    };

    Messages& activeMessages() { return m_views[m_active].messages; }
    const Messages& activeMessages() const { return m_views[m_active].messages; }
    ClockTimeline& activeClockTimeline() { return *m_views[m_active].clockTimeline; }
    const ClockTimeline& activeClockTimeline() const { return *m_views[m_active].clockTimeline; }

    CircuitViews();

    std::size_t size() const { return m_views.size(); }
    std::size_t activeIndex() const { return m_active; }
    Scene& activeScene() { return *m_views[m_active].scene; }
    const Scene& activeScene() const { return *m_views[m_active].scene; }
    Scene& displayedScene() { return inspecting() ? *m_inspections.back().scene : activeScene(); }
    const Scene& displayedScene() const { return inspecting() ? *m_inspections.back().scene : activeScene(); }
    bool inspecting() const { return !m_inspections.empty(); }

    std::string inspectionLabel() const;

    /** @brief Open a read-only view onto the selected subcircuit's running instance. */
    bool inspectComponent(int componentId, Input& input);

    ClockPath signalPath(int authoredId) const;

    bool leaveInspection(Input& input);

    void closeInspection(Input& input);

    /** @brief Re-resolves the instance path; parent design changes safely dismiss stale layouts. */
    bool refreshInspection(Input& input, bool sync = true);

    Scene& mainScene() { return *m_views[0].scene; }
    const std::string& name(std::size_t index) const { return m_views.at(index).name; }

    Role role(std::size_t index) const
    {
        return m_views.at(index).scene->requiresInterfaceNames() ? Role::Subcircuit : Role::Workspace;
    }

    /** @brief Sets authoring intent; subcircuit conversion fills missing vital names. Main stays a
     * workspace. */
    bool setRole(std::size_t index, Role role);

    /** @brief Adds an empty scene and switches to it, cancelling unfinished editor gestures. */
    void create(Input& input, Role role = Role::Workspace);

    /** @brief Saves the outgoing camera and restores the destination camera and scene. */
    bool select(std::size_t index, Input& input);

    /** @brief Removes a circuit; active removal switches left before freeing its scene. Main stays. */
    bool remove(std::size_t index, Input& input);

    void rename(std::size_t index, std::string name);

    DefinitionIdentity& activeDefinition() { return m_views[m_active].definition; }
    const std::filesystem::path& activeFile() const { return m_views[m_active].file; }

    void setActiveFile(std::filesystem::path path);

    void rememberSubcircuitFile(const std::string& id, std::filesystem::path path);

    void replaceMain(Scene scene, std::string name, Input& input);

    void replaceWorkspace(Scene scene, std::string name, Input& input);

    /** @brief Tracks persisted design data; simulation memory and camera changes are excluded. */
    bool hasUnsavedChanges(std::size_t index) const;

    /** @brief Capture the workspace's current design and source settings as its clean baseline. */
    void markSaved(std::size_t index);

    /** @brief Stages updates in every view before replacing any; changed used interfaces reject
     * atomically. */
    void validatePublication(const std::vector<ComponentDefinition>& definitions) const;

    /** @brief Adds available library designs without replacing embedded workspace versions. */
    void loadLibrary(const std::vector<ComponentDefinition>& definitions);

    /** @brief Publish definition updates consistently across workspaces and discard history that retains replaced
     * versions.
     */
    void publishSubcircuits(const std::vector<ComponentDefinition>& definitions);

    void editSubcircuit(const ComponentDefinition& definition, Input& input);

    /** @brief Open a saved reusable design in its own editable workspace with library definitions available. */
    void openSubcircuit(
        Scene scene, DefinitionIdentity identity, std::string name, std::filesystem::path file, Input& input
    );

    void includeLibrary(Scene& scene) const;

  private:
    struct Inspection
    {
        int runtimeId;
        std::string name;
        std::unique_ptr<Scene> scene;
        glm::vec2 parentPan;
        float parentZoom;
    };

    std::vector<Inspection> m_inspections;
    std::uint64_t m_inspectedRevision = 0;

    struct SourceSettings
    {
        int componentId;
        bool inputState = false;
        float clockFrequency = 0;
        bool clockPaused = false;

        bool operator==(const SourceSettings&) const = default;
    };

    struct View
    {
        std::string name;
        std::unique_ptr<Scene> scene = std::make_unique<Scene>();
        std::unique_ptr<EditHistory> history = std::make_unique<EditHistory>();
        glm::vec2 pan{0};
        float zoom = 1;
        DefinitionIdentity definition;
        std::filesystem::path file;
        std::optional<std::string> savedDesign;
        Role savedRole = Role::Workspace;
        mutable std::uint64_t checkedRevision = 0;
        mutable std::string checkedName;
        mutable Role checkedRole = Role::Workspace;
        mutable std::vector<SourceSettings> checkedSources;
        mutable bool unsaved = true;
        Messages messages;
        std::unique_ptr<ClockTimeline> clockTimeline = std::make_unique<ClockTimeline>();
    };

    static std::vector<SourceSettings> sourceSettings(const Scene& scene);

    void replaceWorkspaceAt(std::size_t index, Scene scene, std::string name, Input& input);

    std::vector<View> m_views;
    std::size_t m_active = 0;
    std::map<std::string, ComponentDefinition> m_library;
    std::map<std::string, std::filesystem::path> m_libraryFiles;
};
