#include "Editor/InterfaceComponents.h"

#include "Editor/Scene.h"

#include <algorithm>
#include <array>
#include <set>

bool blankComponentName(const std::string& name)
{
    return name.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::vector<InterfaceComponent> interfaceComponents(const Scene& scene)
{
    std::vector<InterfaceComponent> result;
    for (const auto& [id, ignored] : scene.getComponentViewMap())
    {
        const auto* view = scene.getCommittedComponentView(id);
        const auto* logic = scene.getLogicComponent(id);
        std::optional<InterfaceKind> kind;
        if (dynamic_cast<const InputPin*>(logic))
            kind = InterfaceKind::Input;
        else if (dynamic_cast<const OutputPin*>(logic))
            kind = InterfaceKind::Output;
        else if (dynamic_cast<const Clock*>(logic))
            kind = InterfaceKind::Clock;
        if (kind)
            result.push_back({id, *kind, view->getBodyLabel()});
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& a, const auto& b)
        { return a.kind == b.kind ? a.id < b.id : a.kind < b.kind; }
    );
    return result;
}

EditBatch missingInterfaceNames(const Scene& scene)
{
    const auto components = interfaceComponents(scene);
    std::array<std::set<std::string>, 3> names;
    for (const auto& component : components)
        if (!blankComponentName(component.name))
            names[static_cast<int>(component.kind)].insert(component.name);
    EditBatch edits;
    const char* prefixes[] = {"input ", "output ", "clock "};
    for (const auto& component : components)
        if (blankComponentName(component.name))
        {
            const int kind = static_cast<int>(component.kind);
            int index = 1;
            std::string name;
            do
            {
                name = prefixes[kind] + std::to_string(index++);
            } while (names[kind].contains(name));
            names[kind].insert(name);
            edits.push_back(
                ConfigureComponentProperties{.componentId = component.id, .label = name}
            );
        }
    return edits;
}
