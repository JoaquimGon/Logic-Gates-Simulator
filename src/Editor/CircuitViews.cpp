#include "Editor/CircuitViews.h"

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
    m_views.push_back(View{"unnamed", role});
    select(m_views.size() - 1, input);
}

bool CircuitViews::setRole(std::size_t index, Role role)
{
    if (index >= m_views.size() || (role != Role::Workspace && role != Role::Subcircuit) ||
        (index == 0 && role != Role::Workspace))
        return false;
    m_views[index].role = role;
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
